#include "Figure.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../types/DCC.h"
#include "../types/Palette.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIGURE_TICK_MS 40 // the game runs animations at 25 ticks per second

typedef struct FigureLayer {
    DCC          *dcc; // NULL when the layer isn't drawn
    SDL_Texture **textures;
    SDL_BlendMode blend;
    uint8_t       alpha;
} FigureLayer;

struct Figure {
    COF           *cof;
    const Palette *palette;
    FigureLayer    layers[COF_MAX_LAYERS];
    int            xmin;
    int            ymin;
    int            xmax;
    int            ymax;
};

static void Figure__CofPath(char *out, const size_t out_len, const FigureSpec *spec, const char *mode) {
    snprintf(out, out_len, "/data/global/%s/%s/cof/%s%s%s.cof", spec->base, spec->token, spec->token, mode,
             spec->weapon_class);
}

// True when the mode's COF exists and at least one of its layers has a DCC or DC6 file.
bool Figure_ModeExists(const FigureSpec *spec, const char *mode) {
    char path[256];
    Figure__CofPath(path, sizeof(path), spec, mode);

    COF *cof = COF_Load(path);
    if (cof == NULL) {
        return false;
    }

    bool found = false;
    for (int l = 0; l < COF_GetLayerCount(cof) && !found; l++) {
        const COFLayer *layer = COF_GetLayer(cof, l);
        if (layer->component < 0 || layer->component >= COF_COMPONENT_COUNT ||
            spec->armor[layer->component][0] == '\0') {
            continue;
        }

        const char *component = COF_ComponentCode(layer->component);
        snprintf(path, sizeof(path), "/data/global/%s/%s/%s/%s%s%s%s%s.dcc", spec->base, spec->token, component,
                 spec->token, component, spec->armor[layer->component], mode, layer->weapon_class);
        found = FileManager_FileExists(path);
        if (!found) {
            memcpy(&path[strlen(path) - 3], "dc6", 3);
            found = FileManager_FileExists(path);
        }
    }

    COF_Destroy(&cof);
    return found;
}

// The game's draw effects for transparent layers (glows, fire, ghosts)
static void Figure__BlendFor(const COFLayer *layer, SDL_BlendMode *blend, uint8_t *alpha) {
    *blend = SDL_BLENDMODE_BLEND;
    *alpha = 255;
    if (!layer->transparent) {
        return;
    }

    switch (layer->draw_effect) {
    case 0:
        *alpha = 192; // 25% transparent
        break;
    case 1:
        *alpha = 128; // 50%
        break;
    case 2:
        *alpha = 64; // 75%
        break;
    case 5:
        break; // normal
    default:
        *blend = SDL_BLENDMODE_ADD; // modulate / burn / 2x modes: brightening effects
        break;
    }
}

Figure *Figure_Create(const FigureSpec *spec, const char *mode, const char *palette_name) {
    char path[256];
    Figure__CofPath(path, sizeof(path), spec, mode);

    COF *cof = COF_Load(path);
    if (cof == NULL) {
        return NULL;
    }

    Figure *figure = malloc(sizeof(Figure));
    FAIL_IF_NULL(figure);
    memset(figure, 0, sizeof(Figure));
    figure->cof     = cof;
    figure->palette = Palette_Get(palette_name);
    figure->xmin    = 0x7FFFFFFF;
    figure->ymin    = 0x7FFFFFFF;
    figure->xmax    = -0x7FFFFFFF;
    figure->ymax    = -0x7FFFFFFF;

    const int directions = COF_GetDirectionCount(cof);
    const int frames     = COF_GetFramesPerDirection(cof);

    for (int l = 0; l < COF_GetLayerCount(cof); l++) {
        const COFLayer *layer = COF_GetLayer(cof, l);
        if (layer->component < 0 || layer->component >= COF_COMPONENT_COUNT ||
            spec->armor[layer->component][0] == '\0') {
            continue;
        }

        const char *component = COF_ComponentCode(layer->component);
        snprintf(path, sizeof(path), "/data/global/%s/%s/%s/%s%s%s%s%s.dcc", spec->base, spec->token, component,
                 spec->token, component, spec->armor[layer->component], mode, layer->weapon_class);

        DCC *dcc = DCC_Load(path);
        if (dcc == NULL) {
            // A few layers (Mephisto, some deaths) are DC6 images instead
            const size_t len = strlen(path);
            memcpy(&path[len - 3], "dc6", 3);
            dcc = DCC_LoadFromDC6(path);
        }
        if (dcc == NULL) {
            continue;
        }
        if (DCC_GetDirectionCount(dcc) != directions) {
            LOG_DEBUG("'%s' has %d directions, the COF has %d", path, DCC_GetDirectionCount(dcc), directions);
        }

        FigureLayer *out = &figure->layers[l];
        out->dcc         = dcc;
        out->textures    = calloc((size_t)directions * frames, sizeof(SDL_Texture *));
        FAIL_IF_NULL(out->textures);
        Figure__BlendFor(layer, &out->blend, &out->alpha);

        // Bounds over every frame of this layer
        for (int d = 0; d < DCC_GetDirectionCount(dcc); d++) {
            for (int f = 0; f < DCC_GetFramesPerDirection(dcc); f++) {
                const DCCFrame *frame = DCC_GetFrame(dcc, d, f);
                if (frame == NULL || frame->pixels == NULL) {
                    continue;
                }
                figure->xmin = frame->x < figure->xmin ? frame->x : figure->xmin;
                figure->ymin = frame->y < figure->ymin ? frame->y : figure->ymin;
                figure->xmax = frame->x + frame->width > figure->xmax ? frame->x + frame->width : figure->xmax;
                figure->ymax = frame->y + frame->height > figure->ymax ? frame->y + frame->height : figure->ymax;
            }
        }
    }

    if (figure->xmax < figure->xmin) {
        figure->xmin = figure->ymin = figure->xmax = figure->ymax = 0;
    }

    return figure;
}

