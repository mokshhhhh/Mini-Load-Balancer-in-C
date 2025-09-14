#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT "8080"
#define DEFAULT_BUFLEN 4096

// Function to handle a client's HTTP request
void handle_request(SOCKET client_socket) {
    char recv_buf[DEFAULT_BUFLEN];
    int recv_len;

    // Receive data from the client
    recv_len = recv(client_socket, recv_buf, DEFAULT_BUFLEN - 1, 0);

    if (recv_len > 0) {
        recv_buf[recv_len] = '\0'; // Null-terminate the received data

        printf("Received request:\n%s\n", recv_buf);

        // --- SIMULATE SLOW PROCESSING ---
        // This makes the server appear busy, showing why a thread pool is needed.
        printf("Simulating a slow task... waiting 2 seconds.\n");
        Sleep(2000); // Sleep for 2000 milliseconds (2 seconds)
        // -------------------------------

        // Simple parsing of the request line to find the requested file path
        char method[16];
        char path[256];
        sscanf(recv_buf, "%s %s", method, path);

        // Handle only GET requests for the root path
        if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
            FILE* f = fopen("index.html", "r");
            if (f) {
                // Get file size
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, 0, SEEK_SET);

                // Allocate buffer to read file content
                char* file_content = (char*)malloc(file_size + 1);
                if (file_content) {
                    fread(file_content, 1, file_size, f);
                    file_content[file_size] = '\0';

                    // Construct and send HTTP 200 OK response
                    char header[DEFAULT_BUFLEN];
                    snprintf(header, DEFAULT_BUFLEN,
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: %ld\r\n"
                             "\r\n", file_size);

                    send(client_socket, header, (int)strlen(header), 0);
                    send(client_socket, file_content, file_size, 0);
                    
                    free(file_content);
                }
                fclose(f);
            } else {
                // File not found, send 404 response
                const char* not_found_header =
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 13\r\n"
                    "\r\n"
                    "404 Not Found";
                send(client_socket, not_found_header, (int)strlen(not_found_header), 0);
            }
        } else {
            // Handle requests for other paths by sending a 404 Not Found response
            const char* not_found_header =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 13\r\n"
                "\r\n"
                "404 Not Found";

            send(client_socket, not_found_header, (int)strlen(not_found_header), 0);
        }
    } else {
        // Handle errors or closed connections
        if (recv_len == 0) {
            printf("Connection closed by client.\n");
        } else {
            printf("recv failed: %d\n", WSAGetLastError());
        }
    }
}

int main() {
    WSADATA wsa_data;
    SOCKET listen_socket = INVALID_SOCKET;
    struct addrinfo *result = NULL, hints;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // Set up address info for the server socket
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0) {
        printf("getaddrinfo failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Create the listening socket
    listen_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        printf("socket failed: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Bind the socket to the port
    if (bind(listen_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    // Start listening for connections
    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    printf("Mini HTTP server listening on port %s...\n", DEFAULT_PORT);
    printf("Open your web browser and go to http://localhost:%s to test it.\n", DEFAULT_PORT);

    // Main loop to accept and handle connections
    while (1) {
        SOCKET client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            printf("accept failed: %d\n", WSAGetLastError());
            continue;
        }

        handle_request(client_socket);

        // Close the client socket after handling the request
        closesocket(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();

    return 0;
}
