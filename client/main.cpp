#include <iostream>
#include <string>
#include <fstream>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

// ─── Global ──────────────────────────────────────────────────────────────────
SOCKET controlSocket = INVALID_SOCKET;

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Send command to server
void sendCommand(const std::string& cmd) {
    std::string msg = cmd + "\r\n";
    send(controlSocket, msg.c_str(), msg.length(), 0);
    std::cout << "--> " << cmd << "\n";
}

// Read one reply line from server
std::string readReply() {
    std::string reply = "";
    char c;
    while (recv(controlSocket, &c, 1, 0) > 0) {
        if (c == '\n') break;
        if (c != '\r') reply += c;
    }
    std::cout << "<-- " << reply << "\n";
    return reply;
}

// Read full reply (handles multi-line)
std::string readFullReply() {
    std::string reply = readReply();
    // Multi-line reply starts with "123-"
    if (reply.length() >= 4 && reply[3] == '-') {
        std::string code = reply.substr(0, 3);
        while (true) {
            std::string line = readReply();
            if (line.substr(0, 3) == code && line[3] == ' ')
                break;
        }
    }
    return reply;
}

// Get reply code as number
int getReplyCode(const std::string& reply) {
    if (reply.length() >= 3)
        return std::stoi(reply.substr(0, 3));
    return 0;
}

// ─── Passive Mode ─────────────────────────────────────────────────────────────

// Send PASV and get data socket
SOCKET openDataConnection() {
    sendCommand("PASV");
    std::string reply = readFullReply();

    if (getReplyCode(reply) != 227) {
        std::cout << "PASV failed\n";
        return INVALID_SOCKET;
    }

    // Parse (h1,h2,h3,h4,p1,p2) from reply
    int start = reply.find('(');
    int end   = reply.find(')');
    std::string addrStr = reply.substr(start + 1, end - start - 1);

    int nums[6] = {0};
    int idx = 0;
    std::string temp = "";
    for (char c : addrStr) {
        if (c == ',') {
            nums[idx++] = std::stoi(temp);
            temp = "";
        } else {
            temp += c;
        }
    }
    nums[idx] = std::stoi(temp);

    std::string ip = std::to_string(nums[0]) + "." +
                     std::to_string(nums[1]) + "." +
                     std::to_string(nums[2]) + "." +
                     std::to_string(nums[3]);
    int port = nums[4] * 256 + nums[5];

    // Connect to data port
    SOCKET dataSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(dataSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "Data connection failed\n";
        closesocket(dataSock);
        return INVALID_SOCKET;
    }

    return dataSock;
}

// ─── Commands ─────────────────────────────────────────────────────────────────

void doList() {
    SOCKET dataSock = openDataConnection();
    if (dataSock == INVALID_SOCKET) return;

    sendCommand("LIST");
    readFullReply(); // 150

    // Read listing
    char buf[4096];
    std::string listing = "";
    int bytes;
    while ((bytes = recv(dataSock, buf, sizeof(buf), 0)) > 0) {
        listing += std::string(buf, bytes);
    }
    closesocket(dataSock);
    readFullReply(); // 226

    std::cout << "\n=== Directory Listing ===\n";
    std::cout << listing;
    std::cout << "========================\n\n";
}

void doDownload(const std::string& filename) {
    SOCKET dataSock = openDataConnection();
    if (dataSock == INVALID_SOCKET) return;

    sendCommand("RETR " + filename);
    std::string reply = readFullReply();

    if (getReplyCode(reply) != 150) {
        closesocket(dataSock);
        return;
    }

    // Save file locally
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cout << "Cannot create local file\n";
        closesocket(dataSock);
        return;
    }

    char buf[4096];
    int bytes;
    int total = 0;
    while ((bytes = recv(dataSock, buf, sizeof(buf), 0)) > 0) {
        file.write(buf, bytes);
        total += bytes;
    }

    file.close();
    closesocket(dataSock);
    readFullReply(); // 226

    std::cout << "Downloaded: " << filename
              << " (" << total << " bytes)\n\n";
}

