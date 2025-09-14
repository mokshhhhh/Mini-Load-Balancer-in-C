#include<windows.h>
#include<winsock2.h>
#include<stdio.h>
#include<stdlib.h>
#include <ws2tcpip.h>


#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 1024
#define MSG_BUFFER_SIZE (BUFFER_SIZE*8) // Accumulation buffer per client 

typedef struct client_thread{ 
    SOCKET client_sock; //socket descriptor for client connection, on Win , SOCKET IS typedef'ed to unsigned int
    struct sockaddr_in client_addr; //stores the client’s IP address and port.
} client_info_t; 

DWORD WINAPI client_thread(LPVOID lpParam) {
    //Now ci points to the actual client info struct we passed from main
    client_info_t *ci = (client_info_t *)lpParam;
    SOCKET client = ci->client_sock;

// DWORD → the return type of the thread function (unsigned long).
// WINAPI → calling convention required by Windows threads.
// LPVOID lpParam → generic pointer (void*) to any data you want to pass into the thread.
// LPVOID = void* in Windows typedefs.

    char recvbuf[BUFFER_SIZE]; //temporary buffer for each recv()
    char msgbuf[MSG_BUFFER_SIZE]; //accumulation buffer for building full msg 
// TCP can deliver partial messages. For example, client sends "hello\nworld\n" but recv might only get "hello\nwo".
// recvbuf stores incoming bytes temporarily.
// msgbuf accumulates bytes until a complete message (terminated by \n) is received.

    int msg_idx = 0;
    int recv_len;

    // Initialize the per-thread message buffer
    msgbuf[0] = '\0';

    // Print client info
    char *client_ip = inet_ntoa(ci->client_addr.sin_addr); //inet_ntoa() converts a struct in_addr (the client’s IP in binary form) into a readable string like "127.0.0.1"

    printf("Client connected: %s:%d (thread %u)\n", client_ip, ntohs(ci->client_addr.sin_port), GetCurrentThreadId());
    //ntohs(ci->client_addr.sin_port) converts the client’s port from network byte order (big-endian) to host byte order (little-endian on most PCs).


    while ((recv_len = recv(client, recvbuf, BUFFER_SIZE, 0)) > 0) {
        //for loop for byte to byte stream
        for (int i = 0; i < recv_len; ++i) {
            // append byte to msg buffer if space
            if (msg_idx < MSG_BUFFER_SIZE - 1) {
                msgbuf[msg_idx++] = recvbuf[i];
                msgbuf[msg_idx] = '\0';
            } else {
                // overflow protection: reset buffer (could be handled differently)
                fprintf(stderr, "Warning: msg buffer overflow, resetting.\n");
                msg_idx = 0;
                msgbuf[0] = '\0';
            }

            // on newline, process full message
            if (recvbuf[i] == '\n') {
                // Remove trailing newline(s) for printing if desired
                int send_len = msg_idx; // include newline when echoing (we echo exactly what client sent)
                // Echo back the full message
                int sent = 0;
                while (sent < send_len) {
                    int s = send(client, msgbuf + sent, send_len - sent, 0);
                    if (s == SOCKET_ERROR) {
                        fprintf(stderr, "Send failed with error: %d\n", WSAGetLastError());
                        break;
                    }
                    sent += s;
                }

                // Print the message on server console (trim newline for nicer output)
                if (msg_idx > 0) {
                    // create a printable copy without trailing newline(s)
                    int printable_len = msg_idx;
                    while (printable_len > 0 && (msgbuf[printable_len-1] == '\n' || msgbuf[printable_len-1] == '\r')) {
                        printable_len--;
                    }
                    char *printable = (char *)malloc(printable_len + 1);
                    if (printable) {
                        memcpy(printable, msgbuf, printable_len);
                        printable[printable_len] = '\0';
                        printf("Client %s:%d -> %s\n", client_ip, ntohs(ci->client_addr.sin_port), printable);
                        free(printable);
                    }
                }

                // reset accumulation buffer for next message
                msg_idx = 0;
                msgbuf[0] = '\0';
            }
        }
    }

    if (recv_len == 0) {
        printf("Client disconnected: %s:%d (thread %u)\n", client_ip, ntohs(ci->client_addr.sin_port), GetCurrentThreadId());
    } else if (recv_len == SOCKET_ERROR) {
        fprintf(stderr, "Recv failed for client %s:%d with error: %d\n", client_ip, ntohs(ci->client_addr.sin_port), WSAGetLastError());
    }

    closesocket(client);
    free(ci); // allocated in accept loop
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET listen_sock = INVALID_SOCKET;

    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "Could not create socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Allow quick reuse of the port (useful during development)
    BOOL opt = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    server_addr.sin_family = AF_INET; //IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; //any port
    server_addr.sin_port = htons(PORT); //network should be in big endian 

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "Listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    printf("Multi-client echo server listening on port %d\n", PORT);

    // Accept loop
    while (1) {
        SOCKET client = accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client == INVALID_SOCKET) {
            fprintf(stderr, "Accept failed: %d\n", WSAGetLastError());
            break;
        }

        // allocate client info to pass to thread
        client_info_t *ci = (client_info_t *)malloc(sizeof(client_info_t));
        if (!ci) {
            fprintf(stderr, "Out of memory allocating client info\n");
            closesocket(client);
            continue;
        }
        ci->client_sock = client;
        ci->client_addr = client_addr;

        // Create thread to handle the client
        HANDLE th = CreateThread(NULL, 0, client_thread, ci, 0, NULL);
        if (th == NULL) {
            fprintf(stderr, "CreateThread failed: %d\n", GetLastError());
            closesocket(client);
            free(ci);
            continue;
        }
        // We don't need the handle; close it so OS can clean resources when thread exits.
        CloseHandle(th);
    }

    // cleanup
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
