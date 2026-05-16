// ============================================================================
// ftp_client.h — Refactored FTP client with RAII and clean API
// ============================================================================
#pragma once

#include "../common/platform.h"
#include <string>

namespace ftp {

class FTPClient {
public:
    FTPClient() = default;
    ~FTPClient();

    // Connection
    bool connect(const std::string& host, int port = 21);
    void disconnect();
    bool isConnected() const { return controlSocket_.valid(); }

    // Authentication
    bool login(const std::string& user, const std::string& pass);

    // Directory operations
    void list();
    void pwd();
    void cd(const std::string& dir);

    // File operations
    void download(const std::string& filename);
    void upload(const std::string& filename);
    void deleteFile(const std::string& filename);
    void mkdir(const std::string& dirname);
    void rmdir(const std::string& dirname);
    void rename(const std::string& oldName, const std::string& newName);

    // Help
    void printHelp();

    // Interactive command loop
    void commandLoop();

private:
    // Protocol I/O
    void sendCommand(const std::string& cmd);
    std::string readReply();
    int getReplyCode(const std::string& reply);

    // Data connection (passive mode)
    platform::ManagedSocket openDataConnection();

    platform::ManagedSocket controlSocket_;
};

} // namespace ftp
