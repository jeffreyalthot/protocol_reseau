#include "StratumTestClient.h"

#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <netdb.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string jsonEscape(const std::string& input) {
    std::ostringstream out;
    for (char c : input) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << c; break;
        }
    }
    return out.str();
}

std::string toHex(uint32_t value, size_t width) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << value;
    return oss.str();
}

} // namespace

StratumTestClient::StratumTestClient(ClientConfig cfg)
    : cfg_(std::move(cfg)), extranonce2Counter_(0), nonceCounter_(cfg_.nonceStart) {}

StratumTestClient::~StratumTestClient() {
    stop();
}

bool StratumTestClient::parseUrl(const std::string& url, std::string& host, uint16_t& port) const {
    static const std::regex re(R"(^stratum\+tcp://([^:/]+):(\d+)$)");
    std::smatch m;
    if (!std::regex_match(url, m, re)) {
        return false;
    }
    host = m[1];
    port = static_cast<uint16_t>(std::stoi(m[2]));
    return true;
}

bool StratumTestClient::connectSocket(const std::string& host, uint16_t port) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        return false;
    }

    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
        sock_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_ == -1) {
            continue;
        }
        if (connect(sock_, rp->ai_addr, rp->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return true;
        }
        close(sock_);
        sock_ = -1;
    }

    freeaddrinfo(result);
    return false;
}

bool StratumTestClient::sendJsonLine(const std::string& payload) {
    std::string withNewline = payload + "\n";
    ssize_t sent = send(sock_, withNewline.data(), withNewline.size(), 0);
    return sent == static_cast<ssize_t>(withNewline.size());
}

std::optional<std::string> StratumTestClient::recvLine() {
    std::string line;
    char c = 0;
    while (true) {
        ssize_t r = recv(sock_, &c, 1, 0);
        if (r <= 0) {
            return std::nullopt;
        }
        if (c == '\n') {
            return line;
        }
        if (c != '\r') {
            line.push_back(c);
        }
    }
}

bool StratumTestClient::connectAndAuthorize() {
    std::string host;
    uint16_t port = 0;
    if (!parseUrl(cfg_.stratumUrl, host, port)) {
        std::cerr << "URL stratum invalide. Format attendu: stratum+tcp://host:port\n";
        return false;
    }

    if (!connectSocket(host, port)) {
        std::cerr << "Connexion impossible a " << host << ":" << port << "\n";
        return false;
    }

    running_ = true;
    recvThread_ = std::thread(&StratumTestClient::receiverLoop, this);

    {
        std::ostringstream subscribe;
        subscribe << "{\"id\":" << messageId_++
                  << ",\"method\":\"mining.subscribe\",\"params\":[\"stratum-test-client/0.1\"]}";
        if (!sendJsonLine(subscribe.str())) {
            return false;
        }
    }

    {
        std::ostringstream auth;
        auth << "{\"id\":" << messageId_++ << ",\"method\":\"mining.authorize\",\"params\":[\""
             << jsonEscape(cfg_.walletOrUser + "." + cfg_.worker) << "\",\"" << jsonEscape(cfg_.password)
             << "\"]}";
        if (!sendJsonLine(auth.str())) {
            return false;
        }
    }

    return true;
}

void StratumTestClient::receiverLoop() {
    while (running_) {
        auto line = recvLine();
        if (!line) {
            running_ = false;
            break;
        }

        std::cout << "[RECV] " << *line << "\n";

        std::smatch m;
        if (std::regex_search(*line, m, std::regex(R"xx("result"\s*:\s*\[\s*\[[^\]]*\]\s*,\s*"([^"]+)")xx"))) {
            std::scoped_lock lock(stateMutex_);
            extranonce1_ = m[1];
        }

        if (line->find("\"method\":\"mining.notify\"") != std::string::npos) {
            std::regex notifyRe(R"xx("params"\s*:\s*\[\s*"([^"]+)"\s*,[^\]]*"([0-9a-fA-F]{8})")xx");
            if (std::regex_search(*line, m, notifyRe)) {
                std::scoped_lock lock(stateMutex_);
                lastJob_.jobId = m[1];
                lastJob_.ntime = m[2];
            }
        }

        if (line->find("\"method\":\"mining.set_extranonce\"") != std::string::npos) {
            std::regex extraRe(R"xx("params"\s*:\s*\[\s*"([^"]+)"\s*,\s*([0-9]+)\s*\])xx");
            if (std::regex_search(*line, m, extraRe)) {
                std::scoped_lock lock(stateMutex_);
                extranonce1_ = m[1];
                lastJob_.extranonce2Size = m[2];
            }
        }
    }
}

std::string StratumTestClient::nextExtranonce2() {
    std::scoped_lock lock(stateMutex_);
    size_t bytes = 4;
    if (!lastJob_.extranonce2Size.empty()) {
        bytes = static_cast<size_t>(std::stoul(lastJob_.extranonce2Size));
    }
    return toHex(extranonce2Counter_++, bytes * 2);
}

std::string StratumTestClient::nextNonce() {
    std::scoped_lock lock(stateMutex_);
    return toHex(nonceCounter_++, 8);
}

double StratumTestClient::shareIntervalSeconds() const {
    constexpr long double two32 = 4294967296.0L;
    const long double hashesPerSecond = cfg_.advertisedHashrateEh * 1'000'000'000'000'000'000.0L;
    const long double seconds = (cfg_.syntheticShareDifficulty * two32) / hashesPerSecond;
    const long double boundedSeconds = std::max(0.001L, seconds);
    return static_cast<double>(boundedSeconds);
}

void StratumTestClient::runSyntheticSubmitLoop() {
    const double interval = shareIntervalSeconds();
    std::cout << "Intervalle d'envoi synthetic share: " << interval << " s\n";

    while (running_) {
        std::string jobId;
        std::string ntime;
        {
            std::scoped_lock lock(stateMutex_);
            jobId = lastJob_.jobId;
            ntime = lastJob_.ntime;
        }

        if (!jobId.empty() && !ntime.empty()) {
            std::ostringstream submit;
            submit << "{\"id\":" << messageId_++ << ",\"method\":\"mining.submit\",\"params\":[\""
                   << jsonEscape(cfg_.walletOrUser + "." + cfg_.worker) << "\",\"" << jsonEscape(jobId)
                   << "\",\"" << nextExtranonce2() << "\",\"" << ntime << "\",\"" << nextNonce()
                   << "\"]}";

            if (!sendJsonLine(submit.str())) {
                std::cerr << "Erreur envoi submit.\n";
                running_ = false;
                break;
            }
            std::cout << "[SEND] synthetic share submitted\n";
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
}

void StratumTestClient::stop() {
    running_ = false;
    if (sock_ != -1) {
        shutdown(sock_, SHUT_RDWR);
        close(sock_);
        sock_ = -1;
    }
    if (recvThread_.joinable()) {
        recvThread_.join();
    }
}
