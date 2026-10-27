#include "Sprite.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../types/DC6.h"
#include "../types/Palette.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct SpriteFrame {
    SDL_Texture *texture;
    uint32_t     width;
    uint32_t     height;
    int32_t      offset_x;
    int32_t      offset_y;
} SpriteFrame;

struct Sprite {
    struct SpriteFrame *frames;
    uint32_t            frame_count;
    uint32_t            direction_count;
    double              play_length;
    uint32_t            ticks_per_frame;
    uint64_t            last_ticks;
    uint32_t            animation_index;
};

void Sprite__LoadDC6(Sprite *sprite, const char *path, const char *palette_name, const uint8_t *remap);

char *get_extension(const char *path) {
    const char *ext = strrchr(path, '.') + 1;
    if (strlen(path) <= (size_t)(ext - path)) {
        LOG_FATAL("Invalid path extension for '%s'.", path);
    }

    char *result = strdup(ext);
    for (char *ch = result; *ch; ch++) {
        *ch = (char)tolower((int)*ch);
    }

    return result;
}

bool Sprite_ItemShift(const int shift, const int color, uint8_t remap[256]) {
    static const char *const files[] = {NULL,        "grey",    "grey2",    "gold",        "brown",
                                        "greybrown", "invgrey", "invgrey2", "invgreybrown"};
    if (shift <= 0 || shift >= (int)(sizeof(files) / sizeof(files[0])) || color < 0) {
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/Palette/%s.dat", files[shift]);
    MpqStream *stream = FileManager_FileExists(path) ? FileManager_OpenFile(path) : NULL;
    if (stream == NULL) {
        return false;
    }
    const uint32_t offset = (uint32_t)color * 256;
    bool           ok     = MpqStream_GetSize(stream) >= offset + 256;
    if (ok) {
        MpqStream_Seek(stream, offset, SEEK_SET);
        ok = MpqStream_Read(stream, remap, 0, 256) == 256;
    }
    MpqStream_Destroy(&stream);
    return ok;
}

Sprite *Sprite_Create(const char *path, const char *palette_name) {
    return Sprite_CreateShifted(path, palette_name, NULL);
}

Sprite *Sprite_CreateShifted(const char *path, const char *palette_name, const uint8_t *remap) {
    Sprite *result = malloc(sizeof(Sprite));
    FAIL_IF_NULL(result);

    memset(result, 0, sizeof(Sprite));

    char *file_ext = get_extension(path);

    if (strcmp(file_ext, "dc6") == 0) {
        Sprite__LoadDC6(result, path, palette_name, remap);
    } else {
        LOG_FATAL("Could not load Sprite: '%s' is an unsupported file extension.", file_ext);
    }

    free(file_ext);
    return result;
}

void Sprite__LoadDC6(Sprite *sprite, const char *path, const char *palette_name, const uint8_t *remap) {
    struct DC6           *dc6     = DC6_Load(path);
    const struct Palette *Palette = Palette_Get(palette_name);

    sprite->frame_count     = DC6_GetTotalFrameCount(dc6);
    sprite->direction_count = DC6_GetDirectionCount(dc6);
    if (sprite->frame_count == 0) {
        // No picture (text mode without the MPQs): empty frames, so drawing any of them does nothing
        sprite->frame_count     = 256;
        sprite->direction_count = 1;
        sprite->frames          = calloc(sprite->frame_count, sizeof(SpriteFrame));
        FAIL_IF_NULL(sprite->frames);
        DC6_Destroy(&dc6);
        return;
    }
    sprite->frames          = malloc(sizeof(SpriteFrame) * sprite->frame_count);
    sprite->last_ticks      = SDL_GetTicks64();
    sprite->animation_index = 0;

    FAIL_IF_NULL(sprite->frames);

    Sprite_SetPlayLength(sprite, 1.0f);

    for (uint32_t i = 0; i < sprite->frame_count; i++) {
        SpriteFrame   *spr_frame        = &sprite->frames[i];
        const uint8_t *frame_pixel_data = DC6_GetFramePixelData(dc6, i);

        DC6_GetFrameSize(dc6, i, &spr_frame->width, &spr_frame->height);
        DC6_GetFrameOffset(dc6, i, &spr_frame->offset_x, &spr_frame->offset_y);

        if (spr_frame->width == 0 || spr_frame->height == 0) {
            spr_frame->texture = NULL; // empty frame (some DC6 files have blank frames)
            continue;
        }

        spr_frame->texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                               (int)spr_frame->width, (int)spr_frame->height);

        uint32_t *pixels = malloc((size_t)spr_frame->width * spr_frame->height * 4);
        FAIL_IF_NULL(pixels);
        memset(pixels, 0, (size_t)spr_frame->width * spr_frame->height * 4);

        for (uint32_t idx = 0; idx < (spr_frame->width * spr_frame->height); idx++) {
            const uint8_t index = frame_pixel_data[idx];
            pixels[idx]         = Palette->entries[remap != NULL && index != 0 ? remap[index] : index];
        }

        SDL_UpdateTexture(spr_frame->texture, NULL, pixels, (int)spr_frame->width * 4);
        // Palette index 0 has zero alpha: blend by default so it shows as transparent.
        SDL_SetTextureBlendMode(spr_frame->texture, SDL_BLENDMODE_BLEND);
        free(pixels);
    }

    DC6_Destroy(&dc6);
}

