#include "MapRenderer.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../types/Palette.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TILE_HALF_WIDTH   80 // world pixels
#define TILE_HALF_HEIGHT  40
#define SUBTILE_HALF_W    16
#define SUBTILE_HALF_H    8
#define WALL_BASE_Y       80  // walls stand on the bottom corner of their floor diamond
#define ANIMATED_FLOOR_MS 100 // per frame of lava and other animated floors

typedef struct TextureEntry {
    const DT1Tile *tile;
    SDL_Texture   *texture;
} TextureEntry;

#define LIGHT_TEXTURE_W 256
#define LIGHT_TEXTURE_H 128 // an isometric ellipse, like the ground

#define ROOF_FADE_MS      250    // roofs fade out when you walk in, and back in when you leave
#define FRONT_WALL_FADE   150.0f // and the building's walls between you and the camera go this see-through (of 255)
#define SEE_THROUGH_ALPHA 100    // "See through obstructions": what's left of a wall or tree hiding you
#define WALL_FADE_MS      200    // how long a wall takes to fade out or back in (no popping as you walk)
#define SMALL_STRUCTURE   80     // a structure this many tiles or fewer (a tent, a house, a tree) fades as a whole

struct MapRenderer {
    bool see_through; // walls and trees hiding the viewer fade (MapRenderer_SetSeeThrough)

    // Roofs over the building the viewer is in (see MapRenderer_SetViewer)
    bool       has_viewer;
    float      viewer_x;
    float      viewer_y;
    const Map *roof_map;    // the map roof_hidden was worked out for
    int        roof_tile_x; // the tile it was worked out from, -1 = not under a roof
    int        roof_tile_y;
    uint8_t   *roof_hidden; // per tile: part of the roof being faded
    int        roof_cells;
    float      roof_fade; // 0 = drawn, 1 = gone
    uint64_t   roof_tick;

    // Structures: wall tiles joined edge to edge (a house, a tent, a tree, a stretch of wall), so a building fades
    // as one, not tile by tile. Worked out once per map.
    const Map *structure_map;
    int        structure_cells;
    int       *structure;      // per tile: its structure, -1 = no walls
    int       *structure_size; // per structure: tiles
    int        structure_count;
    uint8_t   *structure_fade; // per structure, this frame: 0 = drawn, else the alpha to fade to
    uint8_t   *wall_alpha;     // per tile: the alpha it's drawn with now, easing towards where it's going
    uint64_t   wall_tick;

