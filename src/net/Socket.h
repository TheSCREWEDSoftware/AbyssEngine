#ifndef ABYSS_SOCKET_H
#define ABYSS_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Plain TCP and UDP sockets for Linux, macOS and Windows, always non-blocking. No engine code in here: the lobby
// server (src/lobby) uses it too.

typedef intptr_t NetSocket;
#define NET_INVALID_SOCKET ((NetSocket) - 1)
#define NET_ADDRESS_MAX    64 // "255.255.255.255" or an IPv6 address, as text

bool Net_Init(void); // once, before anything else (Windows needs it); harmless to call again
void Net_Quit(void);

// TCP
NetSocket Net_Listen(uint16_t port); // on every address
// A waiting connection, or NET_INVALID_SOCKET; address gets the other end ("1.2.3.4")
NetSocket Net_Accept(NetSocket listener, char *address, size_t size);
NetSocket Net_Connect(const char *host, uint16_t port);           // starts connecting (finish with Net_ConnectDone)
int       Net_ConnectDone(NetSocket socket);                      // 1 connected, 0 still trying, -1 failed
int       Net_Send(NetSocket socket, const void *data, int size); // bytes sent (0: try later), -1 closed
int       Net_Receive(NetSocket socket, void *data, int size);    // bytes read (0: nothing yet), -1 closed
void      Net_Close(NetSocket socket);

// UDP
NetSocket Net_UdpOpen(uint16_t port, bool broadcast); // port 0: any; broadcast: may send to 255.255.255.255
int       Net_UdpSendTo(NetSocket socket, const char *host, uint16_t port, const void *data, int size);
// A datagram, or 0 when there's none; from / from_port get the sender
int Net_UdpReceive(NetSocket socket, void *data, int size, char *from, size_t from_size, uint16_t *from_port);

// "host", "host:port", "[v6]:port": the host and the port (default_port when it isn't given). False if it's empty
// or the port isn't a number from 1 to 65535.
bool Net_ParseAddress(const char *text, char *host, size_t host_size, uint16_t *port, uint16_t default_port);

#endif // ABYSS_SOCKET_H
