#include "DT1.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <stdlib.h>
#include <string.h>

#define DT1_HEADER_SIZE       276
#define DT1_TILE_HEADER_SIZE  96
#define DT1_BLOCK_HEADER_SIZE 20
#define DT1_BLOCK_WIDTH       32
#define DT1_BLOCK_HEIGHT      32
#define DT1_ISO_BLOCK_HEIGHT  15

struct DT1 {
    int      tile_count;
    DT1Tile *tiles;
};

typedef struct DT1Block {
    int16_t x;
    int16_t y;
    int16_t format; // 1 = isometric floor block, otherwise RLE
    int32_t length;
    int32_t offset; // from the tile's block headers
} DT1Block;

static int32_t DT1__I32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static int16_t DT1__I16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static void DT1__Plot(DT1Tile *tile, const int x, const int y, const uint8_t value) {
    const int ix = x - tile->image_x;
    const int iy = y - tile->image_y;
    if (ix >= 0 && iy >= 0 && ix < tile->image_width && iy < tile->image_height) {
        tile->image[(iy * tile->image_width) + ix] = value;
    }
}

// Floor blocks are 32x15 diamonds stored row by row.
static void DT1__DecodeIsometric(DT1Tile *tile, const DT1Block *block, const uint8_t *data, int32_t length) {
    static const int x_jump[DT1_ISO_BLOCK_HEIGHT] = {14, 12, 10, 8, 6, 4, 2, 0, 2, 4, 6, 8, 10, 12, 14};
    static const int pixels[DT1_ISO_BLOCK_HEIGHT] = {4, 8, 12, 16, 20, 24, 28, 32, 28, 24, 20, 16, 12, 8, 4};

    int i = 0;
    for (int y = 0; y < DT1_ISO_BLOCK_HEIGHT; y++) {
        for (int n = 0; n < pixels[y]; n++) {
            if (i >= length) {
                return;
            }
            DT1__Plot(tile, block->x + x_jump[y] + n, block->y + y, data[i++]);
        }
    }
}

// Wall blocks: pairs of (skip, count) followed by count pixels; (0, 0) ends a row.
static void DT1__DecodeRle(DT1Tile *tile, const DT1Block *block, const uint8_t *data, int32_t length) {
    int x = 0;
    int y = 0;
    int i = 0;
    while (i + 1 < length) {
        const int skip   = data[i];
        const int count  = data[i + 1];
        i               += 2;
        if (skip == 0 && count == 0) {
            x = 0;
            y++;
            continue;
        }
        x += skip;
        for (int n = 0; n < count && i < length; n++) {
            DT1__Plot(tile, block->x + x, block->y + y, data[i++]);
            x++;
        }
    }
}

static void DT1__DecodeTile(DT1Tile *tile, const uint8_t *file, const uint32_t size, const int32_t block_headers,
                            const int32_t block_count) {
    if (block_count <= 0 || block_headers < 0 ||
        (uint32_t)block_headers + ((uint32_t)block_count * DT1_BLOCK_HEADER_SIZE) > size) {
        return;
    }

    DT1Block *blocks = calloc((size_t)block_count, sizeof(DT1Block));
    FAIL_IF_NULL(blocks);

    int min_x = 0x7FFFFFFF;
    int min_y = 0x7FFFFFFF;
    int max_x = -0x7FFFFFFF;
    int max_y = -0x7FFFFFFF;
    for (int b = 0; b < block_count; b++) {
        const uint8_t *h = file + block_headers + (b * DT1_BLOCK_HEADER_SIZE);
        blocks[b].x      = DT1__I16(h + 0);
        blocks[b].y      = DT1__I16(h + 2);
        blocks[b].format = DT1__I16(h + 8);
        blocks[b].length = DT1__I32(h + 10);
        blocks[b].offset = DT1__I32(h + 16);

        const int height = blocks[b].format == 1 ? DT1_ISO_BLOCK_HEIGHT : DT1_BLOCK_HEIGHT;
        if (blocks[b].x < min_x) {
            min_x = blocks[b].x;
        }
        if (blocks[b].y < min_y) {
            min_y = blocks[b].y;
        }
        if (blocks[b].x + DT1_BLOCK_WIDTH > max_x) {
            max_x = blocks[b].x + DT1_BLOCK_WIDTH;
        }
        if (blocks[b].y + height > max_y) {
            max_y = blocks[b].y + height;
        }
    }

    tile->image_x      = min_x;
    tile->image_y      = min_y;
    tile->image_width  = max_x - min_x;
    tile->image_height = max_y - min_y;
    tile->image        = calloc((size_t)tile->image_width * (size_t)tile->image_height, 1);
    FAIL_IF_NULL(tile->image);

    for (int b = 0; b < block_count; b++) {
        const int64_t start = (int64_t)block_headers + blocks[b].offset;
        if (start < 0 || blocks[b].length <= 0 || start + blocks[b].length > (int64_t)size) {
            continue;
        }
        if (blocks[b].format == 1) {
            DT1__DecodeIsometric(tile, &blocks[b], file + start, blocks[b].length);
        } else {
            DT1__DecodeRle(tile, &blocks[b], file + start, blocks[b].length);
        }
    }

    free(blocks);
}

