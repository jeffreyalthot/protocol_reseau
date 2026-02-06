#include "StratumTestClient.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> gStop{false};

void onSignal(int) {
    gStop = true;
}

void printUsage(const char* bin) {
    std::cout << "Usage:\n  " << bin
              << " <stratum_url> <wallet_or_user> <worker> [password=x] [ehs=1.0] [difficulty=1.0] [nonce_start=0]\n\n"
              << "Exemple:\n  " << bin
              << " stratum+tcp://127.0.0.1:3333 test.wallet rig01 x 1.0 8.0 0\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    ClientConfig cfg;
    cfg.stratumUrl = argv[1];
    cfg.walletOrUser = argv[2];
    cfg.worker = argv[3];
    if (argc > 4) cfg.password = argv[4];
    if (argc > 5) cfg.advertisedHashrateEh = std::stod(argv[5]);
    if (argc > 6) cfg.syntheticShareDifficulty = std::stod(argv[6]);
    if (argc > 7) cfg.nonceStart = static_cast<uint32_t>(std::stoul(argv[7]));

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    StratumTestClient client(cfg);
    if (!client.connectAndAuthorize()) {
        return 2;
    }

    std::thread worker([&client]() { client.runSyntheticSubmitLoop(); });

    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    client.stop();
    if (worker.joinable()) {
        worker.join();
    }

    return 0;
}
