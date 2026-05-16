#include <iostream>
#include <string>
#include <sstream>

#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

const std::string VALID_USER = "admin";
const std::string VALID_PASS = "1234";
const std::string ROOT_DIR = "C:\\Users\\ASUS\\CODING\\ftp_project\\ftproot";

// ─── Helpers ────────────────────────────────────────────────────────────────

void sendReply(SOCKET s, const std::string& reply) {
    std::string msg = reply + "\r\n";
    send(s, msg.c_str(), msg.length(), 0);
    std::cout << "Sent:     " << reply << "\n";
}

std::string cleanCommand(const std::string& cmd) {
    std::string c = cmd;
    while (!c.empty() && (c.back() == '\r' || c.back() == '\n'))
        c.pop_back();
    return c;
}

// ─── Passive Mode ───────────────────────────────────────────────────────────

SOCKET openPassiveSocket(int& port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;

    bind(sock, (sockaddr*)&addr, sizeof(addr));
    listen(sock, 1);

    int len = sizeof(addr);
    getsockname(sock, (sockaddr*)&addr, &len);
    port = ntohs(addr.sin_port);
    return sock;
}

// ─── Active Mode ────────────────────────────────────────────────────────────

SOCKET connectToClient(const std::string& ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "Active connect failed\n";
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

void parsePort(const std::string& arg, std::string& ip, int& port) {
    int nums[6] = {0};
    int idx = 0;
    std::string temp = "";
    for (char c : arg) {
        if (c == ',') {
            nums[idx++] = std::stoi(temp);
            temp = "";
        } else {
            temp += c;
        }
    }
    nums[idx] = std::stoi(temp);

    ip   = std::to_string(nums[0]) + "." +
           std::to_string(nums[1]) + "." +
           std::to_string(nums[2]) + "." +
           std::to_string(nums[3]);
    port = nums[4] * 256 + nums[5];
}

// ─── Data Connection ────────────────────────────────────────────────────────

SOCKET getDataSocket(SOCKET& passiveSocket,
                     bool usePassive,
                     const std::string& activeIp,
                     int activePort) {
    if (usePassive) {
        return accept(passiveSocket, nullptr, nullptr);
    } else {
        return connectToClient(activeIp, activePort);
    }
}

// ─── File System ────────────────────────────────────────────────────────────

std::string getDirectoryListing(const std::string& path) {
    std::string listing = "";
    std::string searchPath = path + "\\*";

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
        return "Error reading directory\r\n";

    do {
        std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            listing += "<DIR>  " + name + "\r\n";
        else
            listing += "       " + name + "\r\n";

    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return listing;
}

std::string getNameListing(const std::string& path) {
    std::string listing = "";
    std::string searchPath = path + "\\*";

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
        return "";

    do {
        std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;
        listing += name + "\r\n";
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
    return listing;
}

// ─── Session ────────────────────────────────────────────────────────────────

void handleClient(SOCKET clientSocket) {
    sendReply(clientSocket, "220 Welcome to My FTP Server");

    char buffer[4096];
    bool loggedIn           = false;
    std::string currentUser = "";
    std::string currentDir  = ROOT_DIR;
    std::string renameFrom  = "";

    // Data connection state
    SOCKET passiveSocket = INVALID_SOCKET;
    bool   usePassive    = false;
    std::string activeIp = "";
    int    activePort    = 0;

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesReceived <= 0) {
            std::cout << "Client disconnected\n";
            break;
        }

        std::string command = cleanCommand(std::string(buffer));
        std::cout << "Received: " << command << "\n";

        // Split verb and argument
        std::string verb = "", argument = "";
        size_t spacePos = command.find(' ');
        if (spacePos != std::string::npos) {
            verb     = command.substr(0, spacePos);
            argument = command.substr(spacePos + 1);
        } else {
            verb = command;
        }
        for (auto& c : verb) c = toupper(c);

        // ── Commands ──────────────────────────────────────────────────────

        if (verb == "OPTS" || verb == "NOOP") {
            sendReply(clientSocket, "200 OK");
        }
        else if (verb == "USER") {
            currentUser = argument;
            sendReply(clientSocket, "331 Password required for " + argument);
        }
        else if (verb == "PASS") {
            if (currentUser == VALID_USER && argument == VALID_PASS) {
                loggedIn = true;
                sendReply(clientSocket, "230 Login successful");
                std::cout << "User logged in: " << currentUser << "\n";
            } else {
                sendReply(clientSocket, "530 Login incorrect");
            }
        }
        else if (verb == "QUIT") {
            sendReply(clientSocket, "221 Goodbye");
            break;
        }
        else if (!loggedIn) {
            sendReply(clientSocket, "530 Please login first");
        }
        else if (verb == "SYST") {
            sendReply(clientSocket, "215 Windows_NT");
        }
        else if (verb == "TYPE") {
            sendReply(clientSocket, "200 Type set to " + argument);
        }
        else if (verb == "PWD") {
            sendReply(clientSocket,
                "257 \"" + currentDir + "\" is current directory");
        }
        else if (verb == "CWD") {
            std::string newDir = currentDir + "\\" + argument;
            DWORD attr = GetFileAttributes(newDir.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                currentDir = newDir;
                sendReply(clientSocket,
                    "250 Directory changed to " + currentDir);
            } else {
                sendReply(clientSocket, "550 Directory not found");
            }
        }
        else if (verb == "CDUP") {
            size_t pos = currentDir.find_last_of("\\");
            if (pos != std::string::npos && currentDir != ROOT_DIR) {
                currentDir = currentDir.substr(0, pos);
            }
            sendReply(clientSocket,
                "250 Directory changed to " + currentDir);
        }

        // ── Passive Mode ──────────────────────────────────────────────────
        else if (verb == "PASV") {
            if (passiveSocket != INVALID_SOCKET)
                closesocket(passiveSocket);

            int port = 0;
            passiveSocket = openPassiveSocket(port);
            usePassive    = true;

            int p1 = port / 256;
            int p2 = port % 256;
            sendReply(clientSocket,
                "227 Entering Passive Mode (127,0,0,1," +
                std::to_string(p1) + "," + std::to_string(p2) + ")");
        }

        // ── Active Mode ───────────────────────────────────────────────────
        else if (verb == "PORT") {
            parsePort(argument, activeIp, activePort);
            usePassive = false;
            if (passiveSocket != INVALID_SOCKET) {
                closesocket(passiveSocket);
                passiveSocket = INVALID_SOCKET;
            }
            std::cout << "Active mode: " << activeIp
                      << ":" << activePort << "\n";
            sendReply(clientSocket, "200 PORT command successful");
        }

        // ── LIST ──────────────────────────────────────────────────────────
        else if (verb == "LIST") {
            sendReply(clientSocket,
                "150 Opening data connection for LIST");

            SOCKET dataSocket = getDataSocket(
                passiveSocket, usePassive, activeIp, activePort);

            if (dataSocket == INVALID_SOCKET) {
                sendReply(clientSocket, "425 Cannot open data connection");
                continue;
            }

            std::string listing = getDirectoryListing(currentDir);
            send(dataSocket, listing.c_str(), listing.length(), 0);
            closesocket(dataSocket);

            if (passiveSocket != INVALID_SOCKET) {
                closesocket(passiveSocket);
                passiveSocket = INVALID_SOCKET;
            }
            sendReply(clientSocket, "226 Directory listing complete");
        }

        // ── NLST ──────────────────────────────────────────────────────────
        else if (verb == "NLST") {
            sendReply(clientSocket,
                "150 Opening data connection for NLST");

            SOCKET dataSocket = getDataSocket(
                passiveSocket, usePassive, activeIp, activePort);

            if (dataSocket == INVALID_SOCKET) {
                sendReply(clientSocket, "425 Cannot open data connection");
                continue;
            }

            std::string listing = getNameListing(currentDir);
            send(dataSocket, listing.c_str(), listing.length(), 0);
            closesocket(dataSocket);

            if (passiveSocket != INVALID_SOCKET) {
                closesocket(passiveSocket);
                passiveSocket = INVALID_SOCKET;
            }
            sendReply(clientSocket, "226 Name listing complete");
        }

        // ── RETR (Download file) ──────────────────────────────────────────
        else if (verb == "RETR") {
            std::string filePath = currentDir + "\\" + argument;
            std::cout << "File requested: " << filePath << "\n";

            FILE* file = fopen(filePath.c_str(), "rb");
            if (!file) {
                sendReply(clientSocket, "550 File not found");
                continue;
            }

            sendReply(clientSocket,
                "150 Opening data connection for file download");

            SOCKET dataSocket = getDataSocket(
                passiveSocket, usePassive, activeIp, activePort);

            if (dataSocket == INVALID_SOCKET) {
                sendReply(clientSocket, "425 Cannot open data connection");
                fclose(file);
                continue;
            }

            char fileBuffer[4096];
            int bytesRead = 0;
            int totalSent = 0;

            while ((bytesRead =
                fread(fileBuffer, 1, sizeof(fileBuffer), file)) > 0) {
                send(dataSocket, fileBuffer, bytesRead, 0);
                totalSent += bytesRead;
            }

            fclose(file);
            closesocket(dataSocket);

            if (passiveSocket != INVALID_SOCKET) {
                closesocket(passiveSocket);
                passiveSocket = INVALID_SOCKET;
            }

            std::cout << "File sent: " << totalSent << " bytes\n";
            sendReply(clientSocket, "226 File transfer complete");
        }

        // ── STOR (Upload file) ────────────────────────────────────────────
        else if (verb == "STOR") {
            std::string filePath = currentDir + "\\" + argument;
            std::cout << "Receiving file: " << filePath << "\n";

            FILE* file = fopen(filePath.c_str(), "wb");
            if (!file) {
                sendReply(clientSocket, "550 Cannot create file");
                continue;
            }

            sendReply(clientSocket,
                "150 Opening data connection for file upload");

            SOCKET dataSocket = getDataSocket(
                passiveSocket, usePassive, activeIp, activePort);

            if (dataSocket == INVALID_SOCKET) {
                sendReply(clientSocket, "425 Cannot open data connection");
                fclose(file);
                continue;
            }

            char fileBuffer[4096];
            int bytesReceived = 0;
            int totalReceived = 0;

            while ((bytesReceived =
                recv(dataSocket, fileBuffer, sizeof(fileBuffer), 0)) > 0) {
                fwrite(fileBuffer, 1, bytesReceived, file);
                totalReceived += bytesReceived;
            }

            fclose(file);
            closesocket(dataSocket);

            if (passiveSocket != INVALID_SOCKET) {
                closesocket(passiveSocket);
                passiveSocket = INVALID_SOCKET;
            }

            std::cout << "File received: " << totalReceived << " bytes\n";
            sendReply(clientSocket, "226 File upload complete");
        }

        // ── DELE (Delete file) ────────────────────────────────────────────
        else if (verb == "DELE") {
            std::string filePath = currentDir + "\\" + argument;
            if (DeleteFile(filePath.c_str())) {
                std::cout << "Deleted: " << filePath << "\n";
                sendReply(clientSocket, "250 File deleted successfully");
            } else {
                sendReply(clientSocket, "550 Could not delete file");
            }
        }

        // ── MKD (Make directory) ──────────────────────────────────────────
        else if (verb == "MKD") {
            std::string dirPath = currentDir + "\\" + argument;
            if (CreateDirectory(dirPath.c_str(), NULL)) {
                std::cout << "Created directory: " << dirPath << "\n";
                sendReply(clientSocket,
                    "257 \"" + dirPath + "\" directory created");
            } else {
                sendReply(clientSocket, "550 Could not create directory");
            }
        }

        // ── RMD (Remove directory) ────────────────────────────────────────
        else if (verb == "RMD") {
            std::string dirPath = currentDir + "\\" + argument;
            if (RemoveDirectory(dirPath.c_str())) {
                std::cout << "Removed directory: " << dirPath << "\n";
                sendReply(clientSocket, "250 Directory removed successfully");
            } else {
                sendReply(clientSocket, "550 Could not remove directory");
            }
        }

        // ── RNFR (Rename from) ────────────────────────────────────────────
        else if (verb == "RNFR") {
            std::string oldPath = currentDir + "\\" + argument;
            DWORD attr = GetFileAttributes(oldPath.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) {
                sendReply(clientSocket, "550 File not found");
            } else {
                renameFrom = oldPath;
                sendReply(clientSocket, "350 Ready for destination name");
            }
        }

        // ── RNTO (Rename to) ──────────────────────────────────────────────
        else if (verb == "RNTO") {
            if (renameFrom.empty()) {
                sendReply(clientSocket, "503 Send RNFR first");
            } else {
                std::string newPath = currentDir + "\\" + argument;
                if (MoveFile(renameFrom.c_str(), newPath.c_str())) {
                    std::cout << "Renamed: " << renameFrom
                              << " -> " << newPath << "\n";
                    sendReply(clientSocket, "250 File renamed successfully");
                } else {
                    sendReply(clientSocket, "550 Could not rename file");
                }
                renameFrom = "";
            }
        }

        else {
            sendReply(clientSocket, "202 Command not implemented yet");
        }
    }

    if (passiveSocket != INVALID_SOCKET)
        closesocket(passiveSocket);
    closesocket(clientSocket);
}

// Win32 thread function wrapper
DWORD WINAPI clientThreadFunc(LPVOID param) {
    SOCKET clientSocket = (SOCKET)(size_t)param;
    handleClient(clientSocket);
    return 0;
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr;
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(21);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket,   (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    std::cout << "FTP Server running on port 21\n";
    std::cout << "Username: admin | Password: 1234\n";
    std::cout << "Root dir: " << ROOT_DIR << "\n";
    std::cout << "Threading: ON (multiple clients supported)\n\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        std::cout << "New client connected!\n";

// Spawn new thread using Win32 API
HANDLE hThread = CreateThread(
    NULL,
    0,
    clientThreadFunc,
    (LPVOID)(size_t)clientSocket,
    0,
    NULL
);
if (hThread) CloseHandle(hThread);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}