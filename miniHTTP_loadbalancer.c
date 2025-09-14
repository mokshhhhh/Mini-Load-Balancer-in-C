#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")

#define LB_PORT "8081"
#define DEFAULT_BUFLEN 4096
#define NUM_BACKENDS 3

const char* backend_servers[NUM_BACKENDS] = {
    "localhost:8080",
    "localhost:8082",
    "localhost:8084"
};

int current_server_index = 0;

void forward_request(SOCKET client_socket, const char* backend_address) {
    // Parse backend address to get host and port
    char host[256];
    char port[16];
    sscanf(backend_address, "%[^:]:%s", host, port);

    SOCKET backend_socket = INVALID_SOCKET;
    struct addrinfo *result = NULL, hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &result) != 0) {
        printf("getaddrinfo failed for backend: %d\n", WSAGetLastError());
        return;
    }

    backend_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (backend_socket == INVALID_SOCKET) {
        printf("socket creation failed for backend: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        return;
    }

    if (connect(backend_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("Failed to connect to backend %s: %d\n", backend_address, WSAGetLastError());
        closesocket(backend_socket);
        freeaddrinfo(result);
        return;
    }
    freeaddrinfo(result);

    // Forward the client request to the backend
    char recv_buf[DEFAULT_BUFLEN];
    int recv_len = recv(client_socket, recv_buf, DEFAULT_BUFLEN - 1, 0);

    if (recv_len > 0) {
        recv_buf[recv_len] = '\0';
        printf("Forwarding request to backend %s\n", backend_address);
        send(backend_socket, recv_buf, recv_len, 0);
        
        // Receive response from backend and forward it back to client
        int backend_recv_len;
        do {
            backend_recv_len = recv(backend_socket, recv_buf, DEFAULT_BUFLEN, 0);
            if (backend_recv_len > 0) {
                send(client_socket, recv_buf, backend_recv_len, 0);
            }
        } while (backend_recv_len > 0);
    }
    
    closesocket(backend_socket);
}

int main() {
    WSADATA wsa_data;
    SOCKET listen_socket = INVALID_SOCKET;
    struct addrinfo *result = NULL, hints;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, LB_PORT, &hints, &result) != 0) {
        printf("getaddrinfo failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        printf("socket failed: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    printf("Load Balancer listening on port %s...\n", LB_PORT);
    printf("Backend servers are: %s, %s, %s\n", backend_servers[0], backend_servers[1], backend_servers[2]);
    printf("Open your web browser and go to http://localhost:%s to test.\n", LB_PORT);

    while (1) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            continue;
        }

        // Simple Round Robin logic
        const char* backend = backend_servers[current_server_index];
        current_server_index = (current_server_index + 1) % NUM_BACKENDS;

        forward_request(client_socket, backend);
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}
