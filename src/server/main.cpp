// ============================================================================
// main.cpp — Server entry point
//
// Clean entry point: load config → init platform → start server.
// The main() function should be trivially simple — all logic lives in modules.
// ============================================================================
#include "../common/platform.h"
#include "../common/config.h"
#include "../common/logger.h"
#include "ftp_server.h"
#include <iostream>
#include <csignal>

static ftp::FTPServer* g_server = nullptr;

// Signal handler for graceful shutdown (Ctrl+C)
void signalHandler(int signum) {
    (void)signum;
    LOG_INFO("Main", "Received shutdown signal (Ctrl+C)");
    if (g_server) g_server->shutdown();
}

int main(int argc, char* argv[]) {
    // Determine config file path
    std::string configPath = "config/server.conf";
    if (argc > 1) configPath = argv[1];

    // Load configuration
    ftp::Config config;
    if (!config.load(configPath)) {
        std::cerr << "Warning: Could not load " << configPath
                  << " — using defaults\n";
    }

    // Initialize platform networking
    if (!ftp::platform::initNetworking()) {
        std::cerr << "Fatal: Failed to initialize networking\n";
        return 1;
    }

    // Register signal handler for clean Ctrl+C shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create and start server
    ftp::FTPServer server(config);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "Fatal: Server failed to start\n";
        ftp::platform::cleanupNetworking();
        return 1;
    }

    // Cleanup
    ftp::platform::cleanupNetworking();
    return 0;
}
