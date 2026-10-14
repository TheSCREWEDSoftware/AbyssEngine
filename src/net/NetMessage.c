#include "NetMessage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_CAPACITY (NET_MESSAGE_MAX + 3)
#define OUT_MAXIMUM (4 * 1024 * 1024) // queued for one connection before it counts as stuck
#define HEADER_SIZE 3

void NetWriter_Begin(NetWriter *writer) {
    writer->size     = 0;
    writer->overflow = false;
}

static void NetWriter__Bytes(NetWriter *writer, const void *bytes, const int count) {
    if (writer->size + count > NET_MESSAGE_MAX) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->data + writer->size, bytes, (size_t)count);
    writer->size += count;
}

void NetWriter_U8(NetWriter *writer, const uint8_t value) { NetWriter__Bytes(writer, &value, 1); }

void NetWriter_U16(NetWriter *writer, const uint16_t value) {
    const uint8_t bytes[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    NetWriter__Bytes(writer, bytes, 2);
}

void NetWriter_U32(NetWriter *writer, const uint32_t value) {
    const uint8_t bytes[4] = {(uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF), (uint8_t)((value >> 16) & 0xFF),
                              (uint8_t)(value >> 24)};
    NetWriter__Bytes(writer, bytes, 4);
}

void NetWriter_I32(NetWriter *writer, const int32_t value) { NetWriter_U32(writer, (uint32_t)value); }

void NetWriter_F32(NetWriter *writer, const float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    NetWriter_U32(writer, bits);
}

void NetWriter_String(NetWriter *writer, const char *text) {
    const size_t length = text != NULL ? strlen(text) : 0;
    const int    count  = length > 0xFFFF ? 0xFFFF : (int)length;
    NetWriter_U16(writer, (uint16_t)count);
    NetWriter__Bytes(writer, text, count);
}

void NetReader_Begin(NetReader *reader, const uint8_t *data, const int size) {
    reader->data   = data;
    reader->size   = size;
    reader->at     = 0;
    reader->failed = false;
}

static bool NetReader__Has(NetReader *reader, const int count) {
    if (reader->failed || reader->at + count > reader->size) {
        reader->failed = true;
        return false;
    }
    return true;
}

uint8_t NetReader_U8(NetReader *reader) { return NetReader__Has(reader, 1) ? reader->data[reader->at++] : 0; }

uint16_t NetReader_U16(NetReader *reader) {
    if (!NetReader__Has(reader, 2)) {
        return 0;
    }
    const uint8_t *b  = reader->data + reader->at;
    reader->at       += 2;
    return (uint16_t)(b[0] | (b[1] << 8));
}

uint32_t NetReader_U32(NetReader *reader) {
    if (!NetReader__Has(reader, 4)) {
        return 0;
    }
    const uint8_t *b  = reader->data + reader->at;
    reader->at       += 4;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int32_t NetReader_I32(NetReader *reader) { return (int32_t)NetReader_U32(reader); }

float NetReader_F32(NetReader *reader) {
    const uint32_t bits = NetReader_U32(reader);
    float          value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void NetReader_String(NetReader *reader, char *text, const int size) {
    const int length = NetReader_U16(reader);
    if (size > 0) {
        text[0] = '\0';
    }
    if (!NetReader__Has(reader, length)) {
        return;
    }
    if (size > 0) {
        const int kept = length < size - 1 ? length : size - 1;
        memcpy(text, reader->data + reader->at, (size_t)kept);
        text[kept] = '\0';
    }
    reader->at += length;
}

char *NetReader_StringAlloc(NetReader *reader) {
    const int length = NetReader_U16(reader);
    char     *text   = calloc((size_t)length + 1, 1);
    if (text == NULL) {
        return NULL;
    }
    if (NetReader__Has(reader, length)) {
        memcpy(text, reader->data + reader->at, (size_t)length);
        reader->at += length;
    }
    return text;
}

NetConnection *NetConnection_Create(const NetSocket socket, const char *address) {
    NetConnection *connection = calloc(1, sizeof(NetConnection));
    if (connection == NULL) {
        return NULL;
    }
    connection->in = malloc(IN_CAPACITY);
    if (connection->in == NULL) {
        free(connection);
        return NULL;
    }
    connection->socket = socket;
    snprintf(connection->address, sizeof(connection->address), "%s", address != NULL ? address : "");
    return connection;
}

void NetConnection_Destroy(NetConnection **connection) {
    if (connection == NULL || *connection == NULL) {
        return;
    }
    Net_Close((*connection)->socket);
    free((*connection)->in);
    free((*connection)->out);
    free(*connection);
    *connection = NULL;
}

bool NetConnection_Send(NetConnection *connection, const uint8_t type, const NetWriter *contents) {
    if (connection->closed || contents->overflow) {
        return false;
    }
    const int total = HEADER_SIZE + contents->size;
    if (connection->out_size + total > OUT_MAXIMUM) {
        connection->closed = true; // it stopped reading: give up on it
        return false;
    }
    if (connection->out_size + total > connection->out_capacity) {
        int capacity = connection->out_capacity > 0 ? connection->out_capacity : 4096;
        while (capacity < connection->out_size + total) {
            capacity *= 2;
        }
        uint8_t *grown = realloc(connection->out, (size_t)capacity);
        if (grown == NULL) {
            connection->closed = true;
            return false;
        }
        connection->out          = grown;
        connection->out_capacity = capacity;
    }
    uint8_t *at = connection->out + connection->out_size;
    at[0]       = (uint8_t)((contents->size + 1) & 0xFF); // the length counts the type byte
    at[1]       = (uint8_t)((contents->size + 1) >> 8);
    at[2]       = type;
    memcpy(at + HEADER_SIZE, contents->data, (size_t)contents->size);
    connection->out_size += total;
    return true;
}

void NetConnection_Flush(NetConnection *connection) {
    while (!connection->closed && connection->out_size > 0) {
        const int sent = Net_Send(connection->socket, connection->out, connection->out_size);
        if (sent < 0) {
            connection->closed = true;
            return;
        }
        if (sent == 0) {
            return; // the socket's full: the rest next time
        }
        memmove(connection->out, connection->out + sent, (size_t)(connection->out_size - sent));
        connection->out_size -= sent;
    }
}

bool NetConnection_Receive(NetConnection *connection, uint8_t *type, NetReader *contents) {
    static uint8_t message[NET_MESSAGE_MAX + 1]; // handed out until the next call
    if (!connection->closed) {
        // Read what's there, up to the buffer's room
        while (connection->in_size < IN_CAPACITY) {
            const int got = Net_Receive(connection->socket, connection->in + connection->in_size,
                                        IN_CAPACITY - connection->in_size);
            if (got < 0) {
                connection->closed = true;
                break;
            }
            if (got == 0) {
                break;
            }
            connection->in_size += got;
        }
    }
    if (connection->in_size < 2) {
        return false;
    }
    const int length = connection->in[0] | (connection->in[1] << 8);
    if (length < 1 || length > NET_MESSAGE_MAX + 1) {
        connection->closed = true; // garbage: not one of ours
        return false;
    }
    if (connection->in_size < 2 + length) {
        return false;
    }
    *type = connection->in[2];
    memcpy(message, connection->in + HEADER_SIZE, (size_t)(length - 1));
    memmove(connection->in, connection->in + 2 + length, (size_t)(connection->in_size - 2 - length));
    connection->in_size -= 2 + length;
    NetReader_Begin(contents, message, length - 1);
    return true;
}
