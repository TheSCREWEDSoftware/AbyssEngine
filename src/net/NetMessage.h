#ifndef ABYSS_NET_MESSAGE_H
#define ABYSS_NET_MESSAGE_H

#include "Socket.h"
#include <stdbool.h>
#include <stdint.h>

// Messages over a TCP connection: each is [length: 2 bytes][type: 1 byte][contents], little-endian numbers and
// strings as a 2-byte length then the bytes. A connection buffers what's sent until the socket takes it, and hands
// out whole messages as they arrive.

#define NET_MESSAGE_MAX 60000 // contents of one message (a character's save is a few kilobytes)

typedef struct NetWriter {
    uint8_t data[NET_MESSAGE_MAX];
    int     size;
    bool    overflow;
} NetWriter;

void NetWriter_Begin(NetWriter *writer);
void NetWriter_U8(NetWriter *writer, uint8_t value);
void NetWriter_U16(NetWriter *writer, uint16_t value);
void NetWriter_U32(NetWriter *writer, uint32_t value);
void NetWriter_I32(NetWriter *writer, int32_t value);
void NetWriter_F32(NetWriter *writer, float value);
void NetWriter_String(NetWriter *writer, const char *text);

typedef struct NetReader {
    const uint8_t *data;
    int            size;
    int            at;
    bool           failed; // read past the end: everything after reads as zero / ""
} NetReader;

void     NetReader_Begin(NetReader *reader, const uint8_t *data, int size);
uint8_t  NetReader_U8(NetReader *reader);
uint16_t NetReader_U16(NetReader *reader);
uint32_t NetReader_U32(NetReader *reader);
int32_t  NetReader_I32(NetReader *reader);
float    NetReader_F32(NetReader *reader);
void     NetReader_String(NetReader *reader, char *text, int size); // cut to fit
char    *NetReader_StringAlloc(NetReader *reader);                  // the whole string (free it)

typedef struct NetConnection {
    NetSocket socket;
    char      address[NET_ADDRESS_MAX];
    uint8_t  *in; // what's arrived and not handed out yet
    int       in_size;
    uint8_t  *out; // what's waiting to be sent
    int       out_size;
    int       out_capacity;
    bool      closed;
} NetConnection;

NetConnection *NetConnection_Create(NetSocket socket, const char *address);
void           NetConnection_Destroy(NetConnection **connection);
// Queues a message (sent on the next flush). False if the contents don't fit or too much is waiting already (a
// client that stopped reading).
bool NetConnection_Send(NetConnection *connection, uint8_t type, const NetWriter *contents);
void NetConnection_Flush(NetConnection *connection); // sends what the socket takes now
// The next whole message that has arrived: type and contents (valid until the next call). False when there's none.
bool NetConnection_Receive(NetConnection *connection, uint8_t *type, NetReader *contents);

#endif // ABYSS_NET_MESSAGE_H
