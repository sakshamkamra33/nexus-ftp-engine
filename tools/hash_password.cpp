// ============================================================================
// hash_password.cpp — Utility to generate hashed users.conf entries
//
// Usage:
//   hash_password.exe <username> <password>
//   hash_password.exe <username> <password> <custom_salt>
//
// Output:
//   Prints a ready-to-paste line for users.conf, e.g.:
//   admin=sha256:A3F9B2C1:5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8
// ============================================================================
#include "common/sha256.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: hash_password <username> <password> [salt]\n";
        std::cerr << "Example: hash_password admin mypassword\n";
        return 1;
    }

    std::string username = argv[1];
    std::string password = argv[2];
    std::string salt     = (argc >= 4) ? argv[3] : "FTP" + std::to_string(time(nullptr) & 0xFFFF);

    std::string hash   = ftp::sha256Salted(salt, password);
    std::string entry  = username + "=sha256:" + salt + ":" + hash;

    std::cout << "\n";
    std::cout << "# Paste this line into config/users.conf:\n";
    std::cout << entry << "\n\n";

    // Verify it round-trips correctly
    bool ok = (ftp::sha256Salted(salt, password) == hash);
    std::cout << "Verification: " << (ok ? "OK" : "FAILED") << "\n";

    return ok ? 0 : 1;
}
