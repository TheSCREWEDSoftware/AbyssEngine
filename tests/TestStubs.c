// What the readers' tests stand in for: the engine's files, logging, settings and sockets aren't part of them (the
// readers are handed their bytes straight), and a memory stream is all an MpqStream needs to be here.
#include "../src/common/FileManager.h"
#include "../src/common/Logging.h"
#include "../src/common/MpqStream.h"
#include "../src/net/Socket.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct MpqStream {
    uint8_t *data;
    uint32_t size;
    uint32_t position;
};

MpqStream *MpqStream_CreateFromMemory(const char *file_name, void *data, const uint32_t size) {
    (void)file_name;
    MpqStream *stream = calloc(1, sizeof(MpqStream));
    stream->data      = data;
    stream->size      = size;
    return stream;
}

void MpqStream_Destroy(MpqStream **stream) {
    if (stream != NULL && *stream != NULL) {
        free((*stream)->data);
        free(*stream);
        *stream = NULL;
    }
}

uint32_t MpqStream_GetSize(const MpqStream *stream) { return stream != NULL ? stream->size : 0; }

uint32_t MpqStream_Read(MpqStream *stream, void *buffer, const uint32_t offset, const uint32_t size) {
    if (stream == NULL) {
        return 0;
    }
    const uint32_t left = stream->size - stream->position;
    const uint32_t got  = size < left ? size : left;
    memcpy((uint8_t *)buffer + offset, stream->data + stream->position, got);
    stream->position += got;
    return got;
}

bool     MpqStream_GetIsEof(const MpqStream *stream) { return stream->position >= stream->size; }
uint32_t MpqStream_Tell(const MpqStream *stream) { return stream != NULL ? stream->position : 0; }

void MpqStream_Seek(MpqStream *stream, const int64_t position, const int32_t origin) {
    const int64_t base = origin == 1 ? (int64_t)stream->position : (origin == 2 ? (int64_t)stream->size : 0);
    const int64_t at   = base + position;
    stream->position   = (uint32_t)(at < 0 ? 0 : (at > (int64_t)stream->size ? (int64_t)stream->size : at));
}

MpqStream *FileManager_OpenFile(const char *path) {
    (void)path;
    return MpqStream_CreateFromMemory("", calloc(1, 1), 0);
}

bool FileManager_FileExists(const char *path) {
    (void)path;
    return false;
}

enum LogLevel log_level = LOG_LEVEL_WARN;

void Log_Message(const enum LogLevel level, const char *file, const int line, const char *format, ...) {
    (void)level;
    (void)file;
    (void)line;
    (void)format;
}

const char *AbyssConfiguration_GetConfigFolder(void) { return "."; }

int Net_Send(const NetSocket socket, const void *data, const int size) {
    (void)socket;
    (void)data;
    return size;
}
int Net_Receive(const NetSocket socket, void *data, const int size) {
    (void)socket;
    (void)data;
    (void)size;
    return 0;
}
void Net_Close(const NetSocket socket) { (void)socket; }
