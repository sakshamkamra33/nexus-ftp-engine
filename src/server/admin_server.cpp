// ============================================================================
// admin_server.cpp — Admin Server Implementation
// ============================================================================
#include "admin_server.h"
#include "ftp_server.h"
#include "../common/logger.h"
#include <sstream>
#include <cstring>
#include <algorithm>

namespace ftp {

AdminServer::AdminServer(FTPServer& server, ServerContext& ctx, int port)
    : server_(server), ctx_(ctx), port_(port) {
}

AdminServer::~AdminServer() {
    stop();
}

void AdminServer::start() {
    if (port_ <= 0) return;

    platform::SocketHandle sock = platform::createTcpSocket();
    if (sock == platform::kInvalidSocket) {
        LOG_ERROR("Admin", "Failed to create socket");
        return;
    }

    platform::setReuseAddr(sock);
    if (platform::bindSocket(sock, port_) < 0) {
        LOG_ERROR("Admin", "Failed to bind admin port " + std::to_string(port_));
        platform::closeSocket(sock);
        return;
    }

    if (platform::listenSocket(sock, 5) < 0) {
        LOG_ERROR("Admin", "Failed to listen on admin port");
        platform::closeSocket(sock);
        return;
    }

    listenSocket_ = platform::ManagedSocket(sock);
    running_.store(true);
    
    LOG_INFO("Admin", "Admin interface listening on port " + std::to_string(port_));
    
    // Start background thread
    thread_ = new threading::Thread([this]() { loop(); });
}

void AdminServer::stop() {
    if (!running_.exchange(false)) return;
    listenSocket_.close();
    if (thread_) {
        delete thread_;
        thread_ = nullptr;
    }
}

void AdminServer::loop() {
    while (running_.load()) {
        platform::SocketHandle clientSock = platform::acceptConnection(listenSocket_.get());
        if (clientSock == platform::kInvalidSocket) continue;

        // Security: Only allow local connections
        std::string peerIp = platform::getPeerIp(clientSock);
        if (peerIp != "127.0.0.1" && peerIp != "::1") {
            LOG_WARN("Admin", "Rejected remote admin attempt from " + peerIp);
            std::string msg = "Access Denied.\n";
            platform::sendData(clientSock, msg.c_str(), (int)msg.size());
            platform::closeSocket(clientSock);
            continue;
        }

        // Handle client synchronously (admin traffic is low)
        handleClient(clientSock);
        platform::closeSocket(clientSock);
    }
}

void AdminServer::handleClient(platform::SocketHandle client) {
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    int n = platform::recvData(client, buf, sizeof(buf) - 1);
    if (n <= 0) return;

    std::string request(buf, n);
    if (request.find("GET") != 0) return; // Only accept GET requests

    // Check if it's a shutdown request
    bool doStop = (request.find("GET /stop ") != std::string::npos);

    std::ostringstream html;
    html << "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>NexusFTP Dashboard</title>"
         << "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&display=swap' rel='stylesheet'>"
         << "<style>"
         << ":root { --bg: #0f172a; --card-bg: rgba(30, 41, 59, 0.7); --text: #f8fafc; --accent: #38bdf8; --danger: #fb7185; --success: #34d399; }"
         << "body { font-family: 'Inter', sans-serif; background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%); color: var(--text); margin: 0; padding: 20px; min-height: 100vh; display: flex; align-items: center; justify-content: center; }"
         << ".dashboard { background: var(--card-bg); backdrop-filter: blur(16px); -webkit-backdrop-filter: blur(16px); border: 1px solid rgba(255,255,255,0.1); border-radius: 24px; padding: 40px; width: 100%; max-width: 800px; box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5); }"
         << ".header { text-align: center; margin-bottom: 40px; }"
         << "h1 { font-weight: 800; font-size: 2.5rem; margin: 0; background: linear-gradient(to right, var(--accent), #818cf8); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -1px; }"
         << ".subtitle { color: #94a3b8; font-size: 1.1rem; margin-top: 8px; }"
         << ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 40px; }"
         << ".stat-card { background: rgba(15, 23, 42, 0.6); border: 1px solid rgba(255,255,255,0.05); border-radius: 16px; padding: 24px; transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1); position: relative; overflow: hidden; }"
         << ".stat-card:hover { transform: translateY(-5px); box-shadow: 0 10px 25px -5px rgba(0,0,0,0.5); border-color: rgba(255,255,255,0.15); }"
         << ".stat-card::before { content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 4px; background: var(--accent); opacity: 0.5; transition: opacity 0.3s; }"
         << ".stat-card:hover::before { opacity: 1; }"
         << ".stat-card.danger::before { background: var(--danger); }"
         << ".stat-card.success::before { background: var(--success); }"
         << ".stat-title { font-size: 0.875rem; color: #94a3b8; text-transform: uppercase; letter-spacing: 1px; font-weight: 600; margin-bottom: 12px; }"
         << ".stat-value { font-size: 2.5rem; font-weight: 800; margin: 0; line-height: 1; }"
         << ".actions { display: flex; gap: 16px; justify-content: center; flex-wrap: wrap; }"
         << ".btn { font-family: 'Inter', sans-serif; padding: 14px 28px; border-radius: 12px; font-size: 1rem; font-weight: 600; cursor: pointer; text-decoration: none; transition: all 0.2s; display: inline-flex; align-items: center; gap: 8px; border: none; }"
         << ".btn-primary { background: rgba(56, 189, 248, 0.1); color: var(--accent); border: 1px solid rgba(56, 189, 248, 0.2); }"
         << ".btn-primary:hover { background: var(--accent); color: #0f172a; box-shadow: 0 0 20px rgba(56,189,248,0.4); transform: scale(1.05); }"
         << ".btn-danger { background: rgba(251, 113, 133, 0.1); color: var(--danger); border: 1px solid rgba(251, 113, 133, 0.2); }"
         << ".btn-danger:hover { background: var(--danger); color: #0f172a; box-shadow: 0 0 20px rgba(251,113,133,0.4); transform: scale(1.05); }"
         << "@media (max-width: 600px) { .dashboard { padding: 24px; } h1 { font-size: 2rem; } .stat-value { font-size: 2rem; } .actions { flex-direction: column; } .btn { width: 100%; justify-content: center; } }"
         << "</style></head><body>"
         << "<div class='dashboard'>"
         << "<div class='header'><h1>NexusFTP Engine</h1><div class='subtitle'>Live Real-Time Telemetry</div></div>";

