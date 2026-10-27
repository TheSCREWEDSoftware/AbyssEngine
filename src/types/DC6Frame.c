#include "DC6Frame.h"
#include "../common/Logging.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct DC6FrameHeader {
    uint32_t flipped;
    uint32_t width;
    uint32_t height;
    int32_t  offset_x;
    int32_t  offset_y;
    uint32_t unknown;
    uint32_t next_block;
    uint32_t data_length;
} DC6FrameHeader;

struct DC6Frame {
    DC6FrameHeader header;
    uint8_t       *frame_data;
    char           terminator[3];
    uint8_t       *indexed_pixel_data;
};

void DC6Frame__Decode(DC6Frame *dc6_frame);

DC6Frame *DC6Frame_Create(MpqStream *mpq_stream) {
    DC6Frame *result = malloc(sizeof(DC6Frame));
    memset(result, 0, sizeof(DC6Frame));
    FAIL_IF_NULL(result);

    MpqStream_Read(mpq_stream, &result->header, 0, sizeof(DC6FrameHeader));
    // A broken file (or a mod's): sizes past what's there make an empty frame instead of a crash
    const uint32_t left = MpqStream_GetSize(mpq_stream) > MpqStream_Tell(mpq_stream)
                              ? MpqStream_GetSize(mpq_stream) - MpqStream_Tell(mpq_stream)
                              : 0;
    if (result->header.data_length > left || result->header.width > 4096 || result->header.height > 4096) {
        result->header.data_length = result->header.data_length > left ? left : result->header.data_length;
        if (result->header.width > 4096 || result->header.height > 4096) {
            result->header.width  = 0;
            result->header.height = 0;
        }
    }
    result->frame_data = calloc(result->header.data_length > 0 ? result->header.data_length : 1, 1);
    FAIL_IF_NULL(result->frame_data);

    MpqStream_Read(mpq_stream, result->frame_data, 0, result->header.data_length);
    MpqStream_Read(mpq_stream, result->terminator, 0, 3);

    DC6Frame__Decode(result);

    return result;
}

void DC6Frame_Destroy(DC6Frame **dc6_frame) {
    free((*dc6_frame)->indexed_pixel_data);
    free((*dc6_frame)->frame_data);
    free(*dc6_frame);
    *dc6_frame = NULL;
}

void DC6Frame_GetFrameSize(const DC6Frame *dc6_frame, uint32_t *width, uint32_t *height) {
    assert((width != NULL) || (height != NULL));

    if (width != NULL) {
        *width = dc6_frame->header.width;
    }

    if (height != NULL) {
        *height = dc6_frame->header.height;
    }
}

const uint8_t *DC6Frame_GetPixelData(const DC6Frame *dc6_frame) { return dc6_frame->indexed_pixel_data; }

void DC6Frame_GetFrameOffset(const DC6Frame *dc6_frame, int32_t *offset_x, int32_t *offset_y) {
    if (offset_x != NULL) {
        *offset_x = dc6_frame->header.offset_x;
    }

    if (offset_y != NULL) {
        *offset_y = dc6_frame->header.offset_y;
    }
}

void DC6Frame__Decode(DC6Frame *dc6_frame) {
    assert(dc6_frame != NULL);
    assert(dc6_frame->frame_data != NULL);

    uint32_t frame_width;
    uint32_t frame_height;
    DC6Frame_GetFrameSize(dc6_frame, &frame_width, &frame_height);
    const uint32_t total_pixels = frame_height * frame_width;

    dc6_frame->indexed_pixel_data = calloc(total_pixels > 0 ? (size_t)total_pixels : 1, 1);
    FAIL_IF_NULL(dc6_frame->indexed_pixel_data);
    if (total_pixels == 0) {
        return;
    }

    // Run lengths from the bottom row up: 0x80 ends a row, 0x80 | n skips n, n copies n pixels. Nothing is read or
    // written outside the frame (a broken file draws what it can)
    const uint32_t length = dc6_frame->header.data_length;
    uint32_t       x      = 0;
    uint32_t       y      = frame_height - 1;
    uint32_t       offset = 0;
    while (offset < length) {
        const int b = dc6_frame->frame_data[offset++];
        if (b == 0x80) {
            if (y == 0) {
                return;
            }
            --y;
            x = 0;
            continue;
        }
        if ((b & 0x80) > 0) {
            x += b & 0x7F;
            continue;
        }
        for (int i = 0; i < b && offset < length; i++, offset++) {
            if (x + (uint32_t)i < frame_width) {
                dc6_frame->indexed_pixel_data[x + (uint32_t)i + (y * frame_width)] = dc6_frame->frame_data[offset];
            }
        }
        x += (uint32_t)b;
    }
}
