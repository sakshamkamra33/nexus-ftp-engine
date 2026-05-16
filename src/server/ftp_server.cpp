// ============================================================================
// ftp_server.cpp
// ============================================================================
#include "ftp_server.h"
#include <sstream>

namespace ftp {

FTPServer::FTPServer(const Config& config)
    : running_(false)
    , context_{auth_, "", {}, {}, {}}
{
    port_           = config.getInt   ("port",           21);
    rootDir_        = config.getString("root_dir",       "ftproot");
    maxClients_     = config.getInt("max_clients", 100);
    threadPoolSize_ = config.getInt("thread_pool_size", 4);
    adminPort_      = config.getInt("admin_port", 8080);
    context_.rootDir = rootDir_;

    std::string usersFile = config.getString("users_file", "config/users.conf");
    auth_.loadUsers(usersFile);

    // ── Security settings ──────────────────────────────────────────────────
    context_.timeoutSecs     = config.getInt("timeout_secs", 300);
    context_.allowAnonymous  = (config.getString("allow_anonymous", "no") == "yes");
    int maxMB = config.getInt("max_file_size_mb", 0);
    context_.maxFileSizeBytes = (uint64_t)maxMB * 1024 * 1024;

    // ── Performance settings ───────────────────────────────────────────────
    int bwKbps = config.getInt("bandwidth_limit_kbps", 0);
    context_.bandwidthLimitBps = (uint64_t)bwKbps * 1024;
    context_.sendBufKB = config.getInt("send_buf_kb", 256);
    context_.recvBufKB = config.getInt("recv_buf_kb", 64);

    std::string lvl = config.getString("log_level", "info");
    if      (lvl == "debug") Logger::instance().setLevel(LogLevel::LVL_DEBUG);
    else if (lvl == "warn")  Logger::instance().setLevel(LogLevel::LVL_WARN);
    else if (lvl == "error") Logger::instance().setLevel(LogLevel::LVL_ERROR);
    else                     Logger::instance().setLevel(LogLevel::LVL_INFO);

    std::string logFile = config.getString("log_file", "");
    if (!logFile.empty()) Logger::instance().setLogFile(logFile);
}

FTPServer::~FTPServer() {
    shutdown();
    delete pool_;
}

bool FTPServer::start() {
    pool_ = new ThreadPool(threadPoolSize_);

    platform::SocketHandle sock = platform::createTcpSocket();
    if (sock == platform::kInvalidSocket) {
        LOG_FATAL("Server", "Failed to create socket");
        return false;
    }
    listenSocket_ = platform::ManagedSocket(sock);
    platform::setReuseAddr(sock);

    if (platform::bindSocket(sock, port_) < 0) {
        LOG_FATAL("Server", "Failed to bind port " + std::to_string(port_));
        return false;
    }
    if (platform::listenSocket(sock, maxClients_) < 0) {
        LOG_FATAL("Server", "Failed to listen");
        return false;
    }

    running_.store(true);

    LOG_INFO("Server", "====================================================");
    LOG_INFO("Server", "  FTP Server v2.0 - Enhanced Edition");
    LOG_INFO("Server", "====================================================");
    LOG_INFO("Server", "Port:       " + std::to_string(port_));
    LOG_INFO("Server", "Root:       " + rootDir_);
    LOG_INFO("Server", "Workers:    " + std::to_string(threadPoolSize_));
    LOG_INFO("Server", "MaxClients: " + std::to_string(maxClients_));
    LOG_INFO("Server", "Admin Port: " + (adminPort_ > 0 ? std::to_string(adminPort_) : "Disabled"));
    LOG_INFO("Server", "Users:      " + std::to_string(auth_.userCount()));
    LOG_INFO("Server", "====================================================");
    LOG_INFO("Server", "Ready. Press Ctrl+C to stop.");

    // Start admin console
    if (adminPort_ > 0) {
        admin_ = new AdminServer(*this, context_, adminPort_);
        admin_->start();
    }

    while (running_.load()) {
        platform::SocketHandle clientSock =
            platform::acceptConnection(listenSocket_.get());

        if (clientSock == platform::kInvalidSocket) {
            if (running_.load()) LOG_WARN("Server", "Accept returned invalid socket");
            continue;
        }

        std::string clientIp = platform::getPeerIp(clientSock);

        if (context_.activeConnections.load() >= (uint64_t)maxClients_) {
            std::string msg = "421 Server busy.\r\n";
            platform::sendData(clientSock, msg.c_str(), (int)msg.size());
            platform::closeSocket(clientSock);
            LOG_WARN("Server", "Rejected " + clientIp + " (at capacity)");
            continue;
        }

        LOG_INFO("Server", "New connection: " + clientIp +
                 " (active: " + std::to_string(context_.activeConnections.load() + 1) +
                 "/" + std::to_string(maxClients_) + ")");

        platform::SocketHandle cs = clientSock;
        std::string            ip = clientIp;
        ServerContext*        ctx = &context_;

        pool_->submit([cs, ip, ctx]() {
            Session session(cs, ip, *ctx);
            session.run();
        });
    }

    return true;
}

void FTPServer::shutdown() {
    if (!running_.exchange(false)) return; // Already stopped

    LOG_INFO("Server", "Shutting down...");
    listenSocket_.close();
    
    if (admin_) {
        admin_->stop();
        delete admin_;
        admin_ = nullptr;
    }
    
    if (pool_) pool_->shutdown();

    LOG_INFO("Server", "--- Final Statistics ---");
    LOG_INFO("Server", "Connections:       " + std::to_string(context_.totalConnections.load()));
    LOG_INFO("Server", "Bytes transferred: " + std::to_string(context_.totalBytesTransferred.load()));
    LOG_INFO("Server", "Failed logins:     " + std::to_string(auth_.failedLoginCount()));
    LOG_INFO("Server", "Shutdown complete.");
}

} // namespace ftp