    SDL_Texture   *light; // white with alpha: transparent in the middle, opaque at the edge
    uint8_t        light_darkness;
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
    if ((*renderer)->light != NULL) {
        SDL_DestroyTexture((*renderer)->light);
    }
    MapRenderer_ClearCache(*renderer);
    free((*renderer)->roof_hidden);
    free((*renderer)->structure);
    free((*renderer)->structure_size);
    free((*renderer)->structure_fade);
    free((*renderer)->wall_alpha);
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

static void MapRenderer__DrawTileAlpha(MapRenderer *renderer, const Map *map, const MapCamera *camera,
                                       const SDL_Rect *view, const DT1Tile *tile, int tx, int ty, uint8_t alpha);

static void MapRenderer__DrawTile(MapRenderer *renderer, const Map *map, const MapCamera *camera, const SDL_Rect *view,
                                  const DT1Tile *tile, const int tx, const int ty) {
    MapRenderer__DrawTileAlpha(renderer, map, camera, view, tile, tx, ty, 255);
}

static void MapRenderer__DrawTileAlpha(MapRenderer *renderer, const Map *map, const MapCamera *camera,
                                       const SDL_Rect *view, const DT1Tile *tile, const int tx, const int ty,
                                       const uint8_t alpha) {
    if (alpha == 0) {
        return;
    }
    if (tile == NULL) {
        return;
    }
    if (tile->anim_frame_count > 1) {
        tile = tile->anim_frames[(SDL_GetTicks64() / ANIMATED_FLOOR_MS) % (uint64_t)tile->anim_frame_count];
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
        if (alpha < 255) {
            SDL_SetTextureAlphaMod(texture, alpha);
        }
        SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
        if (alpha < 255) {
            SDL_SetTextureAlphaMod(texture, 255); // textures are shared between tiles
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Roofs

static bool MapRenderer__HasRoof(const Map *map, const int tx, const int ty) {
    if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height) {
        return false;
    }
    const int c = (ty * map->width) + tx;
    for (int w = 0; w < map->wall_layer_count; w++) {
        const DT1Tile *tile = map->wall_tiles[w][c];
        if (tile != NULL && tile->orientation == DT1_ORIENTATION_ROOF) {
            return true;
        }
    }
    return false;
}

// A tile of the building the viewer is in, or right next to it (its walls stand on its edge)
static bool MapRenderer__NearBuilding(const MapRenderer *renderer, const Map *map, const int tx, const int ty) {
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int x = tx + dx;
            const int y = ty + dy;
            if (x >= 0 && y >= 0 && x < map->width && y < map->height && renderer->roof_hidden[(y * map->width) + x]) {
                return true;
            }
        }
    }
    return false;
}

// A wall tile drawn in the walls pass (or a roof): what structures are made of
static bool MapRenderer__IsStructureTile(const DT1Tile *tile) {
    return tile != NULL && !MapRenderer__IsLowerWall(tile->orientation) && !MapRenderer__IsSpecial(tile->orientation);
}

static void MapRenderer__UpdateStructures(MapRenderer *renderer, const Map *map) {
    const int cells = map->width * map->height;
    if (renderer->structure_map == map && renderer->structure_cells == cells) {
        return;
    }
    free(renderer->structure);
    free(renderer->structure_size);
    free(renderer->structure_fade);
    free(renderer->wall_alpha);
    renderer->structure_map   = map;
    renderer->structure_cells = cells;
    renderer->structure       = malloc(sizeof(int) * (size_t)cells);
    renderer->wall_alpha      = malloc((size_t)cells);
    FAIL_IF_NULL(renderer->structure);
    FAIL_IF_NULL(renderer->wall_alpha);
    memset(renderer->wall_alpha, 255, (size_t)cells);
    for (int c = 0; c < cells; c++) {
        renderer->structure[c] = -1;
    }
    int  count = 0;
    int *sizes = NULL;
    int *queue = malloc(sizeof(int) * (size_t)cells);
    FAIL_IF_NULL(queue);
    for (int start = 0; start < cells; start++) {
        bool has = false;
        for (int w = 0; w < map->wall_layer_count && !has; w++) {
            has = MapRenderer__IsStructureTile(map->wall_tiles[w][start]);
        }
        if (!has || renderer->structure[start] >= 0) {
            continue;
        }
        int head                   = 0;
        int tail                   = 0;
        queue[tail++]              = start;
        renderer->structure[start] = count;
        while (head < tail) {
            const int c          = queue[head++];
            const int cx         = c % map->width;
            const int cy         = c / map->width;
            const int next[4][2] = {
                {cx + 1, cy    },
                {cx - 1, cy    },
                {cx,     cy + 1},
                {cx,     cy - 1}
            };
            for (int n = 0; n < 4; n++) {
                const int nx = next[n][0];
                const int ny = next[n][1];
                if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                    continue;
                }
                const int nc = (ny * map->width) + nx;
                if (renderer->structure[nc] >= 0) {
                    continue;
                }
                bool joined = false;
                for (int w = 0; w < map->wall_layer_count && !joined; w++) {
                    joined = MapRenderer__IsStructureTile(map->wall_tiles[w][nc]);
                }
                if (joined) {
                    renderer->structure[nc] = count;
                    queue[tail++]           = nc;
                }
            }
        }
        sizes = realloc(sizes, sizeof(int) * (size_t)(count + 1));
        FAIL_IF_NULL(sizes);
        sizes[count++] = tail;
    }
    free(queue);
    renderer->structure_size  = sizes;
    renderer->structure_count = count;
    renderer->structure_fade  = calloc(count > 0 ? (size_t)count : 1, 1);
    FAIL_IF_NULL(renderer->structure_fade);
}

