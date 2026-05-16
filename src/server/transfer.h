// ============================================================================
// transfer.h — High-performance file transfer engine
//
// WHY (for FAANG interviews):
//   Naive approach: fread → user buffer → send → repeat = 2 copies per chunk
//   Zero-copy:      kernel reads file → kernel sends directly = 0 user copies
//
// Features:
//   - Zero-copy via TransmitFile (Win) / sendfile (Linux)
//   - Chunked fallback (64KB) for compatibility
//   - Bandwidth throttling (token bucket pattern)
//   - Transfer resume via byte offset
//   - Speed tracking (bytes/sec)
// ============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/logger.h"
#include <string>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace ftp {

// ─── Rate Limiter (Token Bucket) ─────────────────────────────────────────────
// Limits throughput to maxBytesPerSec. Zero = unlimited.
// Token bucket: tokens refill at rate R, consume 1 per byte sent.
// Simpler for FTP: track time window and sleep when over budget.
class RateLimiter {
public:
    explicit RateLimiter(uint64_t maxBytesPerSec = 0)
        : limit_(maxBytesPerSec), windowStart_(clock()), windowBytes_(0) {}

    // Call after sending `bytes` bytes. Will sleep if over rate.
    void throttle(uint64_t bytes) {
        if (limit_ == 0) return; // unlimited

        windowBytes_ += bytes;

        // How many bytes should we have sent in elapsed time?
        clock_t now     = clock();
        double elapsed  = (double)(now - windowStart_) / CLOCKS_PER_SEC;

        if (elapsed < 0.001) elapsed = 0.001; // avoid div-by-zero

        double budget = limit_ * elapsed;

        if ((double)windowBytes_ > budget) {
            // We're ahead of budget — calculate sleep needed
            double excess  = (double)windowBytes_ - budget;
            double sleepSec = excess / limit_;
            uint32_t sleepMs = (uint32_t)(sleepSec * 1000.0);
            if (sleepMs > 0) {
#ifdef _WIN32
                Sleep(sleepMs);
#else
                struct timespec ts;
                ts.tv_sec  = sleepMs / 1000;
                ts.tv_nsec = (sleepMs % 1000) * 1000000L;
                nanosleep(&ts, nullptr);
#endif
                // Reset window after sleeping
                windowStart_ = clock();
                windowBytes_ = 0;
            }
        }

        // Reset window every second to prevent integer overflow
        if (elapsed >= 1.0) {
            windowStart_ = clock();
            windowBytes_ = 0;
        }
    }

    void setLimit(uint64_t bytesPerSec) { limit_ = bytesPerSec; }
    uint64_t limit() const { return limit_; }

private:
    uint64_t limit_;
    clock_t  windowStart_;
    uint64_t windowBytes_;
};

// ─── Transfer Result ──────────────────────────────────────────────────────────
struct TransferResult {
    uint64_t bytes     = 0;
    double   seconds   = 0.0;
    bool     ok        = false;
    bool     rateLimitHit = false;

    double speedMBps() const {
        if (seconds < 0.0001) return 0.0;
        return (bytes / (1024.0 * 1024.0)) / seconds;
    }
    double speedKBps() const {
        if (seconds < 0.0001) return 0.0;
        return (bytes / 1024.0) / seconds;
    }
};

// ─── Transfer Engine ──────────────────────────────────────────────────────────
class TransferEngine {
public:
    static const int CHUNK_SIZE = 65536; // 64KB chunks (vs original 8KB = 8x more efficient)

    // ── Download: Server → Client (RETR) ─────────────────────────────────
    // offset: byte offset for REST/resume support (0 = start of file)
    static TransferResult sendFile(
        platform::SocketHandle  dataSock,
        const std::string&      filePath,
        uint64_t                offset,
        RateLimiter&            limiter,
        const std::string&      clientIp)
    {
        TransferResult result;
        clock_t startTime = clock();

        // Try zero-copy first, fall back to buffered
        bool zeroCopyOk = false;

#ifdef _WIN32
        zeroCopyOk = sendFileWin32(dataSock, filePath, offset, result, limiter);
#else
        zeroCopyOk = sendFilePosix(dataSock, filePath, offset, result, limiter);
#endif

        if (!zeroCopyOk) {
            // Buffered fallback
            sendFileBuffered(dataSock, filePath, offset, result, limiter);
        }

        result.seconds = (double)(clock() - startTime) / CLOCKS_PER_SEC;
        result.ok      = (result.bytes > 0 || offset == 0);

        LOG_INFO("Transfer",
            clientIp + " RETR [" + (zeroCopyOk ? "zero-copy" : "buffered") + "]"
            + " " + std::to_string(result.bytes) + "B"
            + " @ " + formatSpeed(result.speedMBps()) + " MB/s");

        return result;
    }

