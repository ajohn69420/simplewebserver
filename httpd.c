#include "httpd.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <process.h> // For _beginthreadex and _endthreadex
#include <ws2tcpip.h>


#pragma comment(lib, "ws2_32.lib") // Link with ws2_32.lib

#define CONNMAX 1000

char* method,    // "GET" or "POST"
* uri,       // "/index.html" things before '?'
* qs,        // "a=1&b=2"     things after  '?'
* prot;      // "HTTP/1.1"

char* payload;     // for POST
int      payload_size;

static SOCKET listenfd, clients[CONNMAX];
static void error(const char *);
static void startServer(const char *);
static unsigned int __stdcall respond(void *arg);

typedef struct { char *name, *value; } header_t;
static header_t reqhdr[17] = { {"\0", "\0"} };
static SOCKET clientfd;

static char *buf;


void serve_forever(const char *PORT)
{
    SOCKADDR_IN clientaddr;
    int addrlen = sizeof(clientaddr);
    int slot = 0;
    HANDLE threadHandles[CONNMAX];
    DWORD threadIDs[CONNMAX];

    printf("Server started http://127.0.0.1:%s\n", PORT);

    // Setting all elements to INVALID_SOCKET: signifies there is no client connected
    int i;
    for (i = 0; i < CONNMAX; i++)
        clients[i] = INVALID_SOCKET;

    startServer(PORT);

    // ACCEPT connections
    while (1)
    {
        clients[slot] = accept(listenfd, (SOCKADDR *)&clientaddr, &addrlen);
        if (clients[slot] == INVALID_SOCKET)
        {
            printf("accept() error\n");
        }
        else
        {
            // Create a new thread to handle the connection
            threadHandles[slot] = (HANDLE)_beginthreadex(NULL, 0, respond, (void*)clients[slot], 0, (unsigned int*)&threadIDs[slot]);
            if (threadHandles[slot] == NULL)
            {
                printf("Failed to create thread\n");
            }
        }

        while (clients[slot] != INVALID_SOCKET) slot = (slot + 1) % CONNMAX;
    }

    // Cleanup
    for (i = 0; i < CONNMAX; i++)
    {
        if (threadHandles[i] != NULL)
        {
            WaitForSingleObject(threadHandles[i], INFINITE);
            CloseHandle(threadHandles[i]);
        }
    }
    closesocket(listenfd);
    WSACleanup();
}

// Start server
void startServer(const char *port)
{
    WSADATA wsaData;
    struct addrinfo hints, *res, *p;
    int option = 1;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup failed\n");
        exit(1);
    }

    // getaddrinfo for host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res) != 0)
    {
        printf("getaddrinfo() error\n");
        exit(1);
    }
    // socket and bind
    for (p = res; p != NULL; p = p->ai_next)
    {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd == INVALID_SOCKET) continue;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option)) == SOCKET_ERROR) continue;
        if (bind(listenfd, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) continue;
        break;
    }
    if (p == NULL)
    {
        printf("socket() or bind() error\n");
        exit(1);
    }

    freeaddrinfo(res);

    // listen for incoming connections
    if (listen(listenfd, SOMAXCONN) == SOCKET_ERROR)
    {
        printf("listen() error\n");
        exit(1);
    }
}

// get request header
char *request_header(const char* name)
{
    header_t *h = reqhdr;
    while(h->name) {
        if (strcmp(h->name, name) == 0) return h->value;
        h++;
    }
    return NULL;
}

//client connection
unsigned int __stdcall respond(void *arg)
{
    SOCKET clientSocket = (SOCKET)arg;
    int rcvd;
    char *ptr;

    buf = (char *)malloc(65535);
    rcvd = recv(clientSocket, buf, 65535, 0);

    if (rcvd == SOCKET_ERROR)    // receive error
        printf("recv() error\n");
    else if (rcvd == 0)    // receive socket closed
        printf("Client disconnected unexpectedly.\n");
    else    // message received
    {
        buf[rcvd] = '\0';

        method = strtok(buf, " \t\r\n");
        uri    = strtok(NULL, " \t");
        prot   = strtok(NULL, " \t\r\n");

        printf(" + [%s] %s\n", method, uri);

        if (qs = strchr(uri, '?'))
        {
            *qs++ = '\0'; //split URI
        } else {
            qs = uri - 1; //use an empty string
        }

        header_t *h = reqhdr;
        char *t, *t2;
        while (h < reqhdr + 16) {
            char *k, *v;
            k = strtok(NULL, "\r\n: \t");
            if (!k) break;
            v = strtok(NULL, "\r\n");
            while (*v && *v == ' ') v++;
            h->name = k;
            h->value = v;
            h++;
            printf("[H] %s: %s\n", k, v);
            t = v + 1 + strlen(v);
            if (t[1] == '\r' && t[2] == '\n') break;
        }
        t++; // now the *t shall be the beginning of user payload
        t2 = request_header("Content-Length"); // and the related header if there is  
        payload = t;
        payload_size = t2 ? atol(t2) : (rcvd - (t - buf));

        // Call router
        route();

        // Tidy up
        fflush(stdout);
        shutdown(clientSocket, SD_SEND);
        closesocket(clientSocket);
        free(buf);
    }

    // Closing SOCKET
    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);
    return 0;
}