void Sprite_Destroy(Sprite **sprite) {
    if (sprite == NULL || *sprite == NULL) {
        return;
    }
    for (uint32_t i = 0; i < (*sprite)->frame_count; i++) {
        if ((*sprite)->frames[i].texture != NULL) {
            SDL_DestroyTexture((*sprite)->frames[i].texture);
        }
    }

    free((*sprite)->frames);

    free(*sprite);
    *sprite = NULL;
}

void Sprite_Draw(const Sprite *sprite, const uint8_t frame_index, const int x, const int y) {
    const SpriteFrame *frame = &sprite->frames[frame_index];

    const SDL_Rect dest = {x + frame->offset_x, y - (int)frame->height + frame->offset_y, (int)frame->width,
                           (int)frame->height};

    SDL_RenderCopy(sdl_renderer, frame->texture, NULL, &dest);
}

void Sprite_DrawAnimated(Sprite *sprite, const int x, const int y) {
    if (sprite->ticks_per_frame == 0) {
        LOG_FATAL("Attempted to animate a Sprite with no ticks per frame!");
    }

    const uint64_t cur_ticks  = SDL_GetTicks64();
    uint64_t       tick_delta = cur_ticks - sprite->last_ticks;

    while (tick_delta >= sprite->ticks_per_frame) {
        sprite->last_ticks += sprite->ticks_per_frame;
        tick_delta         -= sprite->ticks_per_frame;
        if (++sprite->animation_index >= sprite->frame_count) {
            sprite->animation_index = 0;
        }
    }

    Sprite_Draw(sprite, (uint8_t)sprite->animation_index, x, y);
}
void Sprite_DrawSection(const Sprite *sprite, const int frame_index, const int x, const int y,
                        const SDL_Rect *section) {
    if (sprite == NULL || frame_index < 0 || (uint32_t)frame_index >= sprite->frame_count || section->w <= 0 ||
        section->h <= 0) {
        return;
    }
    const SpriteFrame *frame = &sprite->frames[frame_index];
    const SDL_Rect     dest  = {x + section->x, y + section->y, section->w, section->h};
    SDL_RenderCopy(sdl_renderer, frame->texture, section, &dest);
}

void Sprite_GetFrameSize(const Sprite *sprite, const int frame_index, int *width, int *height) {
    if (width != NULL) {
        *width = sprite->frames[frame_index].width;
    }

    if (height != NULL) {
        *height = sprite->frames[frame_index].height;
    }
}

void Sprite_DrawMulti(const Sprite *sprite, const uint8_t frame_index, const int x, const int y, const int frames_x,
                      const int frames_y) {
    int cur_x     = x;
    int cur_y     = y;
    int cur_frame = frame_index;

    for (int py = 0; py < frames_y; py++) {
        for (int px = 0; px < frames_x; px++) {
            const SpriteFrame *frame = &sprite->frames[cur_frame++];
            SDL_Rect           dest  = {cur_x, cur_y, (int)frame->width, (int)frame->height};
            SDL_RenderCopy(sdl_renderer, frame->texture, NULL, &dest);
            cur_x += (int)sprite->frames[cur_frame - 1].width;
        }
        cur_x  = x;
        cur_y += (int)sprite->frames[cur_frame - 1].height;
    }
}

uint32_t Sprite_GetFrameCount(const Sprite *sprite) { return sprite->frame_count; }

uint32_t Sprite_GetDirectionCount(const Sprite *sprite) {
    return sprite->direction_count > 0 ? sprite->direction_count : 1;
}

void Sprite_GetFrameOffset(const Sprite *sprite, const int frame_index, int *offset_x, int *offset_y) {
    if (offset_x != NULL) {
        *offset_x = sprite->frames[frame_index].offset_x;
    }

    if (offset_y != NULL) {
        *offset_y = sprite->frames[frame_index].offset_y;
    }
}

void Sprite_DrawScaled(const Sprite *sprite, const int frame_index, const int x, const int y, const float scale) {
    const SpriteFrame *frame = &sprite->frames[frame_index];
    if (frame->texture == NULL) {
        return;
    }

    const SDL_Rect dest = {x, y, (int)((float)frame->width * scale), (int)((float)frame->height * scale)};
    SDL_RenderCopy(sdl_renderer, frame->texture, NULL, &dest);
}

void Sprite_DrawStretched(const Sprite *sprite, const int frame_index, const int x, const int y, const int width,
                          const int height) {
    const SpriteFrame *frame = &sprite->frames[frame_index];
    if (frame->texture == NULL || width <= 0 || height <= 0) {
        return;
    }

    const SDL_Rect dest = {x, y, width, height};
    SDL_RenderCopy(sdl_renderer, frame->texture, NULL, &dest);
}

void Sprite_SetBlendMode(const Sprite *sprite, const SDL_BlendMode blend_mode) {
    for (uint32_t frame_idx = 0; frame_idx < sprite->frame_count; frame_idx++) {
        if (sprite->frames[frame_idx].texture != NULL) {
            SDL_SetTextureBlendMode(sprite->frames[frame_idx].texture, blend_mode);
        }
    }
}

void Sprite_SetAlphaMod(const Sprite *sprite, const uint8_t alpha) {
    for (uint32_t frame_idx = 0; frame_idx < sprite->frame_count; frame_idx++) {
        if (sprite->frames[frame_idx].texture != NULL) {
            SDL_SetTextureAlphaMod(sprite->frames[frame_idx].texture, alpha);
        }
    }
}

void Sprite_SetPlayLength(Sprite *sprite, const double play_length) {
    sprite->play_length     = play_length;
    sprite->ticks_per_frame = (uint32_t)((1000 * play_length) / (double)sprite->frame_count);
}
