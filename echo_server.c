// echo_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib") // Link with Winsock library

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    WSADATA wsa; //struct to hold info abt window's socket implementation of berkley socket used in linux

    SOCKET server_fd, client_fd; //special type defined in Winsock
    //server_fd ->listening socket, created by socket(), then bound with bind(), then set to listen with listen()
    //each client gets own client_fd

    struct sockaddr_in server_addr, client_addr;
    //socket adress internet, defining IPv4 addr & port
    //server_addr contains ip port, client_addr : filled by accept() ; tells which client connected(IP+port)
    int client_len, recv_size;

    //contains actual size of client address struct
    // contains bytes of msg received from client in this read
    
    char buffer[BUFFER_SIZE]; //temp storage for incoming data
    char msg_buffer[BUFFER_SIZE*2]; // accumulator for full messages
    msg_buffer[0] = '\0';           // start empty

    // Step 1: Initialize Winsock
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { //using winsock ver2.2
        printf("Failed. Error Code : %d\n", WSAGetLastError());
        return 1;
    }
    printf("Winsock initialized.\n");

    // Step 2: Create socket
    //AF_INET : Address family IPv4
    //sock stream = TCP , 0 picks default, TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Could not create socket : %d\n", WSAGetLastError());
        return 1;
    }
    printf("Socket created.\n");

    // Step 3: Bind socket to IP/Port
    //sin_family -> describes and IPv4 socket address
    //sin_addr - holds IP address
    //s_addr - actual 32 bits value of IP
    //INADOR_ANY->listen anywhere, localhost, wifi-IP, ethernet IP
    server_addr.sin_family = AF_INET; 
    server_addr.sin_addr.s_addr = INADDR_ANY; // listen on any interface
    server_addr.sin_port = htons(PORT);
    //sin_port 16 bits int for TCP port no
    //htons() = host to network short, host byte order to network byte order 
    //network byte order is always big endian(msb first)


    //register the socket at IP : Port With OS
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed with error code : %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Bind done.\n");

    // Step 4: Listen for incoming connections
    if (listen(server_fd, 3) == SOCKET_ERROR) {
        printf("Listen failed with error code : %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Waiting for incoming connections...\n");

    client_len = sizeof(struct sockaddr_in);

    // Step 5: Accept incoming connection
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd == INVALID_SOCKET) {
        printf("Accept failed with error code : %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Connection accepted.\n");

    // Step 6: Communicate
    while ((recv_size = recv(client_fd, buffer, BUFFER_SIZE-1, 0)) > 0) {
        buffer[recv_size] = '\0'; // null terminate

        // Append new data to msg_buffer
        strcat(msg_buffer, buffer);

        // Process full messages
        char *newline_pos;
        while ((newline_pos = strchr(msg_buffer, '\n')) != NULL) {
            *newline_pos = '\0'; // cut at newline
            printf("Client (full msg): %s\n", msg_buffer);

            // Echo back + newline
            strcat(msg_buffer, "\n");
            send(client_fd, msg_buffer, strlen(msg_buffer), 0);

            // Shift leftover (after newline) to front
            strcpy(msg_buffer, newline_pos + 1);
        }

//Client sends → hello\nworld\n
// First recv() → "hello\nwo"
// msg_buffer = "hello\nwo"
// Finds newline → extracts "hello" → echoes back.
// Leftover "wo" shifted to start.
// Next recv() → "rld\n"
// msg_buffer = "world\n" (because "wo" + "rld\n")
// Finds newline → extracts "world" → echoes back.
// Leftover empty → reset to "".

    }

    if (recv_size == SOCKET_ERROR) {
        printf("Recv failed with error code : %d\n", WSAGetLastError());
    }

    // Step 7: Cleanup
    closesocket(client_fd);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}