    // ── Upload: Client → Server (STOR) ───────────────────────────────────
    // appendOffset: 0 = overwrite, > 0 = APPE/resume mode
    static TransferResult recvFile(
        platform::SocketHandle  dataSock,
        const std::string&      filePath,
        uint64_t                appendOffset,
        RateLimiter&            limiter,
        uint64_t                maxFileSize,
        const std::string&      clientIp)
    {
        TransferResult result;
        clock_t startTime = clock();

        // Open file — seek to offset if appending/resuming
        const char* mode = (appendOffset > 0) ? "ab" : "wb";
        FILE* file = fopen(filePath.c_str(), mode);
        if (!file) {
            LOG_ERROR("Transfer", "Cannot open for write: " + filePath);
            return result;
        }
        if (appendOffset > 0 && mode[0] == 'w') {
            fseek(file, (long)appendOffset, SEEK_SET);
        }

        char buf[CHUNK_SIZE];
        int  n;
        bool overflow = false;

        while ((n = platform::recvData(dataSock, buf, sizeof(buf))) > 0) {
            // Max file size guard
            if (maxFileSize > 0 && (result.bytes + n) > maxFileSize) {
                overflow = true;
                break;
            }
            fwrite(buf, 1, n, file);
            result.bytes += n;
            limiter.throttle(n);
        }

        fclose(file);

        if (overflow) {
            platform::deleteFile(filePath);
            result.ok = false;
            result.rateLimitHit = true;
            LOG_WARN("Transfer", clientIp + " STOR aborted: max file size exceeded");
            return result;
        }

        result.seconds = (double)(clock() - startTime) / CLOCKS_PER_SEC;
        result.ok      = true;

        LOG_INFO("Transfer",
            clientIp + " STOR [buffered]"
            + " " + std::to_string(result.bytes) + "B"
            + " @ " + formatSpeed(result.speedMBps()) + " MB/s");

        return result;
    }

private:
    static std::string formatSpeed(double mbps) {
        char buf[32];
        if (mbps >= 1.0)  snprintf(buf, sizeof(buf), "%.2f", mbps);
        else              snprintf(buf, sizeof(buf), "%.3f", mbps);
        return std::string(buf);
    }

    // ── Windows zero-copy (TransmitFile) ─────────────────────────────────
#ifdef _WIN32
    static bool sendFileWin32(
        platform::SocketHandle dataSock,
        const std::string& path,
        uint64_t offset,
        TransferResult& result,
        RateLimiter& limiter)
    {
        // TransmitFile requires SOCKET, not intptr_t
        // platform.cpp exposes a zero-copy helper
        return platform::transmitFile(dataSock, path, offset, result.bytes);
    }
#else
    // ── POSIX zero-copy (sendfile) ────────────────────────────────────────
    static bool sendFilePosix(
        platform::SocketHandle dataSock,
        const std::string& path,
        uint64_t offset,
        TransferResult& result,
        RateLimiter& limiter)
    {
        return platform::transmitFile(dataSock, path, offset, result.bytes);
    }
#endif

    // ── Buffered fallback (64KB chunks) ──────────────────────────────────
    static void sendFileBuffered(
        platform::SocketHandle dataSock,
        const std::string& path,
        uint64_t offset,
        TransferResult& result,
        RateLimiter& limiter)
    {
        FILE* file = fopen(path.c_str(), "rb");
        if (!file) return;

        if (offset > 0) fseek(file, (long)offset, SEEK_SET);

        char buf[CHUNK_SIZE];
        int  n;
        while ((n = (int)fread(buf, 1, sizeof(buf), file)) > 0) {
            if (platform::sendData(dataSock, buf, n) < 0) break;
            result.bytes += n;
            limiter.throttle(n);
        }
        fclose(file);
    }
};

} // namespace ftp