    if (doStop) {
        html << "<div style='text-align:center; padding: 40px;'><h2 style='color:var(--danger); font-size: 2rem;'>Initiating Graceful Shutdown...</h2><p style='color:#94a3b8;'>Flushing sockets and killing thread pool safely.</p></div>";
    } else {
        html << "<div class='grid'>"
             << "<div class='stat-card success'><div class='stat-title'>Active Sockets</div><div class='stat-value'>" << ctx_.activeConnections.load() << "</div></div>"
             << "<div class='stat-card'><div class='stat-title'>Total Connects</div><div class='stat-value'>" << ctx_.totalConnections.load() << "</div></div>"
             << "<div class='stat-card'><div class='stat-title'>Data Transferred</div><div class='stat-value'>" << (ctx_.totalBytesTransferred.load() / 1024) << "<span style='font-size:1rem; color:#94a3b8; margin-left:4px;'>KB</span></div></div>"
             << "<div class='stat-card danger'><div class='stat-title'>Auth Blocks</div><div class='stat-value'>" << ctx_.auth.failedLoginCount() << "</div></div>"
             << "</div>"
             << "<div class='actions'>"
             << "<a href='/' class='btn btn-primary'><svg width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.59-9.21l5.67-5.67'/></svg> Refresh Data</a>"
             << "<a href='/stop' class='btn btn-danger' onclick='return confirm(\"Are you sure you want to trigger a graceful shutdown? Active transfers will be permitted to finish.\");'><svg width='20' height='20' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10'/></svg> Shutdown Cluster</a>"
             << "</div>";
    }

    html << "</div></body></html>";

    std::string body = html.str();
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;

    std::string finalResp = response.str();
    platform::sendData(client, finalResp.c_str(), (int)finalResp.size());

    if (doStop) {
        LOG_INFO("Admin", "Shutdown requested via Web Dashboard");
        server_.shutdown();
    }
}

} // namespace ftp