DT1 *DT1_Load(const char *path) {
    if (!FileManager_FileExists(path)) {
        return NULL;
    }

    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    uint8_t       *data   = malloc(size > 0 ? size : 1);
    FAIL_IF_NULL(data);
    MpqStream_Read(stream, data, 0, size);
    MpqStream_Destroy(&stream);

    if (size < DT1_HEADER_SIZE || DT1__I32(data) != 7 || DT1__I32(data + 4) != 6) {
        LOG_WARN("'%s' is not a DT1 file.", path);
        free(data);
        return NULL;
    }

    const int32_t count   = DT1__I32(data + 268);
    const int32_t headers = DT1__I32(data + 272);
    if (count < 0 || headers < 0 || (uint64_t)headers + ((uint64_t)count * DT1_TILE_HEADER_SIZE) > size) {
        LOG_WARN("'%s' has an invalid DT1 header.", path);
        free(data);
        return NULL;
    }

    DT1 *dt1 = malloc(sizeof(DT1));
    FAIL_IF_NULL(dt1);
    dt1->tile_count = count;
    dt1->tiles      = calloc(count > 0 ? (size_t)count : 1, sizeof(DT1Tile));
    FAIL_IF_NULL(dt1->tiles);

    for (int t = 0; t < count; t++) {
        const uint8_t *h    = data + headers + (t * DT1_TILE_HEADER_SIZE);
        DT1Tile       *tile = &dt1->tiles[t];
        tile->direction     = DT1__I32(h + 0);
        tile->roof_height   = DT1__I16(h + 4);
        tile->material      = (uint16_t)DT1__I16(h + 6);
        tile->height        = DT1__I32(h + 8);
        tile->width         = DT1__I32(h + 12);
        tile->orientation   = DT1__I32(h + 20);
        tile->main_index    = DT1__I32(h + 24);
        tile->sub_index     = DT1__I32(h + 28);
        tile->rarity        = DT1__I32(h + 32);
        memcpy(tile->subtile_flags, h + 40, sizeof(tile->subtile_flags));
        DT1__DecodeTile(tile, data, size, DT1__I32(h + 72), DT1__I32(h + 80));
    }

    free(data);
    return dt1;
}

void DT1_Destroy(DT1 **dt1) {
    if (dt1 == NULL || *dt1 == NULL) {
        return;
    }
    for (int t = 0; t < (*dt1)->tile_count; t++) {
        free((*dt1)->tiles[t].image);
    }
    free((*dt1)->tiles);
    free(*dt1);
    *dt1 = NULL;
}

int DT1_GetTileCount(const DT1 *dt1) { return dt1->tile_count; }

const DT1Tile *DT1_GetTile(const DT1 *dt1, const int index) {
    return (index >= 0 && index < dt1->tile_count) ? &dt1->tiles[index] : NULL;
}
