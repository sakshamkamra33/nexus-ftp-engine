// ============================================================================
// platform.cpp — All OS-specific code lives here, isolated from headers
// ============================================================================

// Platform socket includes FIRST, in this .cpp only
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mswsock.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "mswsock.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <dirent.h>
  #include <sys/stat.h>
  #include <cerrno>
  #include <cstring>
#endif

#include "platform.h"
#include <cstdio>
#include <sstream>
#include <ctime>
#include <sys/stat.h>

namespace ftp {
namespace platform {

// ─── Win/POSIX socket handle conversion ─────────────────────────────────────
#ifdef _WIN32
  static inline SOCKET toWin(SocketHandle h) { return (SOCKET)(uintptr_t)h; }
  static inline SocketHandle fromWin(SOCKET s) {
      return (s == INVALID_SOCKET) ? kInvalidSocket : (SocketHandle)(uintptr_t)s;
  }
#else
  static inline int toPosix(SocketHandle h) { return (int)h; }
  static inline SocketHandle fromPosix(int fd) { return (fd < 0) ? kInvalidSocket : (SocketHandle)fd; }
#endif

// ─── Lifecycle ───────────────────────────────────────────────────────────────
bool initNetworking() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2,2), &w) == 0;
#else
    return true;
#endif
}

void cleanupNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeSocket(SocketHandle sock) {
#ifdef _WIN32
    closesocket(toWin(sock));
#else
    ::close(toPosix(sock));
#endif
}

std::string getSocketError() {
#ifdef _WIN32
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr,
                   WSAGetLastError(), 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    return std::string(strerror(errno));
#endif
}

// ─── Socket creation/connection ──────────────────────────────────────────────
SocketHandle createTcpSocket() {
#ifdef _WIN32
    return fromWin(socket(AF_INET, SOCK_STREAM, 0));
#else
    return fromPosix(socket(AF_INET, SOCK_STREAM, 0));
#endif
}

int bindSocket(SocketHandle sock, int port) {
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
#ifdef _WIN32
    return bind(toWin(sock), (sockaddr*)&addr, sizeof(addr));
#else
    return bind(toPosix(sock), (sockaddr*)&addr, sizeof(addr));
#endif
}

int listenSocket(SocketHandle sock, int backlog) {
#ifdef _WIN32
    return listen(toWin(sock), backlog);
#else
    return listen(toPosix(sock), backlog);
#endif
}

SocketHandle acceptConnection(SocketHandle server) {
    sockaddr_in addr{};
    int len = sizeof(addr);
#ifdef _WIN32
    return fromWin(accept(toWin(server), (sockaddr*)&addr, &len));
#else
    return fromPosix(accept(toPosix(server), (sockaddr*)&addr, (socklen_t*)&len));
#endif
}

SocketHandle connectSocket(const std::string& ip, int port) {
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(s); return kInvalidSocket;
    }
    return fromWin(s);
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(fd); return kInvalidSocket;
    }
    return fromPosix(fd);
#endif
}

int sendData(SocketHandle sock, const char* buf, int len) {
#ifdef _WIN32
    return send(toWin(sock), buf, len, 0);
#else
    return (int)send(toPosix(sock), buf, len, 0);
#endif
}

int recvData(SocketHandle sock, char* buf, int len) {
#ifdef _WIN32
    return recv(toWin(sock), buf, len, 0);
#else
    return (int)recv(toPosix(sock), buf, len, 0);
#endif
}