// Whether a wall tile's picture really hides the viewer: its pixels (not just its box) over a few points of their body
static bool MapRenderer__Covers(const DT1Tile *tile, const int tx, const int ty, const float viewer_wx,
                                const float viewer_wy) {
    if (tile->image == NULL) {
        return false;
    }
    float wx;
    float wy;
    MapRenderer__TileOrigin(tile, tx, ty, &wx, &wy);
    static const int heights[5] = {12, 30, 50, 70, 90}; // feet to head, world pixels above where they stand
    static const int sides[3]   = {-10, 0, 10};
    int              hits       = 0;
    for (int h = 0; h < 5; h++) {
        for (int x = 0; x < 3; x++) {
            const int ix = (int)(viewer_wx + (float)sides[x] - wx);
            const int iy = (int)(viewer_wy - (float)heights[h] - wy);
            if (ix >= 0 && iy >= 0 && ix < tile->image_width && iy < tile->image_height &&
                tile->image[(iy * tile->image_width) + ix] != 0) {
                hits++;
            }
        }
    }
    return hits >= 3;
}

// Eases a tile's alpha towards where it's going; returns what to draw it with
static uint8_t MapRenderer__EaseWall(MapRenderer *renderer, const int c, const uint8_t target, const int step) {
    int alpha               = renderer->wall_alpha[c];
    alpha                   = alpha < target ? (alpha + step > target ? target : alpha + step)
                                             : (alpha - step < target ? target : alpha - step);
    renderer->wall_alpha[c] = (uint8_t)alpha;
    return (uint8_t)alpha;
}

void MapRenderer_SetSeeThrough(MapRenderer *renderer, const bool see_through) { renderer->see_through = see_through; }

void MapRenderer_SetViewer(MapRenderer *renderer, const float subtile_x, const float subtile_y) {
    renderer->has_viewer = subtile_x >= 0.0f && subtile_y >= 0.0f;
    renderer->viewer_x   = subtile_x;
    renderer->viewer_y   = subtile_y;
}

