#include <stdint.h>

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8080"
#define MAX_CLIENTS 100
#define MAX_NICKLEN 32
#define MAX_MSG_LEN 65536

typedef struct {
    SOCKET s;
    char nick[MAX_NICKLEN];
    int active;
} client_t;

static client_t clients[MAX_CLIENTS];
static CRITICAL_SECTION clients_cs;
static SOCKET listen_sock = INVALID_SOCKET;
//helper func to ensure entire buffer is sent over socket. 
static int send_all(SOCKET s, const char *buf, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, buf + total, len - total, 0);
        if (sent == SOCKET_ERROR) return SOCKET_ERROR;
        total += sent;
    }
    return total;
}

//sends msg to a specific client
static int send_message_with_prefix(SOCKET s, const char *msg, int msglen) {
    uint32_t netlen = htonl((uint32_t)msglen);
    if (send_all(s, (const char*)&netlen, sizeof(netlen)) == SOCKET_ERROR) return SOCKET_ERROR;
    if (send_all(s, msg, msglen) == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

// A helper function for receiving all expected bytes from a socket, similar to send_all.
static int recv_all(SOCKET s, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int r = recv(s, buf + total, len - total, 0);
        if (r == 0) return 0; // orderly shutdown
        if (r == SOCKET_ERROR) return SOCKET_ERROR;
        total += r;
    }
    return total;
}

//This function searches the clients array for a client with a specific nickname. 
//It's used to check for duplicate nicknames or to find a recipient for a private message.

static client_t* find_by_nick(const char *nick) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active && _stricmp(clients[i].nick, nick) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

static void broadcast_userlist_locked() {
    // caller must hold clients_cs
    char list[8192];
    list[0] = '\0';
    strncat(list, "USERLIST ", sizeof(list) - strlen(list) - 1);

    int first = 1;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active) {
            if (clients[i].nick[0] == '\0') {
                strncpy(clients[i].nick, "anonymous", MAX_NICKLEN - 1);
                clients[i].nick[MAX_NICKLEN - 1] = '\0';
            }
            if (!first) strncat(list, ",", sizeof(list) - strlen(list) - 1);
            strncat(list, clients[i].nick, sizeof(list) - strlen(list) - 1);
            first = 0;
        }
    }

    size_t len = strlen(list);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active) {
            send_message_with_prefix(clients[i].s, list, (int)len);
        }
    }
}

