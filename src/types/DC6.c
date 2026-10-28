#include "DC6.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "DC6Frame.h"
#include <string.h>

#include <assert.h>
#include <stdlib.h>

typedef struct DC6Header {
    int32_t  version;
    uint32_t flags;
    uint32_t encoding;
    uint8_t  termination[4];
    uint32_t directions;
    uint32_t frames_per_direction;
} DC6Header;

struct DC6 {
    struct DC6Header header;
    uint32_t        *frame_pointers;
    DC6Frame       **dc6_frames;
};

#define DC6_MAX_FRAMES 4096 // directions x frames: more is a broken file

static DC6 *DC6__FromStream(MpqStream *stream);

DC6 *DC6_Load(const char *path) { return DC6__FromStream(FileManager_OpenFile(path)); }

DC6 *DC6_LoadMemory(const uint8_t *data, const uint32_t size) {
    uint8_t *copy = malloc(size > 0 ? size : 1);
    FAIL_IF_NULL(copy);
    if (size > 0) {
        memcpy(copy, data, size);
    }
    MpqStream *stream = MpqStream_CreateFromMemory("(memory)", copy, size); // (the stream frees it)
    return DC6__FromStream(stream);
}

// (closes the stream)
static DC6 *DC6__FromStream(MpqStream *stream) {
    DC6 *result = malloc(sizeof(DC6));
    FAIL_IF_NULL(result);
    memset(&result->header, 0, sizeof(DC6Header));
    const uint64_t present = MpqStream_GetSize(stream) >= sizeof(DC6Header) ? 1 : 0;
    if (present != 0) {
        MpqStream_Read(stream, &result->header, 0, sizeof(DC6Header));
    }
    const uint64_t total = (uint64_t)result->header.directions * result->header.frames_per_direction;
    if (present == 0 || total > DC6_MAX_FRAMES ||
        sizeof(DC6Header) + (total * sizeof(uint32_t)) > MpqStream_GetSize(stream)) {
        result->header.directions           = 0; // (none, or broken: no frames)
        result->header.frames_per_direction = 0;
        // Not there (text mode, from an extracted folder): no frames
        result->frame_pointers = NULL;
        result->dc6_frames     = NULL;
        MpqStream_Destroy(&stream);
        return result;
    }

    const uint32_t total_frames = (uint32_t)total;
    result->frame_pointers      = calloc(total_frames > 0 ? total_frames : 1, sizeof(uint32_t));

    FAIL_IF_NULL(result->frame_pointers);

    MpqStream_Read(stream, result->frame_pointers, 0, sizeof(uint32_t) * total_frames);

    result->dc6_frames = malloc(sizeof(DC6Frame *) * (total_frames > 0 ? total_frames : 1));
    FAIL_IF_NULL(result->dc6_frames);

    for (uint32_t i = 0; i < total_frames; i++) {
        result->dc6_frames[i] = DC6Frame_Create(stream);
    }

    MpqStream_Destroy(&stream);

    return result;
}

void DC6_Destroy(struct DC6 **dc6) {
    assert(dc6 != NULL);
    if (*dc6 == NULL) {
        return; // (never loaded: the automap of a play-test you never opened it in)
    }

    const uint32_t total_frames = DC6_GetTotalFrameCount(*dc6);
    for (uint32_t i = 0; i < total_frames; i++) {
        DC6Frame_Destroy(&(*dc6)->dc6_frames[i]);
    }

    free((*dc6)->dc6_frames);
    free((*dc6)->frame_pointers);

    free(*dc6);
    *dc6 = NULL;
}

uint32_t DC6_GetDirectionCount(const DC6 *dc6) { return dc6->header.directions; }

uint32_t DC6_GetTotalFrameCount(const DC6 *dc6) { return dc6->header.directions * dc6->header.frames_per_direction; }

// (a picture that isn't there, text mode without the MPQs: every frame is empty)
void DC6_GetFrameSize(const DC6 *dc6, const uint32_t frame_index, uint32_t *width, uint32_t *height) {
    assert(dc6 != NULL);
    if (frame_index >= DC6_GetTotalFrameCount(dc6)) {
        if (width != NULL) {
            *width = 0;
        }
        if (height != NULL) {
            *height = 0;
        }
        return;
    }
    DC6Frame_GetFrameSize(dc6->dc6_frames[frame_index], width, height);
}

const uint8_t *DC6_GetFramePixelData(const DC6 *dc6, const uint32_t frame_index) {
    assert(dc6 != NULL);
    if (frame_index >= DC6_GetTotalFrameCount(dc6)) {
        return NULL;
    }
    return DC6Frame_GetPixelData(dc6->dc6_frames[frame_index]);
}

void DC6_GetFrameOffset(const DC6 *dc6, const uint32_t frame_index, int32_t *offset_x, int32_t *offset_y) {
    assert(dc6 != NULL);
    if (frame_index >= DC6_GetTotalFrameCount(dc6)) {
        *offset_x = 0;
        *offset_y = 0;
        return;
    }
    DC6Frame_GetFrameOffset(dc6->dc6_frames[frame_index], offset_x, offset_y);
}
