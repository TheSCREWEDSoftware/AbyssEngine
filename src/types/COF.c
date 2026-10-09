#include "COF.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <stdlib.h>
#include <string.h>

#define COF_HEADER_SIZE 28
#define COF_LAYER_SIZE  9

struct COF {
    int      layer_count;
    int      frames_per_direction;
    int      direction_count;
    int      animation_rate;
    COFLayer layers[COF_MAX_LAYERS];
    uint8_t *draw_order; // [direction][frame][layer] -> component
};

static const char *cof_component_codes[COF_COMPONENT_COUNT] = {"hd", "tr", "lg", "ra", "la", "rh", "lh", "sh",
                                                               "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"};

const char *COF_ComponentCode(const int component) {
    return (component >= 0 && component < COF_COMPONENT_COUNT) ? cof_component_codes[component] : "";
}

COF *COF_Load(const char *path) {
    if (!FileManager_FileExists(path)) {
        return NULL;
    }

    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    uint8_t       *data   = malloc(size > 0 ? size : 1);
    FAIL_IF_NULL(data);
    MpqStream_Read(stream, data, 0, size);
    MpqStream_Destroy(&stream);

    if (size < COF_HEADER_SIZE) {
        free(data);
        return NULL;
    }

    const int layers     = data[0];
    const int frames     = data[1];
    const int directions = data[2];
    const int rate       = (int16_t)(data[24] | (data[25] << 8));

    const size_t needed =
        COF_HEADER_SIZE + ((size_t)layers * COF_LAYER_SIZE) + (size_t)frames + ((size_t)directions * frames * layers);
    if (layers <= 0 || layers > COF_MAX_LAYERS || frames <= 0 || directions <= 0 || needed > size) {
        LOG_WARN("'%s' has an invalid COF header.", path);
        free(data);
        return NULL;
    }

    COF *cof = malloc(sizeof(COF));
    FAIL_IF_NULL(cof);
    memset(cof, 0, sizeof(COF));
    cof->layer_count          = layers;
    cof->frames_per_direction = frames;
    cof->direction_count      = directions;
    cof->animation_rate       = rate > 0 ? rate : 256;

    const uint8_t *layer_data = data + COF_HEADER_SIZE;
    for (int i = 0; i < layers; i++) {
        const uint8_t *l           = layer_data + (i * COF_LAYER_SIZE);
        cof->layers[i].component   = l[0];
        cof->layers[i].shadow      = l[1] != 0;
        cof->layers[i].transparent = l[3] != 0;
        cof->layers[i].draw_effect = l[4];
        memcpy(cof->layers[i].weapon_class, l + 5, 3);
        cof->layers[i].weapon_class[3] = '\0';
    }

    // Skip the per-frame event keys, then copy the layer drawing order
    const uint8_t *order_data = layer_data + ((size_t)layers * COF_LAYER_SIZE) + frames;
    const size_t   order_size = (size_t)directions * frames * layers;
    cof->draw_order           = malloc(order_size);
    FAIL_IF_NULL(cof->draw_order);
    memcpy(cof->draw_order, order_data, order_size);

    free(data);
    return cof;
}

void COF_Destroy(COF **cof) {
    if (cof == NULL || *cof == NULL) {
        return;
    }
    free((*cof)->draw_order);
    free(*cof);
    *cof = NULL;
}

int COF_GetLayerCount(const COF *cof) { return cof->layer_count; }

int COF_GetDirectionCount(const COF *cof) { return cof->direction_count; }

int COF_GetFramesPerDirection(const COF *cof) { return cof->frames_per_direction; }

int COF_GetAnimationRate(const COF *cof) { return cof->animation_rate; }

const COFLayer *COF_GetLayer(const COF *cof, const int layer) {
    return (layer >= 0 && layer < cof->layer_count) ? &cof->layers[layer] : NULL;
}

int COF_GetDrawOrder(const COF *cof, const int direction, const int frame, const int order) {
    if (direction < 0 || direction >= cof->direction_count || frame < 0 || frame >= cof->frames_per_direction ||
        order < 0 || order >= cof->layer_count) {
        return COF_COMPONENT_NONE;
    }
    return cof->draw_order[(((size_t)direction * cof->frames_per_direction) + frame) * cof->layer_count + order];
}
