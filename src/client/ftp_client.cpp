// ============================================================================
// ftp_client.cpp — FTP client using platform wrappers
// ============================================================================
#include "ftp_client.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace ftp {

FTPClient::~FTPClient() { disconnect(); }

// ─── Connection ──────────────────────────────────────────────────────────────

bool FTPClient::connect(const std::string& host, int port) {
    platform::SocketHandle sock = platform::connectSocket(host, port);
    if (sock == platform::kInvalidSocket) {
        std::cout << "Error: Cannot connect to " << host << ":" << port << "\n";
        return false;
    }
    controlSocket_ = platform::ManagedSocket(sock);
    std::string welcome = readReply(); // 220
    return getReplyCode(welcome) == 220;
}

void FTPClient::disconnect() {
    if (controlSocket_.valid()) {
        sendCommand("QUIT");
        readReply();
        controlSocket_.close();
    }
}

// ─── Auth ────────────────────────────────────────────────────────────────────

bool FTPClient::login(const std::string& user, const std::string& pass) {
    sendCommand("USER " + user);
    if (getReplyCode(readReply()) != 331) return false;
    sendCommand("PASS " + pass);
    return getReplyCode(readReply()) == 230;
}

// ─── Protocol ────────────────────────────────────────────────────────────────

void FTPClient::sendCommand(const std::string& cmd) {
    std::string msg = cmd + "\r\n";
    platform::sendData(controlSocket_.get(), msg.c_str(), (int)msg.size());
    std::cout << "--> " << cmd << "\n";
}

std::string FTPClient::readReply() {
    std::string reply;
    char c;
    // Read byte-by-byte until \n (control channel uses short replies)
    while (platform::recvData(controlSocket_.get(), &c, 1) > 0) {
        if (c == '\n') break;
        if (c != '\r') reply += c;
    }
    std::cout << "<-- " << reply << "\n";

    // Handle multi-line replies: "123-text" ... "123 end"
    if (reply.size() >= 4 && reply[3] == '-') {
        std::string code = reply.substr(0, 3);
        while (true) {
            std::string line;
            while (platform::recvData(controlSocket_.get(), &c, 1) > 0) {
                if (c == '\n') break;
                if (c != '\r') line += c;
            }
            std::cout << "<-- " << line << "\n";
            if (line.size() >= 4 && line.substr(0, 3) == code && line[3] == ' ')
                break;
        }
    }
    return reply;
}

int FTPClient::getReplyCode(const std::string& r) {
    if (r.size() >= 3) try { return std::stoi(r.substr(0, 3)); } catch (...) {}
    return 0;
}

// ─── Passive Data Connection ─────────────────────────────────────────────────

platform::ManagedSocket FTPClient::openDataConnection() {
    sendCommand("PASV");
    std::string reply = readReply();
    if (getReplyCode(reply) != 227) return platform::ManagedSocket();

    auto s = reply.find('('), e = reply.find(')');
    if (s == std::string::npos || e == std::string::npos)
        return platform::ManagedSocket();

    int nums[6] = {0}, idx = 0;
    std::string tmp;
    for (char ch : reply.substr(s + 1, e - s - 1)) {
        if (ch == ',') { nums[idx++] = std::stoi(tmp); tmp.clear(); }
        else           { tmp += ch; }
    }
    nums[idx] = std::stoi(tmp);

    std::string ip = std::to_string(nums[0]) + "." + std::to_string(nums[1]) + "." +
                     std::to_string(nums[2]) + "." + std::to_string(nums[3]);
    int port = nums[4] * 256 + nums[5];

    platform::SocketHandle ds = platform::connectSocket(ip, port);
    if (ds == platform::kInvalidSocket) {
        std::cout << "Error: Data connection failed\n";
        return platform::ManagedSocket();
    }
    return platform::ManagedSocket(ds);
}

// ─── Operations ──────────────────────────────────────────────────────────────