static void broadcast_all(const char *msg, int msglen) {
    EnterCriticalSection(&clients_cs);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].active) {
            send_message_with_prefix(clients[i].s, msg, msglen);
        }
    }
    LeaveCriticalSection(&clients_cs);
}
//responsible for managing client's session
DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET client = (SOCKET)(uintptr_t)arg;

    // register client slot
    int slot = -1;

    EnterCriticalSection(&clients_cs);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].active) {
            clients[i].active = 1;
            clients[i].s = client;
            strncpy(clients[i].nick, "anonymous", MAX_NICKLEN - 1);
            clients[i].nick[MAX_NICKLEN - 1] = '\0';
            slot = i;
            break;
        }
    }
    LeaveCriticalSection(&clients_cs);

    if (slot == -1) {
        const char *err = "ERROR server full\n";
        send_message_with_prefix(client, err, (int)strlen(err));
        closesocket(client);
        return 0;
    }

    const char *welcome = "INFO welcome! set nickname with /nick <name>\n";
    send_message_with_prefix(client, welcome, (int)strlen(welcome));
    EnterCriticalSection(&clients_cs);
    broadcast_userlist_locked();
    LeaveCriticalSection(&clients_cs);

    while (1) {
        uint32_t netlen;
        int r = recv_all(client, (char*)&netlen, sizeof(netlen));
        if (r == 0 || r == SOCKET_ERROR) break;
        uint32_t len = ntohl(netlen);
        if (len == 0 || len > MAX_MSG_LEN) break;
        char *buf = (char*)malloc(len + 1);
        if (!buf) break;
        r = recv_all(client, buf, len);
        if (r == 0 || r == SOCKET_ERROR) { free(buf); break; }
        buf[len] = '\0';

        if (buf[0] == '/') {
            if (_strnicmp(buf, "/nick ", 6) == 0) {
                char *name = buf + 6;
                char *nl = strchr(name, '\n'); if (nl) *nl = '\0';
                if (strlen(name) == 0 || strlen(name) >= MAX_NICKLEN) {
                    const char *err = "ERROR invalid nick\n";
                    send_message_with_prefix(client, err, (int)strlen(err));
                } else {
                    EnterCriticalSection(&clients_cs);
                    if (find_by_nick(name)) {
                        LeaveCriticalSection(&clients_cs);
                        const char *err = "ERROR nick in use\n";
                        send_message_with_prefix(client, err, (int)strlen(err));
                    } else {
                        strncpy(clients[slot].nick, name, MAX_NICKLEN - 1);
                        clients[slot].nick[MAX_NICKLEN - 1] = '\0';
                        LeaveCriticalSection(&clients_cs);
                        char info[128];
                        int n = snprintf(info, sizeof(info), "INFO nick set to %s\n", name);
                        send_message_with_prefix(client, info, n);
                        EnterCriticalSection(&clients_cs);
                        broadcast_userlist_locked();
                        LeaveCriticalSection(&clients_cs);
                    }
                }
            } else if (_strnicmp(buf, "/msg ", 5) == 0) {
                char *p = buf + 5;
                char *space = strchr(p, ' ');
                if (!space) {
                    const char *err = "ERROR usage: /msg <nick> <text>\n";
                    send_message_with_prefix(client, err, (int)strlen(err));
                } else {
                    *space = '\0';
                    char *target = p;
                    char *text = space + 1;
                    char *nl = strchr(text, '\n'); if (nl) *nl = '\0';
                    EnterCriticalSection(&clients_cs);
                    client_t *t = find_by_nick(target);
                    if (!t) {
                        LeaveCriticalSection(&clients_cs);
                        const char *err = "ERROR user not found\n";
                        send_message_with_prefix(client, err, (int)strlen(err));
                    } else {
                        char out[4096];
                        int n = snprintf(out, sizeof(out), "PRIVATE %s %s\n", clients[slot].nick, text);
                        send_message_with_prefix(t->s, out, n);
                        LeaveCriticalSection(&clients_cs);
                        const char *ok = "INFO private sent\n";
                        send_message_with_prefix(client, ok, (int)strlen(ok));
                    }
                }
            } else if (_strnicmp(buf, "/list", 5) == 0) {
                EnterCriticalSection(&clients_cs);
                char list[8192];
                list[0] = '\0';
                strncat(list, "USERLIST ", sizeof(list) - strlen(list) - 1);
                int first = 1;
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (clients[i].active) {
                        if (!first) strncat(list, ",", sizeof(list) - strlen(list) - 1);
                        strncat(list, clients[i].nick, sizeof(list) - strlen(list) - 1);
                        first = 0;
                    }
                }
                LeaveCriticalSection(&clients_cs);
                send_message_with_prefix(client, list, (int)strlen(list));
            } else if (_strnicmp(buf, "/quit", 5) == 0) {
                free(buf);
                break;
            } else if (_strnicmp(buf, "/ping", 5) == 0) {
                const char *pong = "PONG\n";
                send_message_with_prefix(client, pong, 5);
            } else {
                const char *err = "ERROR unknown command\n";
                send_message_with_prefix(client, err, (int)strlen(err));
            }
        } else {
            char out[8192];
            int n = snprintf(out, sizeof(out), " %s : %s\n", clients[slot].nick, buf);
            broadcast_all(out, n);
        }

        free(buf);
    }

    EnterCriticalSection(&clients_cs);
    clients[slot].active = 0;
    clients[slot].s = INVALID_SOCKET;
    clients[slot].nick[0] = '\0';
    broadcast_userlist_locked();
    LeaveCriticalSection(&clients_cs);

    closesocket(client);
    return 0;
}

int main(void) {
    //initialisez winsock , creates socket , listening socket on port 8080 , creates inf loop to accept new client connections
    WSADATA wsa;
    int res = WSAStartup(MAKEWORD(2,2), &wsa);
    if (res != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", res);
        return 1;
    }

    ZeroMemory(clients, sizeof(clients));
    InitializeCriticalSection(&clients_cs);

    struct addrinfo hints, *ai = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &ai) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        WSACleanup();
        return 1;
    }

    listen_sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed\n");
        freeaddrinfo(ai);
        WSACleanup();
        return 1;
    }

    BOOL reuse = TRUE;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    if (bind(listen_sock, ai->ai_addr, (int)ai->ai_addrlen) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed\n");
        closesocket(listen_sock);
        freeaddrinfo(ai);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(ai);

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed\n");
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    printf("Chat server listening on port %s\n", PORT);

    while (1) {
        struct sockaddr_in clientaddr;
        int addrlen = sizeof(clientaddr);
        SOCKET client = accept(listen_sock, (struct sockaddr*)&clientaddr, &addrlen);
        if (client == INVALID_SOCKET) {
            fprintf(stderr, "accept failed\n");
            continue;
        }

        DWORD tid;
        HANDLE h = CreateThread(NULL, 0, client_thread, (LPVOID)(uintptr_t)client, 0, &tid);
        if (h) CloseHandle(h);
        else {
            closesocket(client);
        }
    }

    DeleteCriticalSection(&clients_cs);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