void doUpload(const std::string& filename) {
    // Open local file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cout << "Local file not found: " << filename << "\n";
        return;
    }

    SOCKET dataSock = openDataConnection();
    if (dataSock == INVALID_SOCKET) return;

    sendCommand("STOR " + filename);
    std::string reply = readFullReply();

    if (getReplyCode(reply) != 150) {
        closesocket(dataSock);
        return;
    }

    // Send file
    char buf[4096];
    int total = 0;
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        int bytes = file.gcount();
        send(dataSock, buf, bytes, 0);
        total += bytes;
    }

    file.close();
    closesocket(dataSock);
    readFullReply(); // 226

    std::cout << "Uploaded: " << filename
              << " (" << total << " bytes)\n\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "================================\n";
    std::cout << "     My FTP Client v1.0\n";
    std::cout << "================================\n\n";

    // Get server address
    std::string host;
    std::cout << "Enter FTP server address: ";
    std::cin >> host;

    // Connect to server
    controlSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(21);
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());

    if (connect(controlSocket,
                (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Cannot connect to " << host << "\n";
        return 1;
    }

    readFullReply(); // 220 welcome

    // Login
    std::string username, password;
    std::cout << "\nUsername: ";
    std::cin >> username;
    sendCommand("USER " + username);
    readFullReply(); // 331

    std::cout << "Password: ";
    std::cin >> password;
    sendCommand("PASS " + password);
    std::string loginReply = readFullReply(); // 230 or 530

    if (getReplyCode(loginReply) != 230) {
        std::cout << "Login failed!\n";
        closesocket(controlSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "\nLogin successful! Type 'help' for commands.\n\n";

    // Command loop
    std::string input;
    while (true) {
        std::cout << "ftp> ";
        std::cin >> input;

        // Convert to lowercase
        for (auto& c : input) c = tolower(c);

        if (input == "bye" || input == "quit" || input == "exit") {
            sendCommand("QUIT");
            readFullReply();
            break;
        }
        else if (input == "ls" || input == "dir") {
            doList();
        }
        else if (input == "get") {
            std::string filename;
            std::cin >> filename;
            doDownload(filename);
        }
        else if (input == "put") {
            std::string filename;
            std::cin >> filename;
            doUpload(filename);
        }
        else if (input == "mkdir") {
            std::string dirname;
            std::cin >> dirname;
            sendCommand("MKD " + dirname);
            readFullReply();
        }
        else if (input == "rmdir") {
            std::string dirname;
            std::cin >> dirname;
            sendCommand("RMD " + dirname);
            readFullReply();
        }
        else if (input == "delete") {
            std::string filename;
            std::cin >> filename;
            sendCommand("DELE " + filename);
            readFullReply();
        }
        else if (input == "rename") {
            std::string oldname, newname;
            std::cin >> oldname >> newname;
            sendCommand("RNFR " + oldname);
            readFullReply();
            sendCommand("RNTO " + newname);
            readFullReply();
        }
        else if (input == "cd") {
            std::string dirname;
            std::cin >> dirname;
            sendCommand("CWD " + dirname);
            readFullReply();
        }
        else if (input == "pwd") {
            sendCommand("PWD");
            readFullReply();
        }
        else if (input == "help") {
            std::cout << "\n=== Available Commands ===\n";
            std::cout << "ls / dir        → List files\n";
            std::cout << "get <file>      → Download file\n";
            std::cout << "put <file>      → Upload file\n";
            std::cout << "mkdir <name>    → Create directory\n";
            std::cout << "rmdir <name>    → Remove directory\n";
            std::cout << "delete <file>   → Delete file\n";
            std::cout << "rename <old> <new> → Rename file\n";
            std::cout << "cd <dir>        → Change directory\n";
            std::cout << "pwd             → Print current directory\n";
            std::cout << "bye             → Disconnect\n";
            std::cout << "==========================\n\n";
        }
        else {
            std::cout << "Unknown command. Type 'help' for list.\n";
        }
    }

    closesocket(controlSocket);
    WSACleanup();
    std::cout << "Disconnected. Goodbye!\n";
    return 0;
} 