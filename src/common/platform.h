// ============================================================================
// platform.h — Cross-platform abstraction (NO winsock/windows includes here)
//
// This header is safe to include anywhere. Platform-specific socket headers
// are only included in platform.cpp via socket_includes.h.
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ftp {
namespace platform {

// ─── Opaque socket handle ────────────────────────────────────────────────────
// Use intptr_t so it fits both SOCKET (UINT_PTR on Win) and int (POSIX fd).
using SocketHandle = intptr_t;
constexpr SocketHandle kInvalidSocket = -1;

// ─── Networking lifecycle ────────────────────────────────────────────────────
bool initNetworking();
void cleanupNetworking();
void closeSocket(SocketHandle sock);
std::string getSocketError();

// ─── Raw socket calls (thin wrappers so callers don't need winsock) ──────────
SocketHandle createTcpSocket();
SocketHandle acceptConnection(SocketHandle server);
int          bindSocket(SocketHandle sock, int port);
int          listenSocket(SocketHandle sock, int backlog);
SocketHandle connectSocket(const std::string& ip, int port);
int          sendData(SocketHandle sock, const char* buf, int len);
int          recvData(SocketHandle sock, char* buf, int len);
void         setReuseAddr(SocketHandle sock);
void         setRecvTimeout(SocketHandle sock, int seconds);
void         setKeepAlive(SocketHandle sock);
void         setSocketBuffers(SocketHandle sock, int sendBufKB, int recvBufKB);

// Zero-copy file send: returns true if used, sets bytesSent.
// Falls back gracefully if TransmitFile/sendfile unavailable.
bool         transmitFile(SocketHandle sock, const std::string& path,
                          uint64_t offset, uint64_t& bytesSent);

bool         pathExists(const std::string& path);
bool         deleteFile(const std::string& path);
bool         createDir(const std::string& path);
bool         removeDir(const std::string& path);
bool         renameFile(const std::string& from, const std::string& to);
bool         isDirectory(const std::string& path);
int          getLocalPort(SocketHandle sock);
std::string  getPeerIp(SocketHandle sock);

// ─── RAII Socket Wrapper ─────────────────────────────────────────────────────
class ManagedSocket {
public:
    ManagedSocket() = default;
    explicit ManagedSocket(SocketHandle fd) : fd_(fd) {}
    ~ManagedSocket() { close(); }

    ManagedSocket(ManagedSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = kInvalidSocket; }
    ManagedSocket& operator=(ManagedSocket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = kInvalidSocket; }
        return *this;
    }
    ManagedSocket(const ManagedSocket&)            = delete;
    ManagedSocket& operator=(const ManagedSocket&) = delete;

    void         close()  { if (fd_ != kInvalidSocket) { closeSocket(fd_); fd_ = kInvalidSocket; } }
    SocketHandle get()    const { return fd_; }
    SocketHandle release()      { auto f = fd_; fd_ = kInvalidSocket; return f; }
    bool         valid()  const { return fd_ != kInvalidSocket; }
    explicit operator bool() const { return valid(); }

private:
    SocketHandle fd_ = kInvalidSocket;
};

// ─── Filesystem Abstractions ─────────────────────────────────────────────────
struct FileInfo {
    std::string name;
    bool        isDirectory = false;
    uint64_t    size        = 0;
};

std::vector<FileInfo> listDirectory(const std::string& path);
bool isDirectory  (const std::string& path);
bool pathExists   (const std::string& path);
bool deleteFile   (const std::string& path);
bool createDir    (const std::string& path);
bool removeDir    (const std::string& path);
bool renameFile   (const std::string& from, const std::string& to);
bool removeDirRecursive(const std::string& path);
std::string getFileModTime(const std::string& path);

} // namespace platform
} // namespace ftp
