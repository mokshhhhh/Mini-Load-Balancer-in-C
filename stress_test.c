#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "8080"
#define DEFAULT_BUFLEN 4096

int main() {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    int total_requests = 5;
    printf("Starting stress test with %d requests...\n", total_requests);

    // Use clock() for simple time measurement
    clock_t start_time = clock();

    for (int i = 0; i < total_requests; i++) {
        SOCKET connect_socket = INVALID_SOCKET;
        struct addrinfo *result = NULL, hints;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        if (getaddrinfo("localhost", DEFAULT_PORT, &hints, &result) != 0) {
            printf("getaddrinfo failed: %d\n", WSAGetLastError());
            continue;
        }

        connect_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (connect_socket == INVALID_SOCKET) {
            printf("socket failed: %ld\n", WSAGetLastError());
            freeaddrinfo(result);
            continue;
        }

        if (connect(connect_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            printf("connect failed for request %d: %d\n", i + 1, WSAGetLastError());
            closesocket(connect_socket);
            freeaddrinfo(result);
            continue;
        }

        freeaddrinfo(result);

        // Send a simple GET request
        const char* get_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(connect_socket, get_request, (int)strlen(get_request), 0);

        // Receive the response
        char recv_buf[DEFAULT_BUFLEN];
        int recv_len = recv(connect_socket, recv_buf, DEFAULT_BUFLEN - 1, 0);
        if (recv_len > 0) {
            recv_buf[recv_len] = '\0';
            printf("Request %d complete.\n", i + 1);
        } else {
            printf("Request %d failed to receive response.\n", i + 1);
        }

        closesocket(connect_socket);
    }

    clock_t end_time = clock();
    double total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    printf("\nTotal time to process all requests: %.2f seconds\n", total_time);
    
    WSACleanup();
    return 0;
}
