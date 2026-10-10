#include "MapRenderer.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../types/Palette.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TILE_HALF_WIDTH  80 // world pixels
#define TILE_HALF_HEIGHT 40
#define SUBTILE_HALF_W   16
#define SUBTILE_HALF_H   8
#define WALL_BASE_Y      80 // walls stand on the bottom corner of their floor diamond

typedef struct TextureEntry {
    const DT1Tile *tile;
    SDL_Texture   *texture;
} TextureEntry;

struct MapRenderer {
    TextureEntry  *entries; // open addressing, keyed by tile pointer
    int            capacity;
    int            count;
    const Palette *palette;
};

// ---------------------------------------------------------------------------------------------------------------------
// Coordinates

void MapView_SubtileToWorld(const float sx, const float sy, float *world_x, float *world_y) {
    *world_x = (sx - sy) * SUBTILE_HALF_W;
    *world_y = (sx + sy) * SUBTILE_HALF_H;
}

void MapView_WorldToSubtile(const float world_x, const float world_y, float *sx, float *sy) {
    const float a = world_x / SUBTILE_HALF_W; // sx - sy
    const float b = world_y / SUBTILE_HALF_H; // sx + sy
    *sx           = (a + b) / 2.0f;
    *sy           = (b - a) / 2.0f;
}

void MapView_WorldToScreen(const MapCamera *camera, const SDL_Rect *view, const float world_x, const float world_y,
                           int *screen_x, int *screen_y) {
    *screen_x = view->x + (view->w / 2) + (int)floorf((world_x - camera->x) * camera->zoom);
    *screen_y = view->y + (view->h / 2) + (int)floorf((world_y - camera->y) * camera->zoom);
}

void MapView_ScreenToWorld(const MapCamera *camera, const SDL_Rect *view, const int screen_x, const int screen_y,
                           float *world_x, float *world_y) {
    *world_x = camera->x + ((float)(screen_x - view->x - (view->w / 2)) / camera->zoom);
    *world_y = camera->y + ((float)(screen_y - view->y - (view->h / 2)) / camera->zoom);
}

const char *MapView_ActPalette(const int act) {
    static const char *palettes[] = {PALETTE_ACT_1, PALETTE_ACT_2, PALETTE_ACT_3, PALETTE_ACT_4, PALETTE_ACT_5};
    return palettes[act < 0 ? 0 : (act > 4 ? 4 : act)];
}

// ---------------------------------------------------------------------------------------------------------------------
// Texture cache

MapRenderer *MapRenderer_Create(void) {
    MapRenderer *renderer = calloc(1, sizeof(MapRenderer));
    FAIL_IF_NULL(renderer);
    return renderer;
}

void MapRenderer_ClearCache(MapRenderer *renderer) {
    for (int i = 0; i < renderer->capacity; i++) {
        if (renderer->entries[i].texture != NULL) {
            SDL_DestroyTexture(renderer->entries[i].texture);
        }
    }
    free(renderer->entries);
    renderer->entries  = NULL;
    renderer->capacity = 0;
    renderer->count    = 0;
    renderer->palette  = NULL;
}

void MapRenderer_Destroy(MapRenderer **renderer) {
    if (renderer == NULL || *renderer == NULL) {
        return;
    }
    MapRenderer_ClearCache(*renderer);
    free(*renderer);
    *renderer = NULL;
}

static uint32_t MapRenderer__Hash(const DT1Tile *tile) {
    uintptr_t value  = (uintptr_t)tile;
    value           ^= value >> 17;
    value           *= 0xED5AD4BBu;
    value           ^= value >> 11;
    return (uint32_t)value;
}

static void MapRenderer__Insert(MapRenderer *renderer, const DT1Tile *tile, SDL_Texture *texture);

static void MapRenderer__Grow(MapRenderer *renderer) {
    TextureEntry *old          = renderer->entries;
    const int     old_capacity = renderer->capacity;
    renderer->capacity         = old_capacity > 0 ? old_capacity * 2 : 512;
    renderer->entries          = calloc((size_t)renderer->capacity, sizeof(TextureEntry));
    FAIL_IF_NULL(renderer->entries);
    renderer->count = 0;
    for (int i = 0; i < old_capacity; i++) {
        if (old[i].tile != NULL) {
            MapRenderer__Insert(renderer, old[i].tile, old[i].texture);
        }
    }
    free(old);
}

