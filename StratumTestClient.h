#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct MiningJob {
    std::string jobId;
    std::string ntime;
    std::string extranonce2Size;
};

struct ClientConfig {
    std::string stratumUrl;
    std::string walletOrUser;
    std::string worker;
    std::string password{"x"};
    double advertisedHashrateEh{1.0};
    double syntheticShareDifficulty{1.0};
    uint32_t nonceStart{0};
};

class StratumTestClient {
public:
    explicit StratumTestClient(ClientConfig cfg);
    ~StratumTestClient();

    bool connectAndAuthorize();
    void runSyntheticSubmitLoop();
    void stop();

private:
    bool parseUrl(const std::string& url, std::string& host, uint16_t& port) const;
    bool connectSocket(const std::string& host, uint16_t port);
    bool sendJsonLine(const std::string& payload);
    std::optional<std::string> recvLine();
    void receiverLoop();
    std::string nextExtranonce2();
    std::string nextNonce();
    double shareIntervalSeconds() const;

private:
    ClientConfig cfg_;
    int sock_{-1};
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    std::mutex stateMutex_;
    std::string extranonce1_;
    uint32_t extranonce2Counter_{0};
    uint32_t nonceCounter_{0};
    MiningJob lastJob_;

    uint64_t messageId_{1};
};