void setReuseAddr(SocketHandle sock) {
    int opt = 1;
#ifdef _WIN32
    setsockopt(toWin(sock), SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(toPosix(sock), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
}

void setRecvTimeout(SocketHandle sock, int seconds) {
    if (seconds <= 0) return;
#ifdef _WIN32
    DWORD ms = (DWORD)(seconds * 1000);
    setsockopt(toWin(sock), SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = 0;
    setsockopt(toPosix(sock), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

void setKeepAlive(SocketHandle sock) {
    int opt = 1;
#ifdef _WIN32
    setsockopt(toWin(sock), SOL_SOCKET, SO_KEEPALIVE, (const char*)&opt, sizeof(opt));
#else
    setsockopt(toPosix(sock), SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
#endif
}

int getLocalPort(SocketHandle sock) {
    sockaddr_in addr{};
    int len = sizeof(addr);
#ifdef _WIN32
    getsockname(toWin(sock), (sockaddr*)&addr, &len);
#else
    getsockname(toPosix(sock), (sockaddr*)&addr, (socklen_t*)&len);
#endif
    return ntohs(addr.sin_port);
}

std::string getPeerIp(SocketHandle sock) {
    sockaddr_in addr{};
    int len = sizeof(addr);
#ifdef _WIN32
    getpeername(toWin(sock), (sockaddr*)&addr, &len);
#else
    getpeername(toPosix(sock), (sockaddr*)&addr, (socklen_t*)&len);
#endif
    return std::string(inet_ntoa(addr.sin_addr));
}

// ─── Filesystem ──────────────────────────────────────────────────────────────
std::vector<FileInfo> listDirectory(const std::string& path) {
    std::vector<FileInfo> entries;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return entries;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        FileInfo fi;
        fi.name        = name;
        fi.isDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fi.size        = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        entries.push_back(fi);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) return entries;
    struct dirent* e;
    while ((e = readdir(dir)) != nullptr) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        FileInfo fi;
        fi.name = name;
        struct stat st;
        if (stat((path + "/" + name).c_str(), &st) == 0) {
            fi.isDirectory = S_ISDIR(st.st_mode);
            fi.size        = (uint64_t)st.st_size;
        }
        entries.push_back(fi);
    }
    closedir(dir);
#endif
    return entries;
}

bool isDirectory(const std::string& path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool pathExists(const std::string& path) {
#ifdef _WIN32
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool deleteFile(const std::string& path) {
#ifdef _WIN32
    return DeleteFileA(path.c_str()) != 0;
#else
    return ::remove(path.c_str()) == 0;
#endif
}

bool createDir(const std::string& path) {
#ifdef _WIN32
    return CreateDirectoryA(path.c_str(), nullptr) != 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool removeDir(const std::string& path) {
#ifdef _WIN32
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

bool renameFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileA(from.c_str(), to.c_str()) != 0;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool removeDirRecursive(const std::string& path) {
    if (!isDirectory(path)) return deleteFile(path);
    auto entries = listDirectory(path);
    for (auto& e : entries) {
        std::string child = path + "/" + e.name;
        if (e.isDirectory) {
            if (!removeDirRecursive(child)) return false;
        } else {
            if (!deleteFile(child)) return false;
        }
    }
    return removeDir(path);
}

std::string getFileModTime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "";
    struct tm* t = gmtime(&st.st_mtime);
    if (!t) return "";
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return std::string(buf);
}

// ─── Socket buffer tuning ────────────────────────────────────────────────────
// Larger buffers reduce syscall overhead for bulk transfers.
// 256KB send / 64KB recv is optimal for LAN FTP.
void setSocketBuffers(SocketHandle sock, int sendBufKB, int recvBufKB) {
    int sb = sendBufKB * 1024;
    int rb = recvBufKB * 1024;
#ifdef _WIN32
    setsockopt(toWin(sock), SOL_SOCKET, SO_SNDBUF, (const char*)&sb, sizeof(sb));
    setsockopt(toWin(sock), SOL_SOCKET, SO_RCVBUF, (const char*)&rb, sizeof(rb));
#else
    setsockopt(toPosix(sock), SOL_SOCKET, SO_SNDBUF, &sb, sizeof(sb));
    setsockopt(toPosix(sock), SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));
#endif
}

// ─── Zero-copy file transfer ─────────────────────────────────────────────────
// Windows: TransmitFile — kernel-mode, bypasses user-space entirely.
// Linux:   sendfile()  — kernel-mode, no user-space buffer.
// Returns false if not supported; caller falls back to buffered mode.
bool transmitFile(SocketHandle sock, const std::string& path,
                  uint64_t offset, uint64_t& bytesSent)
{
    bytesSent = 0;

#ifdef _WIN32
    // Open the file as a HANDLE (required by TransmitFile)
    HANDLE hFile = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, // hint for prefetching
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // Seek to offset
    if (offset > 0) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN)) {
            CloseHandle(hFile);
            return false;
        }
    }

    // Get file size for bytesSent calculation
    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    uint64_t sendSize = (uint64_t)fileSize.QuadPart - offset;

    // TransmitFile: sends entire file from current position
    // nNumberOfBytesToWrite = 0 means "send all remaining"
    BOOL ok = TransmitFile(
        toWin(sock),
        hFile,
        0,          // nNumberOfBytesToWrite: 0 = all
        0,          // nNumberOfBytesPerSend: 0 = system default
        nullptr,    // lpOverlapped: synchronous
        nullptr,    // lpTransmitBuffers: no headers/trailers
        0           // dwFlags
    );

    CloseHandle(hFile);

    if (ok) {
        bytesSent = sendSize;
        return true;
    }
    return false; // Fall back to buffered

#else
    // POSIX sendfile (Linux)
    #ifdef __linux__
    #include <sys/sendfile.h>
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;

    off_t off = (off_t)offset;
    struct stat st;
    fstat(fd, &st);
    uint64_t toSend = (uint64_t)st.st_size - offset;

    ssize_t sent = sendfile(toPosix(sock), fd, &off, toSend);
    ::close(fd);

    if (sent >= 0) {
        bytesSent = (uint64_t)sent;
        return true;
    }
    return false;
    #else
    // macOS / BSD — no sendfile with same signature; use buffered
    (void)sock; (void)path; (void)offset;
    return false;
    #endif
#endif
}

} // namespace platform
} // namespace ftp