void Figure_Destroy(Figure **figure) {
    if (figure == NULL || *figure == NULL) {
        return;
    }

    const int count = COF_GetDirectionCount((*figure)->cof) * COF_GetFramesPerDirection((*figure)->cof);
    for (int l = 0; l < COF_MAX_LAYERS; l++) {
        FigureLayer *layer = &(*figure)->layers[l];
        if (layer->textures != NULL) {
            for (int i = 0; i < count; i++) {
                if (layer->textures[i] != NULL) {
                    SDL_DestroyTexture(layer->textures[i]);
                }
            }
            free(layer->textures);
        }
        DCC_Destroy(&layer->dcc);
    }

    COF_Destroy(&(*figure)->cof);
    free(*figure);
    *figure = NULL;
}

int Figure_GetDirectionCount(const Figure *figure) { return COF_GetDirectionCount(figure->cof); }

int Figure_GetFramesPerDirection(const Figure *figure) { return COF_GetFramesPerDirection(figure->cof); }

int Figure_GetFrameMilliseconds(const Figure *figure) {
    // The animation advances rate/256 frames per tick
    return (FIGURE_TICK_MS * 256) / COF_GetAnimationRate(figure->cof);
}

void Figure_GetBounds(const Figure *figure, int *xmin, int *ymin, int *xmax, int *ymax) {
    *xmin = figure->xmin;
    *ymin = figure->ymin;
    *xmax = figure->xmax;
    *ymax = figure->ymax;
}

static SDL_Texture *Figure__Texture(Figure *figure, FigureLayer *layer, const int direction, const int frame) {
    const int index = (direction * COF_GetFramesPerDirection(figure->cof)) + frame;
    if (layer->textures[index] != NULL) {
        return layer->textures[index];
    }

    const DCCFrame *source = DCC_GetFrame(layer->dcc, direction, frame);
    if (source == NULL || source->pixels == NULL || source->width <= 0 || source->height <= 0) {
        return NULL;
    }

    uint32_t *pixels = malloc(sizeof(uint32_t) * (size_t)source->width * source->height);
    FAIL_IF_NULL(pixels);
    for (int i = 0; i < source->width * source->height; i++) {
        pixels[i] = source->pixels[i] == 0 ? 0 : figure->palette->entries[source->pixels[i]];
    }

    SDL_Texture *texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                             source->width, source->height);
    if (texture != NULL) {
        SDL_UpdateTexture(texture, NULL, pixels, source->width * 4);
        SDL_SetTextureBlendMode(texture, layer->blend);
        SDL_SetTextureAlphaMod(texture, layer->alpha);
    }
    free(pixels);

    layer->textures[index] = texture;
    return texture;
}

void Figure_Draw(Figure *figure, const int direction, const int frame, const int x, const int y, const float scale) {
    const COF *cof = figure->cof;

    for (int order = 0; order < COF_GetLayerCount(cof); order++) {
        const int component = COF_GetDrawOrder(cof, direction, frame, order);

        // Find the layer for this component
        for (int l = 0; l < COF_GetLayerCount(cof); l++) {
            FigureLayer *layer = &figure->layers[l];
            if (COF_GetLayer(cof, l)->component != component || layer->dcc == NULL) {
                continue;
            }

            const DCCFrame *source  = DCC_GetFrame(layer->dcc, direction, frame);
            SDL_Texture    *texture = Figure__Texture(figure, layer, direction, frame);
            if (source != NULL && texture != NULL) {
                const SDL_Rect dest = {x + (int)((float)source->x * scale), y + (int)((float)source->y * scale),
                                       (int)((float)source->width * scale), (int)((float)source->height * scale)};
                SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
            }
            break;
        }
    }
}