static void MapRenderer__Insert(MapRenderer *renderer, const DT1Tile *tile, SDL_Texture *texture) {
    if ((renderer->count + 1) * 2 > renderer->capacity) {
        MapRenderer__Grow(renderer);
    }
    uint32_t slot = MapRenderer__Hash(tile) & (uint32_t)(renderer->capacity - 1);
    while (renderer->entries[slot].tile != NULL) {
        slot = (slot + 1) & (uint32_t)(renderer->capacity - 1);
    }
    renderer->entries[slot].tile    = tile;
    renderer->entries[slot].texture = texture;
    renderer->count++;
}

static SDL_Texture *MapRenderer__Texture(MapRenderer *renderer, const Map *map, const DT1Tile *tile) {
    const Palette *palette = Palette_Get(MapView_ActPalette(map->act));
    if (palette != renderer->palette) {
        MapRenderer_ClearCache(renderer);
        renderer->palette = palette;
    }

    if (renderer->capacity > 0) {
        uint32_t slot = MapRenderer__Hash(tile) & (uint32_t)(renderer->capacity - 1);
        while (renderer->entries[slot].tile != NULL) {
            if (renderer->entries[slot].tile == tile) {
                return renderer->entries[slot].texture;
            }
            slot = (slot + 1) & (uint32_t)(renderer->capacity - 1);
        }
    }

    SDL_Texture *texture = NULL;
    if (tile->image != NULL && tile->image_width > 0 && tile->image_height > 0 && palette != NULL) {
        const int size   = tile->image_width * tile->image_height;
        uint32_t *pixels = malloc(sizeof(uint32_t) * (size_t)size);
        FAIL_IF_NULL(pixels);
        const bool shadow = tile->orientation == DT1_ORIENTATION_SHADOW;
        for (int i = 0; i < size; i++) {
            const uint8_t index = tile->image[i];
            if (index == 0) {
                pixels[i] = 0;
            } else if (shadow) {
                pixels[i] = 0x00000080; // shadows only darken what's under them
            } else {
                pixels[i] = palette->entries[index];
            }
        }
        texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, tile->image_width,
                                    tile->image_height);
        if (texture != NULL) {
            SDL_UpdateTexture(texture, NULL, pixels, tile->image_width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        free(pixels);
    }
    MapRenderer__Insert(renderer, tile, texture);
    return texture;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing

static bool MapRenderer__IsLowerWall(const int orientation) { return orientation >= DT1_ORIENTATION_LOWER_WALL; }

static bool MapRenderer__IsSpecial(const int orientation) {
    return orientation == DT1_ORIENTATION_SPECIAL_1 || orientation == DT1_ORIENTATION_SPECIAL_2;
}

// World position of a tile image's top-left corner
static void MapRenderer__TileOrigin(const DT1Tile *tile, const int tx, const int ty, float *x, float *y) {
    const float corner_x = (float)((tx - ty) * TILE_HALF_WIDTH);
    const float corner_y = (float)((tx + ty) * TILE_HALF_HEIGHT);

    *x = corner_x - TILE_HALF_WIDTH + (float)tile->image_x;
    if (tile->orientation == DT1_ORIENTATION_FLOOR || tile->orientation == DT1_ORIENTATION_SHADOW ||
        MapRenderer__IsLowerWall(tile->orientation)) {
        *y = corner_y + (float)tile->image_y;
    } else if (tile->orientation == DT1_ORIENTATION_ROOF) {
        *y = corner_y + (float)tile->image_y - (float)tile->roof_height;
    } else {
        *y = corner_y + WALL_BASE_Y + (float)tile->image_y;
    }
}

static void MapRenderer__DrawTile(MapRenderer *renderer, const Map *map, const MapCamera *camera, const SDL_Rect *view,
                                  const DT1Tile *tile, const int tx, const int ty) {
    if (tile == NULL) {
        return;
    }
    float wx;
    float wy;
    MapRenderer__TileOrigin(tile, tx, ty, &wx, &wy);

    int sx;
    int sy;
    MapView_WorldToScreen(camera, view, wx, wy, &sx, &sy);
    const int w = (int)ceilf((float)tile->image_width * camera->zoom);
    const int h = (int)ceilf((float)tile->image_height * camera->zoom);
    if (sx + w < view->x || sy + h < view->y || sx > view->x + view->w || sy > view->y + view->h) {
        return;
    }

    SDL_Texture *texture = MapRenderer__Texture(renderer, map, tile);
    if (texture != NULL) {
        const SDL_Rect dest = {sx, sy, w, h};
        SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
    }
}

// Visible tile range (with a margin for tall walls)
static void MapRenderer__VisibleRange(const Map *map, const MapCamera *camera, const SDL_Rect *view, int *min_sum,
                                      int *max_sum, int *min_diff, int *max_diff) {
    float left;
    float top;
    float right;
    float bottom;
    MapView_ScreenToWorld(camera, view, view->x, view->y, &left, &top);
    MapView_ScreenToWorld(camera, view, view->x + view->w, view->y + view->h, &right, &bottom);
    // tx + ty = y / 40, tx - ty = x / 80
    *min_sum  = (int)floorf(top / TILE_HALF_HEIGHT) - 2;
    *max_sum  = (int)ceilf(bottom / TILE_HALF_HEIGHT) + 12; // walls reach up to ~ 10 tiles above their base
    *min_diff = (int)floorf(left / TILE_HALF_WIDTH) - 2;
    *max_diff = (int)ceilf(right / TILE_HALF_WIDTH) + 2;
    (void)map;
}

static bool MapRenderer__InRange(const int tx, const int ty, const int min_sum, const int max_sum, const int min_diff,
                                 const int max_diff) {
    const int sum  = tx + ty;
    const int diff = tx - ty;
    return sum >= min_sum && sum <= max_sum && diff >= min_diff && diff <= max_diff;
}

static int MapRenderer__CompareDrawables(const void *a, const void *b) {
    const MapDrawable *x  = *(const MapDrawable *const *)a;
    const MapDrawable *y  = *(const MapDrawable *const *)b;
    const float        dx = x->x + x->y;
    const float        dy = y->x + y->y;
    return (dx < dy) ? -1 : (dx > dy);
}

static void MapRenderer__DrawBlocked(const Map *map, const MapCamera *camera, const SDL_Rect *view, const int tx,
                                     const int ty) {
    for (int sy = 0; sy < MAP_SUBTILES; sy++) {
        for (int sx = 0; sx < MAP_SUBTILES; sx++) {
            const int subtile_x = (tx * MAP_SUBTILES) + sx;
            const int subtile_y = (ty * MAP_SUBTILES) + sy;
            if (Map_IsWalkable(map, subtile_x, subtile_y)) {
                continue;
            }
            float wx;
            float wy;
            MapView_SubtileToWorld((float)subtile_x + 0.5f, (float)subtile_y + 0.5f, &wx, &wy);
            int cx;
            int cy;
            MapView_WorldToScreen(camera, view, wx, wy, &cx, &cy);
            const int      hw   = (int)(8 * camera->zoom) > 1 ? (int)(8 * camera->zoom) : 1;
            const int      hh   = hw / 2 > 0 ? hw / 2 : 1;
            const SDL_Rect mark = {cx - hw, cy - hh, hw * 2, hh * 2};
            SDL_RenderFillRect(sdl_renderer, &mark);
        }
    }
}

void MapRenderer_DrawTileOutline(const MapCamera *camera, const SDL_Rect *view, const int tx, const int ty,
                                 const SDL_Color color) {
    const float cx = (float)((tx - ty) * TILE_HALF_WIDTH);
    const float cy = (float)((tx + ty) * TILE_HALF_HEIGHT);
    SDL_Point   points[5];
    MapView_WorldToScreen(camera, view, cx, cy, &points[0].x, &points[0].y);
    MapView_WorldToScreen(camera, view, cx + TILE_HALF_WIDTH, cy + TILE_HALF_HEIGHT, &points[1].x, &points[1].y);
    MapView_WorldToScreen(camera, view, cx, cy + (2 * TILE_HALF_HEIGHT), &points[2].x, &points[2].y);
    MapView_WorldToScreen(camera, view, cx - TILE_HALF_WIDTH, cy + TILE_HALF_HEIGHT, &points[3].x, &points[3].y);
    points[4] = points[0];
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLines(sdl_renderer, points, 5);
}

void MapRenderer_Draw(MapRenderer *renderer, const Map *map, const MapCamera *camera, const SDL_Rect *view,
                      const MapDrawable *drawables, const int drawable_count, const unsigned flags) {
    int min_sum;
    int max_sum;
    int min_diff;
    int max_diff;
    MapRenderer__VisibleRange(map, camera, view, &min_sum, &max_sum, &min_diff, &max_diff);

    SDL_RenderSetClipRect(sdl_renderer, view);

    // 1. Floors (and the parts of walls below the floor line)
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            if (!MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                continue;
            }
            const int c = (ty * map->width) + tx;
            for (int f = 0; f < map->floor_layer_count; f++) {
                MapRenderer__DrawTile(renderer, map, camera, view, map->floor_tiles[f][c], tx, ty);
            }
            if (flags & MAP_DRAW_WALLS) {
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (tile != NULL && MapRenderer__IsLowerWall(tile->orientation)) {
                        MapRenderer__DrawTile(renderer, map, camera, view, tile, tx, ty);
                    }
                }
            }
        }
    }

    // 2. Shadows
    if (flags & MAP_DRAW_SHADOWS) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    MapRenderer__DrawTile(renderer, map, camera, view, map->shadow_tiles[(ty * map->width) + tx], tx,
                                          ty);
                }
            }
        }
    }

    if (flags & MAP_DRAW_GRID) {
        const SDL_Color grid = {255, 255, 255, 40};
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    MapRenderer_DrawTileOutline(camera, view, tx, ty, grid);
                }
            }
        }
    }

    if (flags & MAP_DRAW_BLOCKED) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 40, 40, 90);
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    MapRenderer__DrawBlocked(map, camera, view, tx, ty);
                }
            }
        }
    }

    // 3. Walls and entities, back to front: by diagonal (tx + ty), entities after the walls of their tile
    const MapDrawable **sorted = NULL;
    if (drawable_count > 0) {
        sorted = malloc(sizeof(MapDrawable *) * (size_t)drawable_count);
        FAIL_IF_NULL(sorted);
        for (int i = 0; i < drawable_count; i++) {
            sorted[i] = &drawables[i];
        }
        qsort(sorted, (size_t)drawable_count, sizeof(MapDrawable *), MapRenderer__CompareDrawables);
    }

    int next = 0;
    for (int sum = 0; sum <= (map->width - 1) + (map->height - 1); sum++) {
        if (flags & MAP_DRAW_WALLS) {
            for (int tx = 0; tx <= sum; tx++) {
                const int ty = sum - tx;
                if (tx >= map->width || ty < 0 || ty >= map->height ||
                    !MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    continue;
                }
                const int c = (ty * map->width) + tx;
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (tile == NULL || MapRenderer__IsLowerWall(tile->orientation) ||
                        tile->orientation == DT1_ORIENTATION_ROOF ||
                        (MapRenderer__IsSpecial(tile->orientation) && !(flags & MAP_DRAW_SPECIAL))) {
                        continue;
                    }
                    MapRenderer__DrawTile(renderer, map, camera, view, map->wall_extra_tiles[w][c], tx, ty);
                    MapRenderer__DrawTile(renderer, map, camera, view, tile, tx, ty);
                }
            }
        }

        // Entities standing on this diagonal
        while (next < drawable_count) {
            const MapDrawable *d        = sorted[next];
            const int          tile_sum = (int)floorf(d->x / MAP_SUBTILES) + (int)floorf(d->y / MAP_SUBTILES);
            if (tile_sum > sum && sum < (map->width - 1) + (map->height - 1)) {
                break;
            }
            float wx;
            float wy;
            MapView_SubtileToWorld(d->x, d->y, &wx, &wy);
            int sx;
            int sy;
            MapView_WorldToScreen(camera, view, wx, wy, &sx, &sy);
            d->draw(d->context, sx, sy, camera->zoom);
            next++;
        }
    }
    while (next < drawable_count) { // anything outside the map
        const MapDrawable *d = sorted[next++];
        float              wx;
        float              wy;
        MapView_SubtileToWorld(d->x, d->y, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(camera, view, wx, wy, &sx, &sy);
        d->draw(d->context, sx, sy, camera->zoom);
    }
    free(sorted);

    // 4. Roofs
    if ((flags & MAP_DRAW_WALLS) && (flags & MAP_DRAW_ROOFS)) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (!MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    continue;
                }
                const int c = (ty * map->width) + tx;
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (tile != NULL && tile->orientation == DT1_ORIENTATION_ROOF) {
                        MapRenderer__DrawTile(renderer, map, camera, view, tile, tx, ty);
                    }
                }
            }
        }
    }

    SDL_RenderSetClipRect(sdl_renderer, NULL);
}

void MapRenderer_DrawTilePreview(MapRenderer *renderer, const Map *map, const DT1Tile *tile, const SDL_Rect *box) {
    if (tile == NULL || tile->image_width <= 0 || tile->image_height <= 0) {
        return;
    }
    SDL_Texture *texture = MapRenderer__Texture(renderer, map, tile);
    if (texture == NULL) {
        return;
    }
    float scale = (float)box->w / (float)tile->image_width;
    if ((float)box->h / (float)tile->image_height < scale) {
        scale = (float)box->h / (float)tile->image_height;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    const int      w    = (int)((float)tile->image_width * scale);
    const int      h    = (int)((float)tile->image_height * scale);
    const SDL_Rect dest = {box->x + ((box->w - w) / 2), box->y + ((box->h - h) / 2), w, h};
    if (tile->orientation == DT1_ORIENTATION_SHADOW) {
        SDL_SetRenderDrawColor(sdl_renderer, 120, 120, 120, 255);
        SDL_RenderFillRect(sdl_renderer, &dest);
    }
    SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
}
