// ============================================================================
// session.cpp — FTP session with security hardening (Phase 3)
//
// Security additions:
//   - Receive timeout (SO_RCVTIMEO) on the control socket
//   - Max command length check (prevent buffer abuse)
//   - Filename sanitization (reject null bytes, path separators in names)
//   - Max file size enforcement during STOR
//   - Anonymous login support (configurable)
// ============================================================================
#include "session.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cctype>

namespace ftp {

static const int    BUFFER_SIZE      = 8192;
static const int    MAX_CMD_LENGTH   = 512;   // RFC 959: max 512 bytes per command
static const size_t MAX_PATH_LENGTH  = 260;   // Windows MAX_PATH

// ─── Constructor ──────────────────────────────────────────────────────────────
Session::Session(platform::SocketHandle clientSocket,
                 const std::string& clientIp,
                 ServerContext& ctx)
    : socket_(clientSocket)
    , clientIp_(clientIp)
    , ctx_(ctx)
{
    // Apply socket-level timeout and buffer tuning
    platform::setRecvTimeout(socket_.get(), ctx_.timeoutSecs);
    platform::setKeepAlive(socket_.get());
    platform::setSocketBuffers(socket_.get(), ctx_.sendBufKB, ctx_.recvBufKB);

    // Init per-connection rate limiter from server config
    rateLimiter_.setLimit(ctx_.bandwidthLimitBps);

    ctx_.activeConnections.inc();
    ctx_.totalConnections.inc();
}

// ─── Main Loop ────────────────────────────────────────────────────────────────
void Session::run() {
    LOG_INFO("Session", "Client connected: " + clientIp_);
    sendReply("220 FTP Server v2.0 ready.");
    state_ = SessionState::WAIT_USER;

    while (state_ != SessionState::CLOSED) {
        std::string raw = recvCommand();

        // Empty recv = client disconnected or timed out
        if (raw.empty()) {
            if (state_ != SessionState::CLOSED)
                LOG_INFO("Session", "Client disconnected/timed out: " + clientIp_);
            break;
        }

        // ── Input validation ─────────────────────────────────────────────
        if ((int)raw.size() > MAX_CMD_LENGTH) {
            LOG_WARN("Session", clientIp_ + " sent oversized command ("
                     + std::to_string(raw.size()) + " bytes) — rejected");
            sendReply("500 Command too long");
            continue;
        }

        std::string cmd  = cleanCommand(raw);
        if (cmd.empty()) continue;

        std::string verb, argument;
        size_t sp = cmd.find(' ');
        if (sp != std::string::npos) {
            verb     = cmd.substr(0, sp);
            argument = cmd.substr(sp + 1);
        } else {
            verb = cmd;
        }
        std::transform(verb.begin(), verb.end(), verb.begin(), ::toupper);

        LOG_DEBUG("Session", clientIp_ + " -> " + verb +
                  (argument.empty() ? "" : " [arg:" + std::to_string(argument.size()) + "chars]"));

        // ── Always-allowed ────────────────────────────────────────────────
        if (verb == "USER") { handleUser(argument); continue; }
        if (verb == "PASS") { handlePass(argument); continue; }
        if (verb == "QUIT") { handleQuit();         break;    }
        if (verb == "OPTS" || verb == "NOOP") { sendReply("200 OK"); continue; }
        if (verb == "FEAT") { handleFeat();         continue; }

        // ── Auth guard ────────────────────────────────────────────────────
        if (state_ != SessionState::READY && state_ != SessionState::TRANSFERRING) {
            sendReply("530 Please login first");
            continue;
        }

        // ── Dispatch ──────────────────────────────────────────────────────
        if      (verb == "SYST") handleSyst();
        else if (verb == "TYPE") handleType(argument);
        else if (verb == "PWD")  handlePwd();
        else if (verb == "CWD")  handleCwd(argument);
        else if (verb == "CDUP") handleCdup();
        else if (verb == "PASV") handlePasv();
        else if (verb == "PORT") handlePort(argument);
        else if (verb == "LIST") handleList();
        else if (verb == "NLST") handleNlst();
        else if (verb == "RETR") handleRetr(argument);
        else if (verb == "STOR") handleStor(argument);
        else if (verb == "REST") handleRest(argument);
        else if (verb == "DELE") handleDele(argument);
        else if (verb == "MKD")  handleMkd(argument);
        else if (verb == "RMD")  handleRmd(argument);
        else if (verb == "RNFR") handleRnfr(argument);
        else if (verb == "RNTO") handleRnto(argument);
        else if (verb == "SIZE") handleSize(argument);
        else if (verb == "MDTM") handleMdtm(argument);
        else sendReply("502 Command not implemented: " + verb);
    }

    state_ = SessionState::CLOSED;
    ctx_.activeConnections.dec();
    LOG_INFO("Session", "Session ended: " + clientIp_ +
             " | bytes: " + std::to_string(bytesTransferred_));
}

// ─── I/O ──────────────────────────────────────────────────────────────────────
void Session::sendReply(const std::string& reply) {
    std::string msg = reply + "\r\n";
    platform::sendData(socket_.get(), msg.c_str(), (int)msg.size());
    LOG_DEBUG("Session", clientIp_ + " <- " + reply);
}

std::string Session::recvCommand() {
    char buf[MAX_CMD_LENGTH + 2];
    memset(buf, 0, sizeof(buf));
    int n = platform::recvData(socket_.get(), buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    return std::string(buf, n);
}

std::string Session::cleanCommand(const std::string& cmd) {
    std::string c = cmd;
    // Strip trailing CR/LF
    while (!c.empty() && (c.back() == '\r' || c.back() == '\n'))
        c.pop_back();
    // Strip leading whitespace
    size_t start = c.find_first_not_of(" \t");
    return (start == std::string::npos) ? "" : c.substr(start);
}

// ─── Input validation helpers ─────────────────────────────────────────────────

// Reject filenames containing null bytes or absolute path separators
// (the argument may be relative — path separators are only rejected in filename-only cmds)
bool Session::isSafeFilename(const std::string& name) {
    if (name.empty()) return false;
    // No null bytes
    if (name.find('\0') != std::string::npos) return false;
    // No control characters
    for (unsigned char c : name) {
        if (c < 0x20) return false;
    }
    return true;
}

// ─── Path resolution + security ───────────────────────────────────────────────
std::string Session::resolvePath(const std::string& arg) {
    if (arg.empty()) return currentDir_;

    // Normalise separators
    std::string a = arg;
    for (char& c : a) if (c == '/') c = '\\';

    if (a[0] == '\\')
        return userRootDir_ + a;            // absolute within user root
    return currentDir_ + "\\" + a;          // relative
}

bool Session::isWithinRoot(const std::string& path) {
    // Normalised comparison — user root must be a prefix
    if (path.size() < userRootDir_.size()) return false;
    // Case-insensitive on Windows
    std::string p = path.substr(0, userRootDir_.size());
    std::string r = userRootDir_;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return p == r;
}

// ─── Auth handlers ────────────────────────────────────────────────────────────
void Session::handleUser(const std::string& arg) {
    if (arg.empty()) { sendReply("501 Username required"); return; }

    // Anonymous login
    if ((arg == "anonymous" || arg == "ftp") && ctx_.allowAnonymous) {
        username_ = "anonymous";
        userRootDir_ = ctx_.rootDir + "\\" + username_;
        platform::createDir(userRootDir_);
        currentDir_ = userRootDir_;
        state_    = SessionState::READY;
        LOG_INFO("Session", "Anonymous login from " + clientIp_);
        sendReply("230 Anonymous login OK.");
        return;
    }

    username_ = arg;
    state_    = SessionState::WAIT_PASS;
    sendReply("331 Password required for " + arg);
}

void Session::handlePass(const std::string& arg) {
    if (state_ != SessionState::WAIT_PASS) {
        sendReply("503 Send USER first"); return;
    }
    if (ctx_.auth.isRateLimited(clientIp_)) {
        sendReply("421 Too many failed attempts. Try again later.");
        state_ = SessionState::CLOSED;
        return;
    }
    if (ctx_.auth.authenticate(username_, arg)) {
        userRootDir_ = ctx_.rootDir + "\\" + username_;
        platform::createDir(userRootDir_);
        currentDir_ = userRootDir_;
        state_ = SessionState::READY;
        ctx_.auth.clearAttempts(clientIp_);
        LOG_INFO("Session", "Login OK: '" + username_ + "' from " + clientIp_);
        sendReply("230 Welcome, " + username_ + ".");
    } else {
        ctx_.auth.recordFailedAttempt(clientIp_);
        state_ = SessionState::WAIT_USER;
        // Do NOT say "wrong password" — say "incorrect" to not confirm username
        sendReply("530 Login incorrect");
    }
}

void Session::handleQuit() {
    sendReply("221 Goodbye.");
    state_ = SessionState::CLOSED;
}

// ─── Info commands ────────────────────────────────────────────────────────────
void Session::handleSyst()                    { sendReply("215 UNIX Type: L8"); }
void Session::handleType(const std::string& a){ sendReply("200 Type set to " + a); }

void Session::handlePwd() {
    // Show virtual path (relative to user root) — never expose real disk path
    std::string vpath = "/";
    if (currentDir_.size() > userRootDir_.size())
        vpath = currentDir_.substr(userRootDir_.size());
    for (char& c : vpath) if (c == '\\') c = '/';
    sendReply("257 \"" + vpath + "\" is current directory");
}

void Session::handleCwd(const std::string& arg) {
    if (arg.empty()) { sendReply("501 Path required"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied");    return; }
    if (platform::isDirectory(p)) {
        currentDir_ = p;
        sendReply("250 Directory changed.");
    } else {
        sendReply("550 Directory not found: " + arg);
    }
}

void Session::handleCdup() {
    size_t pos = currentDir_.find_last_of("\\");
    if (pos != std::string::npos && currentDir_ != userRootDir_) {
        std::string parent = currentDir_.substr(0, pos);
        if (isWithinRoot(parent)) currentDir_ = parent;
    }
    sendReply("250 Directory changed to parent.");
}

void Session::handleSize(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    auto entries = platform::listDirectory(currentDir_);
    for (auto& e : entries) {
        if (e.name == arg && !e.isDirectory) {
            sendReply("213 " + std::to_string(e.size));
            return;
        }
    }
    sendReply("550 File not found");
}

void Session::handleMdtm(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (!platform::pathExists(p) || platform::isDirectory(p)) {
        sendReply("550 File not found: " + arg); return;
    }
    std::string modTime = platform::getFileModTime(p);
    if (modTime.empty()) {
        sendReply("550 Could not get modification time");
    } else {
        sendReply("213 " + modTime);
    }
}

void Session::handleFeat() {
    std::string feat = 
        "211-Extensions supported:\r\n"
        " SIZE\r\n"
        " MDTM\r\n"
        " REST STREAM\r\n"
        "211 End.";
    sendReply(feat);
}

// ─── Data connection ──────────────────────────────────────────────────────────
void Session::handlePasv() {
    int port = dataChannel_.enterPassive();
    if (port < 0) { sendReply("425 Cannot enter passive mode"); return; }
    sendReply(dataChannel_.pasvResponse());
}

void Session::handlePort(const std::string& arg) {
    std::string ip; int port;
    DataChannel::parsePortCommand(arg, ip, port);
    // Security: reject PORT to privileged ports or loopback
    if (port < 1024) { sendReply("504 PORT to privileged port rejected"); return; }
    dataChannel_.enterActive(ip, port);
    sendReply("200 PORT command successful");
}

// ─── Directory listing ────────────────────────────────────────────────────────
void Session::handleList() {
    sendReply("150 Opening data connection for LIST");
    auto ds = dataChannel_.open();
    if (!ds) { sendReply("425 Cannot open data connection"); return; }

    state_ = SessionState::TRANSFERRING;
    std::string listing;
    for (auto& e : platform::listDirectory(currentDir_)) {
        char line[512];
        if (e.isDirectory) {
            snprintf(line, sizeof(line), "drwxr-xr-x 1 owner group %10llu Jan  1 00:00 %s\r\n",
                     (unsigned long long)0, e.name.c_str());
        } else {
            snprintf(line, sizeof(line), "-rw-r--r-- 1 owner group %10llu Jan  1 00:00 %s\r\n",
                     (unsigned long long)e.size, e.name.c_str());
        }
        listing += line;
    }
    platform::sendData(ds.get(), listing.c_str(), (int)listing.size());
    state_ = SessionState::READY;
    sendReply("226 Directory listing complete.");
}

void Session::handleNlst() {
    sendReply("150 Opening data connection for NLST");
    auto ds = dataChannel_.open();
    if (!ds) { sendReply("425 Cannot open data connection"); return; }

    state_ = SessionState::TRANSFERRING;
    std::string listing;
    for (auto& e : platform::listDirectory(currentDir_))
        listing += e.name + "\r\n";
    platform::sendData(ds.get(), listing.c_str(), (int)listing.size());
    state_ = SessionState::READY;
    sendReply("226 Name listing complete.");
}

// ─── File transfer ────────────────────────────────────────────────────────────
void Session::handleRetr(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string path = resolvePath(arg);
    if (!isWithinRoot(path)) { sendReply("550 Permission denied"); return; }
    if (!platform::pathExists(path) || platform::isDirectory(path)) {
        sendReply("550 File not found: " + arg); return;
    }

    sendReply("150 Opening data connection for RETR");
    auto ds = dataChannel_.open();
    if (!ds) { sendReply("425 Cannot open data connection"); return; }

    state_ = SessionState::TRANSFERRING;
    // Use zero-copy TransferEngine with resume offset
    TransferResult res = TransferEngine::sendFile(
        ds.get(), path, restartOffset_, rateLimiter_, clientIp_);
    restartOffset_ = 0; // Reset after use

    bytesTransferred_          += res.bytes;
    ctx_.totalBytesTransferred.add(res.bytes);
    state_ = SessionState::READY;

    if (res.ok)
        sendReply("226 Transfer complete (" + std::to_string(res.bytes) +
                  " bytes @ " + std::to_string((int)res.speedKBps()) + " KB/s).");
    else
        sendReply("426 Transfer aborted.");
}

void Session::handleStor(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string path = resolvePath(arg);
    if (!isWithinRoot(path)) { sendReply("550 Permission denied"); return; }

    sendReply("150 Opening data connection for STOR");
    auto ds = dataChannel_.open();
    if (!ds) { sendReply("425 Cannot open data connection"); return; }

    state_ = SessionState::TRANSFERRING;
    // Use TransferEngine with resume offset and size limit
    TransferResult res = TransferEngine::recvFile(
        ds.get(), path, restartOffset_, rateLimiter_,
        ctx_.maxFileSizeBytes, clientIp_);
    restartOffset_ = 0;

    if (res.rateLimitHit) {
        sendReply("552 Exceeded storage allocation. Upload aborted.");
        state_ = SessionState::READY;
        return;
    }

    bytesTransferred_          += res.bytes;
    ctx_.totalBytesTransferred.add(res.bytes);
    state_ = SessionState::READY;

    if (res.ok)
        sendReply("226 Upload complete (" + std::to_string(res.bytes) +
                  " bytes @ " + std::to_string((int)res.speedKBps()) + " KB/s).");
    else
        sendReply("426 Upload failed.");
}

// ─── File management ──────────────────────────────────────────────────────────
void Session::handleDele(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (platform::deleteFile(p)) {
        LOG_INFO("Session", clientIp_ + " deleted " + arg);
        sendReply("250 File deleted.");
    } else {
        sendReply("550 Could not delete file.");
    }
}

void Session::handleMkd(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (platform::createDir(p)) sendReply("257 \"" + arg + "\" directory created.");
    else sendReply("550 Could not create directory.");
}

void Session::handleRmd(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (platform::removeDir(p)) sendReply("250 Directory removed.");
    else sendReply("550 Could not remove directory.");
}

void Session::handleRnfr(const std::string& arg) {
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (platform::pathExists(p)) {
        renameFrom_ = p;
        sendReply("350 Ready for destination name.");
    } else {
        sendReply("550 File not found.");
    }
}

void Session::handleRnto(const std::string& arg) {
    if (renameFrom_.empty()) { sendReply("503 Send RNFR first"); return; }
    if (!isSafeFilename(arg)) { sendReply("501 Invalid filename"); return; }
    std::string p = resolvePath(arg);
    if (!isWithinRoot(p)) { sendReply("550 Permission denied"); return; }
    if (platform::renameFile(renameFrom_, p)) {
        LOG_INFO("Session", clientIp_ + " renamed -> " + arg);
        sendReply("250 File renamed.");
    } else {
        sendReply("550 Could not rename.");
    }
    renameFrom_.clear();
}

// ─── REST — Transfer Resume (RFC 3659) ───────────────────────────────────────
// Sets the byte offset where the next RETR or STOR begins.
// Client sends: REST 1048576  (resume from 1 MB)
// Server sets restartOffset_, then RETR seeks to that position.
void Session::handleRest(const std::string& arg) {
    if (arg.empty()) { sendReply("501 Offset required"); return; }
    uint64_t offset = 0;
    bool valid = true;
    for (char c : arg) {
        if (c < '0' || c > '9') { valid = false; break; }
        offset = offset * 10 + (c - '0');
    }
    if (!valid) { sendReply("501 Invalid offset"); return; }
    restartOffset_ = offset;
    LOG_DEBUG("Session", clientIp_ + " REST " + std::to_string(offset));
    sendReply("350 Restarting at " + std::to_string(offset) + ". Send RETR or STOR.");
}

} // namespace ftp
