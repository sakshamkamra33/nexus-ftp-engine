// ============================================================================
// session.h — FTP session state machine
// ============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/logger.h"
#include "../common/win32_threads.h"
#include "auth.h"
#include "data_channel.h"
#include "transfer.h"
#include <string>
#include <cstdint>

namespace ftp {

struct ServerContext;

enum class SessionState {
    CONNECTED, WAIT_USER, WAIT_PASS, READY, TRANSFERRING, CLOSED
};

class Session {
public:
    Session(platform::SocketHandle clientSocket,
            const std::string& clientIp,
            ServerContext& ctx);

    void run();

    const std::string& clientIp()        const { return clientIp_; }
    SessionState       state()           const { return state_; }
    uint64_t           bytesTransferred()const { return bytesTransferred_; }

private:
    void sendReply(const std::string& reply);
    std::string recvCommand();
    std::string cleanCommand(const std::string& cmd);

    void handleUser(const std::string& arg);
    void handlePass(const std::string& arg);
    void handleQuit();
    void handleSyst();
    void handleType(const std::string& arg);
    void handlePwd();
    void handleCwd (const std::string& arg);
    void handleCdup();
    void handlePasv();
    void handlePort(const std::string& arg);
    void handleList();
    void handleNlst();
    void handleRetr(const std::string& arg);
    void handleStor(const std::string& arg);
    void handleDele(const std::string& arg);
    void handleMkd (const std::string& arg);
    void handleRmd (const std::string& arg);
    void handleRnfr(const std::string& arg);
    void handleRnto(const std::string& arg);
    void handleSize(const std::string& arg);
    void handleMdtm(const std::string& arg);
    void handleRest(const std::string& arg);  // Transfer resume
    void handleFeat();                        // Features list

    std::string resolvePath(const std::string& arg);
    bool        isWithinRoot(const std::string& path);
    bool        isSafeFilename(const std::string& name);

    platform::ManagedSocket socket_;
    std::string             clientIp_;
    ServerContext&          ctx_;
    SessionState            state_            = SessionState::CONNECTED;
    std::string             username_;
    std::string             userRootDir_;
    std::string             currentDir_;
    std::string             renameFrom_;
    DataChannel             dataChannel_;
    RateLimiter             rateLimiter_;      // per-connection bandwidth limit
    uint64_t                bytesTransferred_ = 0;
    uint64_t                restartOffset_    = 0; // REST command offset
};

// Shared server-wide state — injected into each Session
struct ServerContext {
    AuthManager&          auth;
    std::string           rootDir;
    threading::AtomicU64  totalConnections;
    threading::AtomicU64  activeConnections;
    threading::AtomicU64  totalBytesTransferred;
    // Security settings
    int                   timeoutSecs      = 300;
    uint64_t              maxFileSizeBytes = 0;
    bool                  allowAnonymous   = false;
    // Performance settings
    uint64_t              bandwidthLimitBps = 0;  // 0 = unlimited
    int                   sendBufKB        = 256; // SO_SNDBUF
    int                   recvBufKB        = 64;  // SO_RCVBUF
};

} // namespace ftp
