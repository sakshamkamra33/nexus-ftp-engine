// ============================================================================
// admin_server.h — Local Admin Control Interface (Phase 6)
// Provides a telnet-compatible interface for real-time monitoring and shutdown.
// ============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/win32_threads.h"
#include "session.h"
#include <string>

namespace ftp {

class FTPServer;

class AdminServer {
public:
    AdminServer(FTPServer& server, ServerContext& ctx, int port);
    ~AdminServer();

    void start();
    void stop();

private:
    void loop();
    void handleClient(platform::SocketHandle client);

    FTPServer&            server_;
    ServerContext&        ctx_;
    int                   port_;
    platform::ManagedSocket listenSocket_;
    threading::AtomicBool running_;
    threading::Thread*    thread_ = nullptr;
};

} // namespace ftp
