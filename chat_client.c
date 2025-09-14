#include <stdint.h>
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")

static SOCKET sockfd = INVALID_SOCKET;

static int recv_all(SOCKET s, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, buf + total, len - total, 0);
        if (r == 0) return 0;
        if (r == SOCKET_ERROR) return SOCKET_ERROR;
        total += r;
    }
    return total;
}

DWORD WINAPI reader_thread(LPVOID arg) {
    (void)arg;
    while (1) {
        uint32_t netlen;
        int r = recv_all(sockfd, (char*)&netlen, sizeof(netlen));
        if (r == 0 || r == SOCKET_ERROR) {
            printf("Disconnected from server.\n");
            ExitProcess(0);
        }
        uint32_t len = ntohl(netlen);
        if (len == 0) continue;
        char *buf = (char*)malloc(len + 1);
        if (!buf) break;
        r = recv_all(sockfd, buf, len);
        if (r == 0 || r == SOCKET_ERROR) { free(buf); printf("Disconnected\n"); ExitProcess(0); }
        buf[len] = '\0';
        printf("%s", buf);
        free(buf);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s host port\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    const char *port = argv[2];

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    struct addrinfo hints, *res = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        WSACleanup();
        return 1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == INVALID_SOCKET) {
        fprintf(stderr, "socket failed\n");
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    if (connect(sockfd, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        fprintf(stderr, "connect failed\n");
        closesocket(sockfd);
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(res);

    // start reader thread
    DWORD tid;
    HANDLE h = CreateThread(NULL, 0, reader_thread, NULL, 0, &tid);
    if (h) CloseHandle(h);

    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len == 0) continue;
        uint32_t netlen = htonl((uint32_t)len);
        if (send(sockfd, (const char*)&netlen, sizeof(netlen), 0) == SOCKET_ERROR) break;
        if (send(sockfd, line, (int)len, 0) == SOCKET_ERROR) break;
        if (_strnicmp(line, "/quit", 5) == 0) break;
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}