// The roof over the viewer: every roof tile joined to the one above them (one building's roof)
static void MapRenderer__UpdateRoofs(MapRenderer *renderer, const Map *map) {
    const uint64_t now  = SDL_GetTicks64();
    const float    step = renderer->roof_tick == 0 ? 0.0f : (float)(now - renderer->roof_tick) / ROOF_FADE_MS;
    renderer->roof_tick = now;

    const int cells = map->width * map->height;
    if (renderer->roof_map != map || renderer->roof_cells != cells) {
        free(renderer->roof_hidden);
        renderer->roof_hidden = calloc((size_t)cells, 1);
        FAIL_IF_NULL(renderer->roof_hidden);
        renderer->roof_cells  = cells;
        renderer->roof_map    = map;
        renderer->roof_tile_x = -1;
        renderer->roof_fade   = 0.0f;
    }

    int tx = -1;
    int ty = -1;
    if (renderer->has_viewer) {
        tx = (int)floorf(renderer->viewer_x / MAP_SUBTILES);
        ty = (int)floorf(renderer->viewer_y / MAP_SUBTILES);
        if (!MapRenderer__HasRoof(map, tx, ty)) {
            tx = -1;
        }
    }
    const bool under = tx >= 0;
    const bool same  = under && renderer->roof_tile_x >= 0 && renderer->roof_hidden[(ty * map->width) + tx] != 0;

    if (under && !same) {
        // Walked under a roof (or under another one): flood-fill it
        memset(renderer->roof_hidden, 0, (size_t)cells);
        int *queue = malloc(sizeof(int) * (size_t)cells);
        FAIL_IF_NULL(queue);
        int head                                      = 0;
        int tail                                      = 0;
        queue[tail++]                                 = (ty * map->width) + tx;
        renderer->roof_hidden[(ty * map->width) + tx] = 1;
        while (head < tail) {
            const int c          = queue[head++];
            const int cx         = c % map->width;
            const int cy         = c / map->width;
            const int next[4][2] = {
                {cx + 1, cy    },
                {cx - 1, cy    },
                {cx,     cy + 1},
                {cx,     cy - 1}
            };
            for (int n = 0; n < 4; n++) {
                const int nx = next[n][0];
                const int ny = next[n][1];
                if (MapRenderer__HasRoof(map, nx, ny) && renderer->roof_hidden[(ny * map->width) + nx] == 0) {
                    renderer->roof_hidden[(ny * map->width) + nx] = 1;
                    queue[tail++]                                 = (ny * map->width) + nx;
                }
            }
        }
        free(queue);
        renderer->roof_tile_x = tx;
        renderer->roof_tile_y = ty;
    }
    if (under) {
        renderer->roof_tile_x = tx;
        renderer->roof_tile_y = ty;
        renderer->roof_fade   = fminf(renderer->roof_fade + step, 1.0f);
    } else {
        renderer->roof_fade = fmaxf(renderer->roof_fade - step, 0.0f);
        if (renderer->roof_fade <= 0.0f) {
            renderer->roof_tile_x = -1;
        }
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
            // Blue: an object standing there (chest, closed door, ...); red: the map's own tiles (walls, cliffs, water)
            if (Map_IsObjectBlocked(map, subtile_x, subtile_y)) {
                SDL_SetRenderDrawColor(sdl_renderer, 60, 140, 255, 130);
            } else {
                SDL_SetRenderDrawColor(sdl_renderer, 255, 40, 40, 90);
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

    // The building the viewer is in: roof fading away, walls in front of them see-through (worked out before the walls)
    if ((flags & MAP_DRAW_WALLS) && (flags & MAP_DRAW_ROOFS)) {
        MapRenderer__UpdateRoofs(renderer, map);
    }
    const bool    inside      = (flags & MAP_DRAW_ROOFS) && renderer->roof_tile_x >= 0;
    const int     viewer_sum  = inside ? renderer->roof_tile_x + renderer->roof_tile_y : 0;
    const uint8_t front_alpha = (uint8_t)(255.0f - (FRONT_WALL_FADE * renderer->roof_fade));

    // See through obstructions: where the viewer stands (world pixels), and the diagonal they're on
    const bool see_through = renderer->see_through && renderer->has_viewer;
    const int  viewer_tile_sum =
        see_through ? (int)floorf(renderer->viewer_x / MAP_SUBTILES) + (int)floorf(renderer->viewer_y / MAP_SUBTILES)
                     : 0;
    float viewer_wx = 0.0f;
    float viewer_wy = 0.0f;
    if (see_through) {
        MapView_SubtileToWorld(renderer->viewer_x, renderer->viewer_y, &viewer_wx, &viewer_wy);
    }

    // Which structures fade this frame: the building you're in (its walls in front of you), and with "see through
    // obstructions" anything small (a tent, a tree, a house) with a tile in front of you that covers you, all of it
    enum { FADE_BUILDING = 1, FADE_COVERING = 2 };
    MapRenderer__UpdateStructures(renderer, map);
    memset(renderer->structure_fade, 0, renderer->structure_count > 0 ? (size_t)renderer->structure_count : 1);
    if ((flags & MAP_DRAW_WALLS) && (inside || see_through)) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                const int c = (ty * map->width) + tx;
                const int k = renderer->structure[c];
                if (k < 0 || !MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    continue;
                }
                if (inside && MapRenderer__NearBuilding(renderer, map, tx, ty)) {
                    renderer->structure_fade[k] |= FADE_BUILDING;
                }
                if (see_through && tx + ty > viewer_tile_sum && renderer->structure_size[k] <= SMALL_STRUCTURE &&
                    !(renderer->structure_fade[k] & FADE_COVERING)) {
                    for (int w = 0; w < map->wall_layer_count; w++) {
                        const DT1Tile *tile = map->wall_tiles[w][c];
                        // The roof you're under is already going (and doesn't make its walls hide you)
                        if (MapRenderer__IsStructureTile(tile) &&
                            !(tile->orientation == DT1_ORIENTATION_ROOF && inside && renderer->roof_hidden[c]) &&
                            MapRenderer__Covers(tile, tx, ty, viewer_wx, viewer_wy)) {
                            renderer->structure_fade[k] |= FADE_COVERING;
                            break;
                        }
                    }
                }
            }
        }
    }
    const uint64_t now  = SDL_GetTicks64();
    int            step = renderer->wall_tick == 0 ? 255 : (int)((now - renderer->wall_tick) * 255 / WALL_FADE_MS);
    step                = step < 1 ? 1 : (step > 255 ? 255 : step);
    renderer->wall_tick = now;

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
                const int k = renderer->structure[c];
                if (k < 0) {
                    continue;
                }
                // Where this tile's alpha is going: whole structures for buildings and small things; tile by tile
                // for long walls, tree lines and cliffs (fading all of those would empty the screen)
                uint8_t target = 255;
                if ((renderer->structure_fade[k] & FADE_BUILDING) && sum > viewer_sum) {
                    target = front_alpha;
                }
                if (renderer->structure_fade[k] & FADE_COVERING) {
                    target = target < SEE_THROUGH_ALPHA ? target : SEE_THROUGH_ALPHA;
                } else if (see_through && sum > viewer_tile_sum && renderer->structure_size[k] > SMALL_STRUCTURE) {
                    for (int w = 0; w < map->wall_layer_count; w++) {
                        const DT1Tile *tile = map->wall_tiles[w][c];
                        if (MapRenderer__IsStructureTile(tile) &&
                            MapRenderer__Covers(tile, tx, ty, viewer_wx, viewer_wy)) {
                            target = target < SEE_THROUGH_ALPHA ? target : SEE_THROUGH_ALPHA;
                            break;
                        }
                    }
                }
                const uint8_t alpha = MapRenderer__EaseWall(renderer, c, target, step);
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (tile == NULL || MapRenderer__IsLowerWall(tile->orientation) ||
                        tile->orientation == DT1_ORIENTATION_ROOF ||
                        (MapRenderer__IsSpecial(tile->orientation) && !(flags & MAP_DRAW_SPECIAL))) {
                        continue;
                    }
                    MapRenderer__DrawTileAlpha(renderer, map, camera, view, map->wall_extra_tiles[w][c], tx, ty, alpha);
                    MapRenderer__DrawTileAlpha(renderer, map, camera, view, tile, tx, ty, alpha);
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

    // 4. Roofs (the one over the viewer fades away)
    if ((flags & MAP_DRAW_WALLS) && (flags & MAP_DRAW_ROOFS)) {
        const uint8_t hidden_alpha = (uint8_t)(255.0f * (1.0f - renderer->roof_fade));
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (!MapRenderer__InRange(tx, ty, min_sum, max_sum, min_diff, max_diff)) {
                    continue;
                }
                const int c = (ty * map->width) + tx;
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (tile != NULL && tile->orientation == DT1_ORIENTATION_ROOF) {
                        const bool hidden = renderer->roof_tile_x >= 0 && renderer->roof_hidden[c] != 0;
                        uint8_t    alpha  = hidden ? hidden_alpha : 255;
                        // As faded as the rest of its structure (a tent covering you fades, roof and all)
                        alpha = alpha < renderer->wall_alpha[c] ? alpha : renderer->wall_alpha[c];
                        MapRenderer__DrawTileAlpha(renderer, map, camera, view, tile, tx, ty, alpha);
                    }
                }
            }
        }
    }

    // 5. Collision view, on top of everything so walls don't hide it
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
        // Exits: green outline = the way to the next area on foot, yellow = an entrance (cave mouth, door, stairs)
        for (int e = 0; e < map->exit_count; e++) {
            const MapExit *exit          = &map->exits[e];
            float          corners[4][2] = {
                {(float)exit->x0,        (float)exit->y0       },
                {(float)exit->x1 + 1.0f, (float)exit->y0       },
                {(float)exit->x1 + 1.0f, (float)exit->y1 + 1.0f},
                {(float)exit->x0,        (float)exit->y1 + 1.0f},
            };
            SDL_Point points[5];
            for (int k = 0; k < 4; k++) {
                float wx;
                float wy;
                MapView_SubtileToWorld(corners[k][0], corners[k][1], &wx, &wy);
                MapView_WorldToScreen(camera, view, wx, wy, &points[k].x, &points[k].y);
            }
            points[4] = points[0];
            if (exit->entrance) {
                SDL_SetRenderDrawColor(sdl_renderer, 255, 220, 40, 230);
            } else {
                SDL_SetRenderDrawColor(sdl_renderer, 60, 230, 90, 230);
            }
            SDL_RenderDrawLines(sdl_renderer, points, 5);
            for (int k = 0; k < 5; k++) { // twice as thick
                points[k].y += 1;
            }
            SDL_RenderDrawLines(sdl_renderer, points, 5);
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

void MapRenderer_DrawLight(MapRenderer *renderer, const MapCamera *camera, const SDL_Rect *view, const float world_x,
                           const float world_y, const float radius, const uint8_t darkness) {
    if (darkness == 0) {
        return;
    }
    if (renderer->light == NULL || renderer->light_darkness != darkness) {
        if (renderer->light != NULL) {
            SDL_DestroyTexture(renderer->light);
        }
        uint32_t *pixels = malloc(sizeof(uint32_t) * LIGHT_TEXTURE_W * LIGHT_TEXTURE_H);
        FAIL_IF_NULL(pixels);
        for (int y = 0; y < LIGHT_TEXTURE_H; y++) {
            for (int x = 0; x < LIGHT_TEXTURE_W; x++) {
                const float dx = ((float)x + 0.5f - (LIGHT_TEXTURE_W / 2.0f)) / (LIGHT_TEXTURE_W / 2.0f);
                const float dy = ((float)y + 0.5f - (LIGHT_TEXTURE_H / 2.0f)) / (LIGHT_TEXTURE_H / 2.0f);
                float       d  = sqrtf((dx * dx) + (dy * dy)); // 0 in the middle, 1 at the edge
                d              = d > 1.0f ? 1.0f : d;
                const float t  = d * d * (3.0f - (2.0f * d));                        // smooth fall-off
                pixels[(y * LIGHT_TEXTURE_W) + x] = (uint32_t)(t * (float)darkness); // RGBA8888: black, alpha
            }
        }
        renderer->light = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                            LIGHT_TEXTURE_W, LIGHT_TEXTURE_H);
        if (renderer->light != NULL) {
            SDL_UpdateTexture(renderer->light, NULL, pixels, LIGHT_TEXTURE_W * 4);
            SDL_SetTextureBlendMode(renderer->light, SDL_BLENDMODE_BLEND);
        }
        renderer->light_darkness = darkness;
        free(pixels);
    }

    int cx;
    int cy;
    MapView_WorldToScreen(camera, view, world_x, world_y, &cx, &cy);
    const int      half_w = (int)(radius * SUBTILE_HALF_W * 2.0f * camera->zoom);
    const int      half_h = half_w / 2;
    const SDL_Rect pool   = {cx - half_w, cy - half_h, half_w * 2, half_h * 2};

    SDL_RenderSetClipRect(sdl_renderer, view);
    if (renderer->light != NULL) {
        SDL_RenderCopy(sdl_renderer, renderer->light, NULL, &pool);
    }
    // Everything outside the pool is fully dark
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, darkness);
    const SDL_Rect above    = {view->x, view->y, view->w, pool.y - view->y};
    const SDL_Rect below    = {view->x, pool.y + pool.h, view->w, (view->y + view->h) - (pool.y + pool.h)};
    const SDL_Rect left     = {view->x, pool.y, pool.x - view->x, pool.h};
    const SDL_Rect right    = {pool.x + pool.w, pool.y, (view->x + view->w) - (pool.x + pool.w), pool.h};
    const SDL_Rect sides[4] = {above, below, left, right};
    for (int i = 0; i < 4; i++) {
        if (sides[i].w > 0 && sides[i].h > 0) {
            SDL_RenderFillRect(sdl_renderer, &sides[i]);
        }
    }
    SDL_RenderSetClipRect(sdl_renderer, NULL);
}
