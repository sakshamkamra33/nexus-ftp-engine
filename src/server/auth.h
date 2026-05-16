// ============================================================================
// auth.h — Authentication: hashed passwords + rate limiting
//
// users.conf format (two supported):
//   Plain (legacy): username=password
//   Hashed:         username=sha256:<salt>:<hash>
//
// WHY sha256 + salt: Even if users.conf leaks, hashes can't be reversed.
// Salt prevents rainbow table attacks (two users with same password
// will have different hashes).
// ============================================================================
#pragma once

#include "../common/win32_threads.h"
#include "../common/sha256.h"
#include <string>
#include <unordered_map>

namespace ftp {

class AuthManager {
public:
    // Load credentials from file. Supports plain and hashed formats.
    bool loadUsers(const std::string& path);

    // Authenticate. Handles both plain and hashed formats transparently.
    bool authenticate(const std::string& user, const std::string& pass);

    // ── Rate limiting ──────────────────────────────────────────────────────
    bool isRateLimited(const std::string& ip);
    void recordFailedAttempt(const std::string& ip);
    void clearAttempts(const std::string& ip);

    // ── Stats ──────────────────────────────────────────────────────────────
    size_t   userCount()        const { return users_.size(); }
    uint64_t failedLoginCount() const { return totalFailed_; }

    // ── Utility: hash a password for storing in users.conf ─────────────────
    // Returns "sha256:<salt>:<hash>" ready to paste after "username="
    static std::string hashPassword(const std::string& password,
                                    const std::string& salt = "");

private:
    struct UserRecord {
        bool        hashed;  // true = sha256 format, false = plain
        std::string salt;    // empty if plain
        std::string stored;  // plain password or sha256 hash
    };

    std::unordered_map<std::string, UserRecord> users_;
    mutable threading::Mutex userMutex_;

    struct RateInfo {
        int       failedAttempts  = 0;
        long long blockedUntilSec = 0;
    };
    std::unordered_map<std::string, RateInfo> rateMap_;
    mutable threading::Mutex rateMutex_;

    static const int kMaxAttempts = 5;
    static const int kBlockSecs   = 60;
    uint64_t totalFailed_ = 0;

    static std::string generateSalt();
};

} // namespace ftp
