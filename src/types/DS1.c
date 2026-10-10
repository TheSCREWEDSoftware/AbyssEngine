#include "DS1.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define DS1_MAX_SIZE 1024

typedef struct DS1Reader {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       position;
    bool           failed;
} DS1Reader;

static int32_t DS1__Read(DS1Reader *reader) {
    if (reader->position + 4 > reader->size) {
        reader->failed = true;
        return 0;
    }
    const uint8_t *p  = reader->data + reader->position;
    reader->position += 4;
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static DS1Cell DS1__Cell(const uint32_t value) {
    DS1Cell cell;
    cell.prop1       = (uint8_t)(value & 0xFF);
    cell.sequence    = (uint8_t)((value >> 8) & 0x3F);
    cell.style       = (uint8_t)((value >> 20) & 0x3F);
    cell.orientation = 0;
    cell.hidden      = (value >> 31) != 0;
    return cell;
}

// "\d2\data\global\tiles\ACT1\Town\Floor.tg1" or "C:\D2\DATA\..." -> "data\global\tiles\ACT1\Town\Floor.dt1"
static char *DS1__TilePath(const char *name) {
    const char *start = name;
    if (isalpha((unsigned char)start[0]) && start[1] == ':') {
        start += 2; // "C:\D2\DATA\..."
    }
    while (*start == '\\' || *start == '/') {
        start++;
    }
    if ((start[0] == 'd' || start[0] == 'D') && start[1] == '2' && (start[2] == '\\' || start[2] == '/')) {
        start += 3;
    }
    char *path = strdup(start);
    FAIL_IF_NULL(path);
    for (char *c = path; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\\';
        }
    }
    char *dot = strrchr(path, '.');
    if (dot != NULL && strlen(dot) == 4) {
        strcpy(dot, ".dt1");
    }
    return path;
}

