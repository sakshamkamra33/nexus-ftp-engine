// ============================================================================
// main.cpp — Client entry point
// ============================================================================
#include "../common/platform.h"
#include "ftp_client.h"
#include <iostream>
#include <string>

int main() {
    // Initialize platform
    if (!ftp::platform::initNetworking()) {
        std::cerr << "Fatal: Failed to initialize networking\n";
        return 1;
    }

    std::cout << "╔══════════════════════════════════════╗\n"
              << "║       FTP Client v2.0 (Enhanced)     ║\n"
              << "╚══════════════════════════════════════╝\n\n";

    // Get server address
    std::string host;
    std::cout << "Server address: ";
    std::cin >> host;

    int port = 21;
    std::cout << "Port [21]: ";
    std::string portStr;
    std::getline(std::cin >> std::ws, portStr);
    if (!portStr.empty()) {
        try { port = std::stoi(portStr); } catch (...) { port = 21; }
    }

    // Connect
    ftp::FTPClient client;
    if (!client.connect(host, port)) {
        std::cerr << "Error: Connection failed\n";
        ftp::platform::cleanupNetworking();
        return 1;
    }

    // Login
    std::string user, pass;
    std::cout << "\nUsername: ";
    std::cin >> user;
    std::cout << "Password: ";
    std::cin >> pass;

    if (!client.login(user, pass)) {
        std::cerr << "Error: Login failed\n";
        ftp::platform::cleanupNetworking();
        return 1;
    }

    std::cout << "\n✓ Login successful!\n";

    // Run interactive command loop
    client.commandLoop();
    client.disconnect();

    std::cout << "Disconnected. Goodbye!\n";
    ftp::platform::cleanupNetworking();
    return 0;
}
