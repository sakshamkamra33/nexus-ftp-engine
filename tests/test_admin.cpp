// ============================================================================
// test_admin.cpp — Tests the Phase 6 Admin Server
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>
#include <string>
#include <vector>

void test_admin_commands() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("FAIL: Could not connect to Admin Port 8080. Is the server running?\n");
        return;
    }

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf)-1, 0);
    printf("Server said: %s\n", buf);

    // Send STATS
    const char* cmd1 = "STATS\r\n";
    send(sock, cmd1, strlen(cmd1), 0);
    
    Sleep(100);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf)-1, 0);
    printf("STATS Output:\n%s\n", buf);

    // Send STOP
    const char* cmd2 = "STOP\r\n";
    send(sock, cmd2, strlen(cmd2), 0);
    
    Sleep(100);
    memset(buf, 0, sizeof(buf));
    recv(sock, buf, sizeof(buf)-1, 0);
    printf("STOP Output:\n%s\n", buf);

    closesocket(sock);
    WSACleanup();
    printf("Admin test complete.\n");
}

int main() {
    test_admin_commands();
    return 0;
}
