#include "Socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define NET_WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS || (e) == WSAEALREADY)
#define NET_LAST_ERROR     WSAGetLastError()
#define NET_CLOSE          closesocket
#define NATIVE(s)          ((SOCKET)(s))
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#define NET_WOULD_BLOCK(e) ((e) == EWOULDBLOCK || (e) == EAGAIN || (e) == EINPROGRESS)
#define NET_LAST_ERROR     errno
#define NET_CLOSE          close
#define NATIVE(s)          ((int)(s))
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

static bool started = false;

bool Net_Init(void) {
    if (started) {
        return true;
    }
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
#endif
    started = true;
    return true;
}

void Net_Quit(void) {
    if (!started) {
        return;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    started = false;
}

static void Net__NonBlocking(const NetSocket socket) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)socket, FIONBIO, &on);
#else
    const int flags = fcntl(NATIVE(socket), F_GETFL, 0);
    fcntl(NATIVE(socket), F_SETFL, flags | O_NONBLOCK);
#endif
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(NATIVE(socket), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

static void Net__Option(const NetSocket socket, const int level, const int name) {
    int one = 1;
    setsockopt(NATIVE(socket), level, name, (const char *)&one, sizeof(one));
}

static void Net__AddressText(const struct sockaddr *address, char *text, const size_t size) {
    if (text == NULL || size == 0) {
        return;
    }
    text[0] = '\0';
    if (address->sa_family == AF_INET) {
        inet_ntop(AF_INET, &((const struct sockaddr_in *)address)->sin_addr, text, (socklen_t)size);
    } else if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)address;
        inet_ntop(AF_INET6, &v6->sin6_addr, text, (socklen_t)size);
        // An IPv4 client on a dual-stack socket: "::ffff:1.2.3.4" -> "1.2.3.4"
        if (strncmp(text, "::ffff:", 7) == 0 && strchr(text + 7, '.') != NULL) {
            memmove(text, text + 7, strlen(text + 7) + 1);
        }
    }
}

NetSocket Net_Listen(const uint16_t port) {
    // IPv6 with IPv4 too where we can, otherwise IPv4 only
    NetSocket listener = (NetSocket)socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listener != NET_INVALID_SOCKET) {
        int off = 0;
        setsockopt(NATIVE(listener), IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof(off));
        Net__Option(listener, SOL_SOCKET, SO_REUSEADDR);
        struct sockaddr_in6 address;
        memset(&address, 0, sizeof(address));
        address.sin6_family = AF_INET6;
        address.sin6_port   = htons(port);
        address.sin6_addr   = in6addr_any;
        if (bind(NATIVE(listener), (struct sockaddr *)&address, sizeof(address)) == 0 &&
            listen(NATIVE(listener), 16) == 0) {
            Net__NonBlocking(listener);
            return listener;
        }
        NET_CLOSE(NATIVE(listener));
    }
    listener = (NetSocket)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == NET_INVALID_SOCKET) {
        return NET_INVALID_SOCKET;
    }
    Net__Option(listener, SOL_SOCKET, SO_REUSEADDR);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(NATIVE(listener), (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(NATIVE(listener), 16) != 0) {
        NET_CLOSE(NATIVE(listener));
        return NET_INVALID_SOCKET;
    }
    Net__NonBlocking(listener);
    return listener;
}

NetSocket Net_Accept(const NetSocket listener, char *address, const size_t size) {
    struct sockaddr_storage from;
    socklen_t               length = sizeof(from);
    const NetSocket         client = (NetSocket)accept(NATIVE(listener), (struct sockaddr *)&from, &length);
    if (client == NET_INVALID_SOCKET) {
        return NET_INVALID_SOCKET;
    }
    Net__NonBlocking(client);
    Net__Option(client, IPPROTO_TCP, TCP_NODELAY); // small messages often: don't wait to fill packets
    Net__AddressText((struct sockaddr *)&from, address, size);
    return client;
}

NetSocket Net_Connect(const char *host, const uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char service[8];
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    struct addrinfo *found = NULL;
    if (getaddrinfo(host, service, &hints, &found) != 0 || found == NULL) {
        return NET_INVALID_SOCKET;
    }
    NetSocket result = NET_INVALID_SOCKET;
    for (struct addrinfo *at = found; at != NULL && result == NET_INVALID_SOCKET; at = at->ai_next) {
        const NetSocket attempt = (NetSocket)socket(at->ai_family, at->ai_socktype, at->ai_protocol);
        if (attempt == NET_INVALID_SOCKET) {
            continue;
        }
        Net__NonBlocking(attempt);
        Net__Option(attempt, IPPROTO_TCP, TCP_NODELAY);
        if (connect(NATIVE(attempt), at->ai_addr, (socklen_t)at->ai_addrlen) == 0 || NET_WOULD_BLOCK(NET_LAST_ERROR)) {
            result = attempt;
        } else {
            NET_CLOSE(NATIVE(attempt));
        }
    }
    freeaddrinfo(found);
    return result;
}

