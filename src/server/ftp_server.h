// ============================================================================
// ftp_server.h — FTP Server class
// ============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/config.h"
#include "../common/logger.h"
#include "../common/win32_threads.h"
#include "thread_pool.h"
#include "auth.h"
#include "session.h"
#include "admin_server.h"
#include <string>

namespace ftp {

class FTPServer {
public:
    explicit FTPServer(const Config& config);
    ~FTPServer();

    bool start();
    void shutdown();

    FTPServer(const FTPServer&)            = delete;
    FTPServer& operator=(const FTPServer&) = delete;

private:
    int         port_;
    std::string rootDir_;
    int         maxClients_;
    int         threadPoolSize_;
    int         adminPort_;

    platform::ManagedSocket listenSocket_;
    threading::AtomicBool   running_;

    AuthManager   auth_;
    ThreadPool*   pool_  = nullptr;
    AdminServer*  admin_ = nullptr;
    ServerContext context_;
};

} // namespace ftp
