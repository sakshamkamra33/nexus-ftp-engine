// ============================================================================
// auth.cpp — Authentication implementation
// ============================================================================
#include "auth.h"
#include "../common/logger.h"
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>   // rand, srand
#include <cstring>

namespace ftp {

// ─── Salt generation ──────────────────────────────────────────────────────────
// Generates a simple 16-char hex salt from rand().
// Production: use CryptGenRandom (Win) / /dev/urandom (POSIX).
std::string AuthManager::generateSalt() {
    static bool seeded = false;
    if (!seeded) { srand((unsigned)time(nullptr)); seeded = true; }
    std::ostringstream ss;
    for (int i = 0; i < 4; i++)
        ss << std::hex << std::uppercase << (rand() & 0xFFFF);
    return ss.str();
}

// ─── Hash a password ─────────────────────────────────────────────────────────
std::string AuthManager::hashPassword(const std::string& password,
                                      const std::string& saltIn) {
    std::string salt = saltIn.empty() ? generateSalt() : saltIn;
    std::string hash = sha256Salted(salt, password);
    return "sha256:" + salt + ":" + hash;
}

// ─── Load users ──────────────────────────────────────────────────────────────
// Format:
//   username=plain_password          (legacy plain)
//   username=sha256:<salt>:<hash>    (hashed)
//   # comment lines ignored
bool AuthManager::loadUsers(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Auth", "Cannot open users file: " + path +
                 " — server will reject all logins");
        return false;
    }

    threading::LockGuard lock(userMutex_);
    users_.clear();

    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip blanks and comments
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string user  = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (user.empty() || value.empty()) continue;

        UserRecord rec;
        // Detect hashed format: "sha256:<salt>:<hash>"
        if (value.substr(0, 7) == "sha256:") {
            auto p1 = value.find(':', 7);
            if (p1 != std::string::npos) {
                rec.hashed = true;
                rec.salt   = value.substr(7, p1 - 7);
                rec.stored = value.substr(p1 + 1);
                users_[user] = rec;
                count++;
            }
        } else {
            // Plain text (legacy / migration)
            rec.hashed = false;
            rec.salt   = "";
            rec.stored = value;
            users_[user] = rec;
            count++;
            LOG_WARN("Auth", "User '" + user + "' has plain-text password. "
                     "Run hash_password tool to upgrade.");
        }
    }

    LOG_INFO("Auth", "Loaded " + std::to_string(count) + " users from " + path);
    return count > 0;
}

// ─── Authenticate ────────────────────────────────────────────────────────────
bool AuthManager::authenticate(const std::string& user, const std::string& pass) {
    threading::LockGuard lock(userMutex_);
    auto it = users_.find(user);
    if (it == users_.end()) return false;

    const UserRecord& rec = it->second;
    if (rec.hashed) {
        // Compare sha256(salt:password) with stored hash
        return sha256Salted(rec.salt, pass) == rec.stored;
    } else {
        // Plain text comparison (legacy)
        return rec.stored == pass;
    }
}

// ─── Rate limiting ────────────────────────────────────────────────────────────
static long long nowSecs() { return (long long)time(nullptr); }

bool AuthManager::isRateLimited(const std::string& ip) {
    threading::LockGuard lock(rateMutex_);
    auto it = rateMap_.find(ip);
    if (it == rateMap_.end()) return false;

    auto& info = it->second;
    if (info.failedAttempts >= kMaxAttempts) {
        if (nowSecs() < info.blockedUntilSec) {
            long long remaining = info.blockedUntilSec - nowSecs();
            LOG_WARN("Auth", "IP " + ip + " is rate-limited (" +
                     std::to_string(remaining) + "s remaining)");
            return true;
        }
        // Block expired — reset
        info.failedAttempts = 0;
        info.blockedUntilSec = 0;
    }
    return false;
}

void AuthManager::recordFailedAttempt(const std::string& ip) {
    threading::LockGuard lock(rateMutex_);
    auto& info = rateMap_[ip];
    info.failedAttempts++;
    totalFailed_++;

    if (info.failedAttempts >= kMaxAttempts) {
        info.blockedUntilSec = nowSecs() + kBlockSecs;
        LOG_WARN("Auth", "IP " + ip + " blocked for " +
                 std::to_string(kBlockSecs) + "s after " +
                 std::to_string(kMaxAttempts) + " failed attempts");
    } else {
        LOG_INFO("Auth", "Failed login from " + ip +
                 " (" + std::to_string(info.failedAttempts) +
                 "/" + std::to_string(kMaxAttempts) + " attempts)");
    }
}

void AuthManager::clearAttempts(const std::string& ip) {
    threading::LockGuard lock(rateMutex_);
    rateMap_.erase(ip);
}

} // namespace ftp