int Net_ConnectDone(const NetSocket socket) {
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(NATIVE(socket), &writable);
    FD_SET(NATIVE(socket), &failed);
    struct timeval none = {0, 0};
    if (select(NATIVE(socket) + 1, NULL, &writable, &failed, &none) <= 0) {
        return 0;
    }
    int       error  = 0;
    socklen_t length = sizeof(error);
    getsockopt(NATIVE(socket), SOL_SOCKET, SO_ERROR, (char *)&error, &length);
    return error == 0 && !FD_ISSET(NATIVE(socket), &failed) ? 1 : -1;
}

int Net_Send(const NetSocket socket, const void *data, const int size) {
    const int sent = (int)send(NATIVE(socket), (const char *)data, (size_t)size, MSG_NOSIGNAL);
    if (sent >= 0) {
        return sent;
    }
    return NET_WOULD_BLOCK(NET_LAST_ERROR) ? 0 : -1;
}

int Net_Receive(const NetSocket socket, void *data, const int size) {
    const int got = (int)recv(NATIVE(socket), (char *)data, (size_t)size, 0);
    if (got > 0) {
        return got;
    }
    if (got == 0) {
        return -1; // the other end closed it
    }
    return NET_WOULD_BLOCK(NET_LAST_ERROR) ? 0 : -1;
}

void Net_Close(const NetSocket socket) {
    if (socket != NET_INVALID_SOCKET) {
        NET_CLOSE(NATIVE(socket));
    }
}

NetSocket Net_UdpOpen(const uint16_t port, const bool broadcast) {
    const NetSocket udp = (NetSocket)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == NET_INVALID_SOCKET) {
        return NET_INVALID_SOCKET;
    }
    Net__Option(udp, SOL_SOCKET, SO_REUSEADDR); // several games on one computer can all answer LAN searches
#ifdef SO_REUSEPORT
    Net__Option(udp, SOL_SOCKET, SO_REUSEPORT);
#endif
    if (broadcast) {
        Net__Option(udp, SOL_SOCKET, SO_BROADCAST);
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(NATIVE(udp), (struct sockaddr *)&address, sizeof(address)) != 0) {
        NET_CLOSE(NATIVE(udp));
        return NET_INVALID_SOCKET;
    }
    Net__NonBlocking(udp);
    return udp;
}

int Net_UdpSendTo(const NetSocket socket, const char *host, const uint16_t port, const void *data, const int size) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        return -1;
    }
    return (int)sendto(NATIVE(socket), (const char *)data, (size_t)size, 0, (struct sockaddr *)&address,
                       sizeof(address));
}

int Net_UdpReceive(const NetSocket socket, void *data, const int size, char *from, const size_t from_size,
                   uint16_t *from_port) {
    struct sockaddr_storage sender;
    socklen_t               length = sizeof(sender);
    const int got = (int)recvfrom(NATIVE(socket), (char *)data, (size_t)size, 0, (struct sockaddr *)&sender, &length);
    if (got <= 0) {
        return 0;
    }
    Net__AddressText((struct sockaddr *)&sender, from, from_size);
    if (from_port != NULL) {
        *from_port = sender.ss_family == AF_INET ? ntohs(((struct sockaddr_in *)&sender)->sin_port) : 0;
    }
    return got;
}

bool Net_ParseAddress(const char *text, char *host, const size_t host_size, uint16_t *port,
                      const uint16_t default_port) {
    while (*text == ' ') {
        text++;
    }
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", text);
    for (char *end = copy + strlen(copy); end > copy && end[-1] == ' '; end--) {
        end[-1] = '\0';
    }
    if (copy[0] == '\0') {
        return false;
    }
    const char *port_text = NULL;
    if (copy[0] == '[') { // [IPv6]:port
        char *close = strchr(copy, ']');
        if (close == NULL) {
            return false;
        }
        *close = '\0';
        snprintf(host, host_size, "%s", copy + 1);
        port_text = close[1] == ':' ? close + 2 : NULL;
    } else {
        char *colon = strchr(copy, ':');
        if (colon != NULL && strchr(colon + 1, ':') == NULL) { // one colon: host:port (more: a bare IPv6 address)
            *colon    = '\0';
            port_text = colon + 1;
        }
        snprintf(host, host_size, "%s", copy);
    }
    if (host[0] == '\0') {
        return false;
    }
    *port = default_port;
    if (port_text != NULL && port_text[0] != '\0') {
        char      *end   = NULL;
        const long value = strtol(port_text, &end, 10);
        if (*end != '\0' || value < 1 || value > 65535) {
            return false;
        }
        *port = (uint16_t)value;
    }
    return true;
}
