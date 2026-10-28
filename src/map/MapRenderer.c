#include "MapRenderer.h"
#include "../common/FileManager.h"
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

#define ROOF_FADE_MS       250    // roofs fade out when you walk in, and back in when you leave
#define FRONT_WALL_FADE    150.0f // and the building's walls between you and the camera go this see-through (of 255)
#define SEE_THROUGH_ALPHA  70     // "See through obstructions": what's left of a wall or tree hiding you (in the hole)
#define SEE_THROUGH_RADIUS 64     // the hole's radius, screen pixels at zoom 1 (with a soft ring a quarter as wide)
#define WALL_FADE_MS       200    // how long a wall takes to fade out or back in (no popping as you walk)

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
    uint8_t   *cover;          // per tile, this frame: its picture covers the viewer (see through obstructions)
    bool       cut;            // see through obstructions: a round hole around the viewer, centre and radius on screen
    int        cut_x;
    int        cut_y;
    int        cut_radius;
    uint64_t   wall_tick;

    SDL_Texture *light; // white with alpha: transparent in the middle, opaque at the edge
    uint8_t      light_darkness;
    SDL_Texture *dark_mask; // MapRenderer_DrawLights: the darkness at a quarter of the view's size
    // The dark in the act's colours: for each amount of darkness (0..255), what each of red, green and blue is
    // multiplied by, from the palette's light levels (pal.pl2), so the shadows go the way the game's do
    const Palette *shade_palette;
    uint8_t        shade[256][3];
    int            dark_w;
    int            dark_h;
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
    renderer->structure_map = NULL; // a new map can come at the same address: work the structures out again
    renderer->roof_map      = NULL;
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
    if ((*renderer)->dark_mask != NULL) {
        SDL_DestroyTexture((*renderer)->dark_mask);
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
    free((*renderer)->cover);
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
    const Palette *palette = Palette_Get(MapView_ActPalette(map->palette >= 0 ? map->palette : map->act));
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

// One clipped part of a tile (the clip within the view)
static void MapRenderer__DrawTileClipped(MapRenderer *renderer, const Map *map, const MapCamera *camera,
                                         const SDL_Rect *view, const DT1Tile *tile, const int tx, const int ty,
                                         const uint8_t alpha, const SDL_Rect *clip) {
    SDL_Rect part;
    if (clip->w <= 0 || clip->h <= 0 || !SDL_IntersectRect(clip, view, &part)) {
        return;
    }
    SDL_RenderSetClipRect(sdl_renderer, &part);
    MapRenderer__DrawTileAlpha(renderer, map, camera, view, tile, tx, ty, alpha);
}

// A tile with a round hole where it covers the viewer: "inside" alpha in the circle, "outside" beyond it, half way
// between in a ring round its edge (drawn in thin bands, each clipped to the parts of the circle it crosses)
static void MapRenderer__DrawTileCut(MapRenderer *renderer, const Map *map, const MapCamera *camera,
                                     const SDL_Rect *view, const DT1Tile *tile, const int tx, const int ty,
                                     const uint8_t inside, const uint8_t outside) {
    if (tile == NULL) {
        return;
    }
    if (inside >= outside || !renderer->cut) {
        MapRenderer__DrawTileAlpha(renderer, map, camera, view, tile, tx, ty, inside < outside ? inside : outside);
        return;
    }
    float wx;
    float wy;
    MapRenderer__TileOrigin(tile, tx, ty, &wx, &wy);
    int sx;
    int sy;
    MapView_WorldToScreen(camera, view, wx, wy, &sx, &sy);
    const int     w      = (int)ceilf((float)tile->image_width * camera->zoom);
    const int     h      = (int)ceilf((float)tile->image_height * camera->zoom);
    const int     cx     = renderer->cut_x;
    const int     cy     = renderer->cut_y;
    const int     inner  = renderer->cut_radius;
    const int     outer  = inner + (inner / 4); // the soft ring
    const uint8_t ring   = (uint8_t)((inside + outside) / 2);
    const int     top    = cy - outer;
    const int     bottom = cy + outer;
    // Above and below the circle: as it is
    const SDL_Rect above = {sx, sy, w, top - sy};
    const SDL_Rect below = {sx, bottom, w, sy + h - bottom};
    MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, outside, &above);
    MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, outside, &below);
    const int band = 4;
    for (int y = (top > sy ? top : sy); y < bottom && y < sy + h; y += band) {
        const float    dy    = (float)(y + (band / 2) - cy);
        const int      ro    = (float)(outer * outer) > dy * dy ? (int)sqrtf((float)(outer * outer) - (dy * dy)) : 0;
        const int      ri    = (float)(inner * inner) > dy * dy ? (int)sqrtf((float)(inner * inner) - (dy * dy)) : 0;
        const SDL_Rect left  = {sx, y, (cx - ro) - sx, band};
        const SDL_Rect right = {cx + ro, y, (sx + w) - (cx + ro), band};
        const SDL_Rect ring_left  = {cx - ro, y, ro - ri, band};
        const SDL_Rect ring_right = {cx + ri, y, ro - ri, band};
        const SDL_Rect middle     = {cx - ri, y, 2 * ri, band};
        MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, outside, &left);
        MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, outside, &right);
        MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, ring, &ring_left);
        MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, ring, &ring_right);
        MapRenderer__DrawTileClipped(renderer, map, camera, view, tile, tx, ty, inside, &middle);
    }
    SDL_RenderSetClipRect(sdl_renderer, view);
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
    free(renderer->cover);
    renderer->structure_map   = map;
    renderer->structure_cells = cells;
    renderer->structure       = malloc(sizeof(int) * (size_t)cells);
    renderer->wall_alpha      = malloc((size_t)cells);
    renderer->cover           = calloc((size_t)cells, 1);
    FAIL_IF_NULL(renderer->cover);
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