void FTPClient::list() {
    auto ds = openDataConnection();
    if (!ds) return;
    sendCommand("LIST");
    readReply(); // 150

    char buf[4096];
    std::string listing;
    int n;
    while ((n = platform::recvData(ds.get(), buf, sizeof(buf))) > 0)
        listing += std::string(buf, n);
    readReply(); // 226

    std::cout << "\n=== Directory Listing ===\n" << listing << "=========================\n\n";
}

void FTPClient::pwd()                        { sendCommand("PWD");       readReply(); }
void FTPClient::cd(const std::string& d)     { sendCommand("CWD " + d); readReply(); }
void FTPClient::deleteFile(const std::string& f) { sendCommand("DELE "+f); readReply(); }
void FTPClient::mkdir(const std::string& d)  { sendCommand("MKD " + d); readReply(); }
void FTPClient::rmdir(const std::string& d)  { sendCommand("RMD " + d); readReply(); }

void FTPClient::rename(const std::string& o, const std::string& n) {
    sendCommand("RNFR " + o); readReply();
    sendCommand("RNTO " + n); readReply();
}

void FTPClient::download(const std::string& filename) {
    auto ds = openDataConnection();
    if (!ds) return;
    sendCommand("RETR " + filename);
    if (getReplyCode(readReply()) != 150) return;

    std::ofstream file(filename, std::ios::binary);
    if (!file) { std::cout << "Error: Cannot create local file\n"; return; }

    char buf[8192];
    int n;
    uint64_t total = 0;
    while ((n = platform::recvData(ds.get(), buf, sizeof(buf))) > 0) {
        file.write(buf, n);
        total += n;
    }
    file.close();
    readReply(); // 226
    std::cout << "Downloaded: " << filename << " (" << total << " bytes)\n\n";
}

void FTPClient::upload(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) { std::cout << "Error: File not found: " << filename << "\n"; return; }

    auto ds = openDataConnection();
    if (!ds) return;
    sendCommand("STOR " + filename);
    if (getReplyCode(readReply()) != 150) return;

    char buf[8192];
    uint64_t total = 0;
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        int n = (int)file.gcount();
        platform::sendData(ds.get(), buf, n);
        total += n;
    }
    file.close();
    ds.close();
    readReply(); // 226
    std::cout << "Uploaded: " << filename << " (" << total << " bytes)\n\n";
}

void FTPClient::printHelp() {
    std::cout << "\n"
              << "+--------------------------------------+\n"
              << "|      FTP Client v2.0 Commands        |\n"
              << "+--------------------------------------+\n"
              << "|  ls / dir       -> List files        |\n"
              << "|  get <file>     -> Download file     |\n"
              << "|  put <file>     -> Upload file       |\n"
              << "|  mkdir <name>   -> Create directory  |\n"
              << "|  rmdir <name>   -> Remove directory  |\n"
              << "|  delete <file>  -> Delete file       |\n"
              << "|  rename <o> <n> -> Rename file       |\n"
              << "|  cd <dir>       -> Change directory  |\n"
              << "|  pwd            -> Working directory |\n"
              << "|  bye            -> Disconnect        |\n"
              << "+--------------------------------------+\n\n";
}

void FTPClient::commandLoop() {
    printHelp();
    std::string input;
    while (true) {
        std::cout << "ftp> ";
        std::cin >> input;
        std::transform(input.begin(), input.end(), input.begin(), ::tolower);

        if (input == "bye" || input == "quit" || input == "exit") break;
        else if (input == "ls" || input == "dir") list();
        else if (input == "pwd")                  pwd();
        else if (input == "help")                 printHelp();
        else if (input == "get")    { std::string f; std::cin >> f; download(f); }
        else if (input == "put")    { std::string f; std::cin >> f; upload(f); }
        else if (input == "cd")     { std::string d; std::cin >> d; cd(d); }
        else if (input == "mkdir")  { std::string d; std::cin >> d; mkdir(d); }
        else if (input == "rmdir")  { std::string d; std::cin >> d; rmdir(d); }
        else if (input == "delete") { std::string f; std::cin >> f; deleteFile(f); }
        else if (input == "rename") { std::string o, n; std::cin >> o >> n; rename(o, n); }
        else std::cout << "Unknown command. Type 'help'.\n";
    }
}

} // namespace ftp
