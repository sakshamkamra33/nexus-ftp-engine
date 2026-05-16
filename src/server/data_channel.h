// ============================================================================
// data_channel.h — FTP data connection management (active + passive modes)
// All socket calls go through platform wrappers — no OS headers needed here.
// ============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/logger.h"
#include <string>
#include <sstream>

namespace ftp {

class DataChannel {
public:
    // Enter passive mode: server opens a listener, returns the port number
    int enterPassive() {
        closePassive();

        platform::SocketHandle sock = platform::createTcpSocket();
        if (sock == platform::kInvalidSocket) return -1;

        platform::setReuseAddr(sock);

        if (platform::bindSocket(sock, 0)  < 0 ||
            platform::listenSocket(sock, 1) < 0) {
            platform::closeSocket(sock);
            return -1;
        }

        passivePort_   = platform::getLocalPort(sock);
        passiveSocket_ = platform::ManagedSocket(sock);
        usePassive_    = true;

        LOG_DEBUG("DataChannel", "Passive mode on port " + std::to_string(passivePort_));
        return passivePort_;
    }

    // Enter active mode: client specifies where to connect for data
    void enterActive(const std::string& ip, int port) {
        closePassive();
        activeIp_   = ip;
        activePort_ = port;
        usePassive_ = false;
        LOG_DEBUG("DataChannel", "Active mode -> " + ip + ":" + std::to_string(port));
    }

    // Open the actual data connection (accept or connect depending on mode)
    platform::ManagedSocket open() {
        if (usePassive_) {
            platform::SocketHandle ds = platform::acceptConnection(passiveSocket_.get());
            closePassive();
            return platform::ManagedSocket(ds);
        } else {
            platform::SocketHandle ds = platform::connectSocket(activeIp_, activePort_);
            if (ds == platform::kInvalidSocket) {
                LOG_ERROR("DataChannel", "Active connect failed to " + activeIp_);
                return platform::ManagedSocket();
            }
            return platform::ManagedSocket(ds);
        }
    }

    // Parse PORT command: h1,h2,h3,h4,p1,p2
    static void parsePortCommand(const std::string& arg,
                                 std::string& ip, int& port) {
        int nums[6] = {0};
        int idx = 0;
        std::string temp;
        for (char c : arg) {
            if (c == ',') { nums[idx++] = std::stoi(temp); temp.clear(); }
            else          { temp += c; }
        }
        nums[idx] = std::stoi(temp);
        ip   = std::to_string(nums[0]) + "." + std::to_string(nums[1]) + "." +
               std::to_string(nums[2]) + "." + std::to_string(nums[3]);
        port = nums[4] * 256 + nums[5];
    }

    // Build the PASV response string
    std::string pasvResponse() const {
        return "227 Entering Passive Mode (127,0,0,1," +
               std::to_string(passivePort_ / 256) + "," +
               std::to_string(passivePort_ % 256) + ")";
    }

    bool isPassive() const { return usePassive_; }

private:
    void closePassive() { passiveSocket_.close(); }

    platform::ManagedSocket passiveSocket_;
    bool        usePassive_  = false;
    int         passivePort_ = 0;
    std::string activeIp_;
    int         activePort_  = 0;
};

// Remove the alias — not needed

} // namespace ftp