// A wall that stands (not a roof, tree, floor, shadow or marker): what a building's footprint is made of
static bool MapRenderer__IsStandingWall(const DT1Tile *tile) {
    return tile != NULL && tile->orientation != DT1_ORIENTATION_FLOOR && tile->orientation != DT1_ORIENTATION_ROOF &&
           tile->orientation != DT1_ORIENTATION_TREE && tile->orientation != DT1_ORIENTATION_SHADOW &&
           !MapRenderer__IsSpecial(tile->orientation);
}

// The building the viewer is inside, as one of its roof tiles (rx, ry); false if they aren't inside one. A roof's
// tiles sit a little north-west of the walls they cover (they're drawn high up), so standing on one isn't being
// inside: the viewer has to be within the walls around the roof (the box of its walls).
static bool MapRenderer__RoofOver(const Map *map, const int tx, const int ty, int *rx, int *ry) {
    static uint8_t seen[64 * 64];
    memset(seen, 0, sizeof(seen));
    for (int dy = -4; dy <= 2; dy++) {
        for (int dx = -4; dx <= 2; dx++) {
            const int sx = tx + dx;
            const int sy = ty + dy;
            if (!MapRenderer__HasRoof(map, sx, sy) || seen[((dy + 32) * 64) + dx + 32]) {
                continue;
            }
            // This roof: its tiles (flood fill, kept small) and their box
            int queue[256][2];
            int head       = 0;
            int tail       = 0;
            int x0         = sx;
            int y0         = sy;
            int x1         = sx;
            int y1         = sy;
            queue[tail][0] = sx;
            queue[tail][1] = sy;
            tail++;
            seen[((dy + 32) * 64) + dx + 32] = 1;
            while (head < tail) {
                const int cx = queue[head][0];
                const int cy = queue[head][1];
                head++;
                x0                   = cx < x0 ? cx : x0;
                y0                   = cy < y0 ? cy : y0;
                x1                   = cx > x1 ? cx : x1;
                y1                   = cy > y1 ? cy : y1;
                const int next[4][2] = {
                    {cx + 1, cy    },
                    {cx - 1, cy    },
                    {cx,     cy + 1},
                    {cx,     cy - 1}
                };
                for (int n = 0; n < 4 && tail < 256; n++) {
                    const int lx = next[n][0] - tx + 32;
                    const int ly = next[n][1] - ty + 32;
                    if (lx < 0 || ly < 0 || lx >= 64 || ly >= 64 || seen[(ly * 64) + lx] ||
                        !MapRenderer__HasRoof(map, next[n][0], next[n][1])) {
                        continue;
                    }
                    seen[(ly * 64) + lx] = 1;
                    queue[tail][0]       = next[n][0];
                    queue[tail][1]       = next[n][1];
                    tail++;
                }
            }
            // The walls under it and along its edges
            int wx0 = 1 << 30;
            int wy0 = 1 << 30;
            int wx1 = -1;
            int wy1 = -1;
            for (int y = y0 - 1; y <= y1 + 1; y++) {
                for (int x = x0 - 1; x <= x1 + 1; x++) {
                    if (x < 0 || y < 0 || x >= map->width || y >= map->height) {
                        continue;
                    }
                    for (int w = 0; w < map->wall_layer_count; w++) {
                        if (MapRenderer__IsStandingWall(map->wall_tiles[w][(y * map->width) + x])) {
                            wx0 = x < wx0 ? x : wx0;
                            wy0 = y < wy0 ? y : wy0;
                            wx1 = x > wx1 ? x : wx1;
                            wy1 = y > wy1 ? y : wy1;
                            break;
                        }
                    }
                }
            }
            // Inside: a wall stands on its cell's north-west edges, so the walls' box runs from the first wall cell
            // up to (not into) the last one
            if (wx1 >= 0 && tx >= wx0 && tx < wx1 && ty >= wy0 && ty < wy1) {
                *rx = sx;
                *ry = sy;
                return true;
            }
        }
    }
    return false;
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
        int rx;
        int ry;
        if (MapRenderer__RoofOver(map, tx, ty, &rx, &ry)) {
            tx = rx; // flood the roof from one of its tiles
            ty = ry;
        } else {
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
    const MapDrawable *x = *(const MapDrawable *const *)a;
    const MapDrawable *y = *(const MapDrawable *const *)b;
    // By the tile diagonal they're on (they're drawn with its walls), then what lies on the floor before what stands
    // there, then front to back
    const int tx = (int)floorf(x->x / MAP_SUBTILES) + (int)floorf(x->y / MAP_SUBTILES);
    const int ty = (int)floorf(y->x / MAP_SUBTILES) + (int)floorf(y->y / MAP_SUBTILES);
    if (tx != ty) {
        return tx < ty ? -1 : 1;
    }
    if (x->on_floor != y->on_floor) {
        return x->on_floor ? -1 : 1;
    }
    const float dx = x->x + x->y;
    const float dy = y->x + y->y;
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
    const bool inside = (flags & MAP_DRAW_ROOFS) && renderer->roof_tile_x >= 0;
    const int  viewer_sum =
        inside ? (int)floorf(renderer->viewer_x / MAP_SUBTILES) + (int)floorf(renderer->viewer_y / MAP_SUBTILES) : 0;
    const uint8_t front_alpha = (uint8_t)(255.0f - (FRONT_WALL_FADE * renderer->roof_fade));

    // See through obstructions: where the viewer stands (world pixels), and the diagonal they're on
    const bool see_through = renderer->see_through && renderer->has_viewer;
    const int  viewer_tile_sum =
        see_through ? (int)floorf(renderer->viewer_x / MAP_SUBTILES) + (int)floorf(renderer->viewer_y / MAP_SUBTILES)
                     : 0;
    float viewer_wx = 0.0f;
    float viewer_wy = 0.0f;
    renderer->cut   = see_through;
    if (see_through) {
        MapView_SubtileToWorld(renderer->viewer_x, renderer->viewer_y, &viewer_wx, &viewer_wy);
        // The hole: round the viewer's body (half way up a character), on screen
        MapView_WorldToScreen(camera, view, viewer_wx, viewer_wy - 45.0f, &renderer->cut_x, &renderer->cut_y);
        renderer->cut_radius = (int)(SEE_THROUGH_RADIUS * camera->zoom);
    }

    // What fades this frame: the building you're in (its walls in front of you, as one), and with "see through
    // obstructions" the tiles whose pictures really cover you (and their neighbours in the same structure, so it's a
    // window rather than a hole), wall by wall; roofs covering you whichever side of them you are
    enum { FADE_BUILDING = 1 };
    MapRenderer__UpdateStructures(renderer, map);
    memset(renderer->structure_fade, 0, renderer->structure_count > 0 ? (size_t)renderer->structure_count : 1);
    memset(renderer->cover, 0, (size_t)renderer->structure_cells);
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
                if (!see_through) {
                    continue;
                }
                for (int w = 0; w < map->wall_layer_count; w++) {
                    const DT1Tile *tile = map->wall_tiles[w][c];
                    if (!MapRenderer__IsStructureTile(tile)) {
                        continue;
                    }
                    const bool roof = tile->orientation == DT1_ORIENTATION_ROOF;
                    // Walls behind you are drawn before you; the roof you're under is already going
                    if ((roof ? (inside && renderer->roof_hidden[c]) : tx + ty <= viewer_tile_sum)) {
                        continue;
                    }
                    if (MapRenderer__Covers(tile, tx, ty, viewer_wx, viewer_wy)) {
                        renderer->cover[c] = 1;
                        break;
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
                const int k = renderer->structure[c]; // -1: no walls (or the structures aren't worked out yet)
                // Where this tile's alpha is going
                uint8_t target = 255;
                if (k >= 0 && (renderer->structure_fade[k] & FADE_BUILDING) && sum > viewer_sum) {
                    target = front_alpha;
                }
                const uint8_t base = target; // without see through: outside the hole
                if (k >= 0 && see_through) {
                    bool             covered    = renderer->cover[c] != 0;
                    static const int next[4][2] = {
                        {1,  0 },
                        {-1, 0 },
                        {0,  1 },
                        {0,  -1}
                    };
                    for (int n = 0; n < 4 && !covered && sum > viewer_tile_sum; n++) {
                        const int nx = tx + next[n][0];
                        const int ny = ty + next[n][1];
                        if (nx >= 0 && ny >= 0 && nx < map->width && ny < map->height) {
                            const int nc = (ny * map->width) + nx;
                            covered      = renderer->cover[nc] != 0 && renderer->structure[nc] == k;
                        }
                    }
                    if (covered) {
                        target = target < SEE_THROUGH_ALPHA ? target : SEE_THROUGH_ALPHA;
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
                    // See through obstructions: a round hole around the viewer (it opens and closes with the easing)
                    MapRenderer__DrawTileCut(renderer, map, camera, view, map->wall_extra_tiles[w][c], tx, ty, alpha,
                                             base);
                    MapRenderer__DrawTileCut(renderer, map, camera, view, tile, tx, ty, alpha, base);
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
                        const uint8_t eased = alpha < renderer->wall_alpha[c] ? alpha : renderer->wall_alpha[c];
                        MapRenderer__DrawTileCut(renderer, map, camera, view, tile, tx, ty, eased, alpha);
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

// The game darkens by its palette's light levels (pal.pl2: 32 tables, each mapping a colour to the one it becomes at
// that level), so its dark isn't plain black over the picture: each act's colours sink their own way. Worked out here
// as the average change of red, green and blue at each level, put in order of how dark it is.
static void MapRenderer__BuildShade(MapRenderer *renderer, const Palette *palette) {
    renderer->shade_palette = palette;
    for (int f = 0; f < 256; f++) { // (no pl2: plain black)
        renderer->shade[f][0] = renderer->shade[f][1] = renderer->shade[f][2] = (uint8_t)(255 - f);
    }
    if (palette == NULL || palette->name == NULL) {
        return;
    }
    char path[256];
    snprintf(path, sizeof(path), "/data/global/Palette/%s/Pal.pl2", palette->name);
    if (!FileManager_FileExists(path)) {
        return;
    }
    MpqStream *stream = FileManager_OpenFile(path);
    if (stream == NULL) {
        return;
    }
    enum { LEVELS = 32 };
    static uint8_t data[1024 + (LEVELS * 256)];
    const bool     read =
        MpqStream_GetSize(stream) >= sizeof(data) && MpqStream_Read(stream, data, 0, sizeof(data)) == sizeof(data);
    MpqStream_Destroy(&stream);
    if (!read) {
        return;
    }
    // Each level: how much of each channel is left (mapped colours against the palette's own)
    float ratio[LEVELS][4];
    for (int l = 0; l < LEVELS; l++) {
        double from[3] = {0.0, 0.0, 0.0};
        double to[3]   = {0.0, 0.0, 0.0};
        for (int i = 1; i < 256; i++) {
            const uint8_t *was = &data[i * 4]; // (pl2's palette: R, G, B, unused)
            const uint8_t *now = &data[data[1024 + (l * 256) + i] * 4];
            for (int c = 0; c < 3; c++) {
                from[c] += was[c];
                to[c]   += now[c];
            }
        }
        for (int c = 0; c < 3; c++) {
            ratio[l][c] = from[c] > 0.0 ? (float)(to[c] / from[c]) : 0.0f;
            ratio[l][c] = ratio[l][c] > 1.0f ? 1.0f : ratio[l][c];
        }
        ratio[l][3] = (ratio[l][0] * 0.3f) + (ratio[l][1] * 0.59f) + (ratio[l][2] * 0.11f); // how bright it leaves
    }
    // For each amount of darkness: the level (between two) that leaves that much light
    for (int f = 0; f < 256; f++) {
        const float want = 1.0f - ((float)f / 255.0f);
        int         best = 0;
        for (int l = 1; l < LEVELS; l++) {
            best = fabsf(ratio[l][3] - want) < fabsf(ratio[best][3] - want) ? l : best;
        }
        // (scaled so it leaves exactly as much light as wanted, keeping that level's tint)
        const float scale = ratio[best][3] > 0.001f ? want / ratio[best][3] : 0.0f;
        for (int c = 0; c < 3; c++) {
            const float v         = ratio[best][c] * scale;
            renderer->shade[f][c] = (uint8_t)(v >= 1.0f ? 255 : (v <= 0.0f ? 0 : (int)(v * 255.0f)));
        }
    }
}

void MapRenderer_DrawLights(MapRenderer *renderer, const MapCamera *camera, const SDL_Rect *view,
                            const MapLight *lights, const int count, const uint8_t darkness) {
    if (darkness == 0 || view->w <= 0 || view->h <= 0) {
        return;
    }
    const int w = (view->w + 3) / 4;
    const int h = (view->h + 3) / 4;
    if (renderer->dark_mask == NULL || renderer->dark_w != w || renderer->dark_h != h) {
        if (renderer->dark_mask != NULL) {
            SDL_DestroyTexture(renderer->dark_mask);
        }
        renderer->dark_mask =
            SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        renderer->dark_w = w;
        renderer->dark_h = h;
        if (renderer->dark_mask == NULL) {
            return;
        }
        SDL_SetTextureBlendMode(renderer->dark_mask, SDL_BLENDMODE_MOD); // (the picture times the shade)
        SDL_SetTextureScaleMode(renderer->dark_mask, SDL_ScaleModeLinear);
    }
    if (renderer->shade_palette != renderer->palette || renderer->shade_palette == NULL) {
        MapRenderer__BuildShade(renderer, renderer->palette);
    }
    // Each pool on screen: its middle and half width (the pools are flat ellipses, twice as wide as high)
    enum { POOLS = 64 };
    float px[POOLS];
    float py[POOLS];
    float pr[POOLS];
    int   pools = 0;
    for (int l = 0; l < count && pools < POOLS; l++) {
        int sx;
        int sy;
        MapView_WorldToScreen(camera, view, lights[l].world_x, lights[l].world_y, &sx, &sy);
        const float half = lights[l].radius * SUBTILE_HALF_W * 2.0f * camera->zoom;
        if (half <= 0.0f || sx + half < view->x || sx - half > view->x + view->w || sy + half < view->y ||
            sy - (half / 2.0f) > view->y + view->h) {
            continue;
        }
        px[pools]   = (float)(sx - view->x) / 4.0f;
        py[pools]   = (float)(sy - view->y) / 4.0f;
        pr[pools++] = half / 4.0f;
    }
    void *raw   = NULL;
    int   pitch = 0;
    if (SDL_LockTexture(renderer->dark_mask, NULL, &raw, &pitch) != 0) {
        return;
    }
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)raw + ((size_t)y * (size_t)pitch));
        for (int x = 0; x < w; x++) {
            float shade = 1.0f; // the least dark of the pools over this spot
            for (int p = 0; p < pools && shade > 0.0f; p++) {
                const float dx = ((float)x + 0.5f - px[p]) / pr[p];
                const float dy = ((float)y + 0.5f - py[p]) / (pr[p] / 2.0f);
                const float d2 = (dx * dx) + (dy * dy);
                if (d2 >= 1.0f) {
                    continue;
                }
                const float d = sqrtf(d2);
                const float t = d * d * (3.0f - (2.0f * d)); // smooth fall-off
                shade         = t < shade ? t : shade;
            }
            const uint8_t *tint = renderer->shade[(int)(shade * (float)darkness)];
            row[x] =
                ((uint32_t)tint[0] << 24) | ((uint32_t)tint[1] << 16) | ((uint32_t)tint[2] << 8) | 0xFFu; // RGBA8888
        }
    }
    SDL_UnlockTexture(renderer->dark_mask);
    SDL_RenderSetClipRect(sdl_renderer, view);
    const SDL_Rect dest = {view->x, view->y, w * 4, h * 4};
    SDL_RenderCopy(sdl_renderer, renderer->dark_mask, NULL, &dest);
    SDL_RenderSetClipRect(sdl_renderer, NULL);
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
