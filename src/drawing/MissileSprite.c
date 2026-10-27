#include "MissileSprite.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../types/DCC.h"
#include "../types/Palette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MISSILE_SPRITES_MAX 192

struct MissileSprite {
    char           cel_file[40];
    char           palette[16];
    bool           glow;
    bool           missing; // no such file: remembered so it isn't looked for again
    DCC           *dcc;
    const Palette *colours;
    SDL_Texture  **textures; // [direction * frames + frame]
};

static MissileSprite sprites[MISSILE_SPRITES_MAX];
static int           sprite_count = 0;

MissileSprite *MissileSprite_Get(const char *cel_file, const char *palette_name, const bool glow) {
    if (cel_file == NULL || cel_file[0] == '\0' || strcasecmp(cel_file, "null") == 0) {
        return NULL;
    }
    for (int i = 0; i < sprite_count; i++) {
        MissileSprite *s = &sprites[i];
        if (strcasecmp(s->cel_file, cel_file) == 0 && strcmp(s->palette, palette_name) == 0 && s->glow == glow) {
            return s->missing ? NULL : s;
        }
    }
    if (sprite_count >= MISSILE_SPRITES_MAX) {
        return NULL;
    }
    MissileSprite *s = &sprites[sprite_count++];
    memset(s, 0, sizeof(MissileSprite));
    snprintf(s->cel_file, sizeof(s->cel_file), "%s", cel_file);
    snprintf(s->palette, sizeof(s->palette), "%s", palette_name);
    s->glow = glow;
    char path[128];
    snprintf(path, sizeof(path), "/data/global/missiles/%s.dcc", cel_file);
    s->dcc     = DCC_Load(path);
    s->colours = Palette_Get(palette_name);
    if (s->dcc == NULL || s->colours == NULL) {
        LOG_DEBUG("No missile picture '%s'", path);
        DCC_Destroy(&s->dcc);
        s->missing = true;
        return NULL;
    }
    const int count = DCC_GetDirectionCount(s->dcc) * DCC_GetFramesPerDirection(s->dcc);
    s->textures     = calloc(count > 0 ? (size_t)count : 1, sizeof(SDL_Texture *));
    FAIL_IF_NULL(s->textures);
    return s;
}

int MissileSprite_Directions(const MissileSprite *sprite) { return DCC_GetDirectionCount(sprite->dcc); }

int MissileSprite_Frames(const MissileSprite *sprite) { return DCC_GetFramesPerDirection(sprite->dcc); }

static SDL_Texture *MissileSprite__Texture(MissileSprite *s, const int direction, const int frame,
                                           const DCCFrame **source_out) {
    const DCCFrame *source = DCC_GetFrame(s->dcc, direction, frame);
    *source_out            = source;
    if (source == NULL || source->pixels == NULL || source->width <= 0 || source->height <= 0) {
        return NULL;
    }
    const int index = (direction * DCC_GetFramesPerDirection(s->dcc)) + frame;
    if (s->textures[index] != NULL) {
        return s->textures[index];
    }
    uint32_t *pixels = malloc(sizeof(uint32_t) * (size_t)source->width * (size_t)source->height);
    FAIL_IF_NULL(pixels);
    for (int i = 0; i < source->width * source->height; i++) {
        pixels[i] = source->pixels[i] == 0 ? 0 : s->colours->entries[source->pixels[i]];
    }
    SDL_Texture *texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                             source->width, source->height);
    if (texture != NULL) {
        SDL_UpdateTexture(texture, NULL, pixels, source->width * 4);
        SDL_SetTextureBlendMode(texture, s->glow ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
    free(pixels);
    s->textures[index] = texture;
    return texture;
}

void MissileSprite_Draw(MissileSprite *sprite, const int direction, const int frame, const int x, const int y,
                        const float scale) {
    if (sprite == NULL) {
        return;
    }
    const int directions = DCC_GetDirectionCount(sprite->dcc);
    const int frames     = DCC_GetFramesPerDirection(sprite->dcc);
    if (directions <= 0 || frames <= 0) {
        return;
    }
    const DCCFrame *source  = NULL;
    SDL_Texture    *texture = MissileSprite__Texture(sprite, ((direction % directions) + directions) % directions,
                                                     ((frame % frames) + frames) % frames, &source);
    if (texture == NULL) {
        return;
    }
    const SDL_Rect dest = {x + (int)((float)source->x * scale), y + (int)((float)source->y * scale),
                           (int)((float)source->width * scale), (int)((float)source->height * scale)};
    SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
}

void MissileSprite_ReleaseAll(void) {
    for (int i = 0; i < sprite_count; i++) {
        MissileSprite *s = &sprites[i];
        if (s->textures != NULL) {
            const int count = DCC_GetDirectionCount(s->dcc) * DCC_GetFramesPerDirection(s->dcc);
            for (int t = 0; t < count; t++) {
                if (s->textures[t] != NULL) {
                    SDL_DestroyTexture(s->textures[t]);
                }
            }
            free(s->textures);
        }
        DCC_Destroy(&s->dcc);
    }
    sprite_count = 0;
}