DS1 *DS1_Load(const char *path) {
    if (!FileManager_FileExists(path)) {
        return NULL;
    }

    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    uint8_t       *data   = malloc(size > 0 ? size : 1);
    FAIL_IF_NULL(data);
    MpqStream_Read(stream, data, 0, size);
    MpqStream_Destroy(&stream);

    DS1Reader reader = {data, size, 0, false};
    DS1      *ds1    = calloc(1, sizeof(DS1));
    FAIL_IF_NULL(ds1);

    ds1->version = DS1__Read(&reader);
    ds1->width   = DS1__Read(&reader) + 1;
    ds1->height  = DS1__Read(&reader) + 1;
    if (ds1->version >= 8) {
        ds1->act = DS1__Read(&reader);
        ds1->act = ds1->act < 0 ? 0 : (ds1->act > 4 ? 4 : ds1->act);
    }
    if (ds1->version >= 10) {
        ds1->substitution_type = DS1__Read(&reader);
    }
    if (reader.failed || ds1->width <= 0 || ds1->height <= 0 || ds1->width > DS1_MAX_SIZE ||
        ds1->height > DS1_MAX_SIZE) {
        LOG_WARN("'%s' has an invalid DS1 header.", path);
        free(data);
        DS1_Destroy(&ds1);
        return NULL;
    }

    if (ds1->version >= 3) {
        const int32_t count = DS1__Read(&reader);
        ds1->files          = calloc(count > 0 ? (size_t)count : 1, sizeof(char *));
        FAIL_IF_NULL(ds1->files);
        for (int f = 0; f < count && !reader.failed; f++) {
            const char *name = (const char *)data + reader.position;
            const void *end  = memchr(name, 0, size - reader.position);
            if (end == NULL) {
                reader.failed = true;
                break;
            }
            ds1->files[ds1->file_count++]  = DS1__TilePath(name);
            reader.position               += (uint32_t)strlen(name) + 1;
        }
    }

    if (ds1->version >= 9 && ds1->version <= 13) {
        reader.position += 8;
    }

    ds1->wall_layer_count  = 1;
    ds1->floor_layer_count = 1;
    if (ds1->version >= 4) {
        ds1->wall_layer_count = DS1__Read(&reader);
        if (ds1->version >= 16) {
            ds1->floor_layer_count = DS1__Read(&reader);
        }
    }
    if (ds1->wall_layer_count < 0 || ds1->wall_layer_count > DS1_MAX_WALL_LAYERS || ds1->floor_layer_count < 0 ||
        ds1->floor_layer_count > DS1_MAX_FLOOR_LAYERS) {
        reader.failed = true;
    }

    const size_t cells = (size_t)ds1->width * (size_t)ds1->height;
    for (int w = 0; w < ds1->wall_layer_count; w++) {
        ds1->walls[w] = calloc(cells, sizeof(DS1Cell));
        FAIL_IF_NULL(ds1->walls[w]);
    }
    for (int f = 0; f < ds1->floor_layer_count; f++) {
        ds1->floors[f] = calloc(cells, sizeof(DS1Cell));
        FAIL_IF_NULL(ds1->floors[f]);
    }
    ds1->shadows = calloc(cells, sizeof(DS1Cell));
    FAIL_IF_NULL(ds1->shadows);

    // Old versions stored orientations as indices into this table
    static const uint8_t direction_lookup[] = {0x00, 0x01, 0x02, 0x01, 0x02, 0x03, 0x03, 0x05, 0x05,
                                               0x06, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                               0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x14};

    // Layer order: wall1, orientation1, wall2, orientation2, ..., floors, shadow, (tags)
    for (int w = 0; w < ds1->wall_layer_count && !reader.failed; w++) {
        for (size_t c = 0; c < cells; c++) {
            ds1->walls[w][c] = DS1__Cell((uint32_t)DS1__Read(&reader));
        }
        for (size_t c = 0; c < cells; c++) {
            uint32_t value = (uint32_t)DS1__Read(&reader) & 0xFF;
            if (ds1->version < 7) {
                value = value < sizeof(direction_lookup) ? direction_lookup[value] : 0;
            }
            ds1->walls[w][c].orientation = (uint8_t)value;
        }
    }
    for (int f = 0; f < ds1->floor_layer_count && !reader.failed; f++) {
        for (size_t c = 0; c < cells; c++) {
            ds1->floors[f][c] = DS1__Cell((uint32_t)DS1__Read(&reader));
        }
    }
    for (size_t c = 0; c < cells && !reader.failed; c++) {
        ds1->shadows[c]             = DS1__Cell((uint32_t)DS1__Read(&reader));
        ds1->shadows[c].orientation = 13;
    }
    if (ds1->substitution_type == 1 || ds1->substitution_type == 2) {
        reader.position += (uint32_t)(cells * 4); // substitution tags
    }

    if (ds1->version >= 2 && !reader.failed) {
        const int32_t count = DS1__Read(&reader);
        if (count > 0 && count < 100000) {
            ds1->objects = calloc((size_t)count, sizeof(DS1Object));
            FAIL_IF_NULL(ds1->objects);
            for (int o = 0; o < count; o++) {
                DS1Object object;
                object.type  = DS1__Read(&reader);
                object.id    = DS1__Read(&reader);
                object.x     = DS1__Read(&reader);
                object.y     = DS1__Read(&reader);
                object.flags = ds1->version > 5 ? DS1__Read(&reader) : 0;
                if (reader.failed) {
                    break;
                }
                ds1->objects[ds1->object_count++] = object;
            }
        }
        reader.failed = false; // objects are optional; the tiles are what matters
    }

    free(data);
    if (reader.failed) {
        LOG_WARN("'%s' is truncated.", path);
        DS1_Destroy(&ds1);
        return NULL;
    }
    return ds1;
}

void DS1_Destroy(DS1 **ds1) {
    if (ds1 == NULL || *ds1 == NULL) {
        return;
    }
    for (int f = 0; f < (*ds1)->file_count; f++) {
        free((*ds1)->files[f]);
    }
    free((*ds1)->files);
    for (int w = 0; w < DS1_MAX_WALL_LAYERS; w++) {
        free((*ds1)->walls[w]);
    }
    for (int f = 0; f < DS1_MAX_FLOOR_LAYERS; f++) {
        free((*ds1)->floors[f]);
    }
    free((*ds1)->shadows);
    free((*ds1)->objects);
    free(*ds1);
    *ds1 = NULL;
}
