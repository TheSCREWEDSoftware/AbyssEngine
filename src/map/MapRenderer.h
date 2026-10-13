#ifndef ABYSS_MAP_RENDERER_H
#define ABYSS_MAP_RENDERER_H

#include "Map.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// Draws a Map isometrically: floors, shadows, then walls and entities from back to front, then roofs.
//
// World pixels: the top corner of tile (tx, ty) is at ((tx - ty) * 80, (tx + ty) * 40); one sub-tile is 32x16.
// The camera names the world pixel shown at the centre of the view.

typedef struct MapCamera {
    float x;
    float y;
    float zoom;
} MapCamera;

// Something drawn in the world between the walls (a monster, the player, a marker).
typedef struct MapDrawable {
    float x; // sub-tiles
    float y;
    void (*draw)(void *context, int screen_x, int screen_y, float zoom);
    void *context;
} MapDrawable;

#define MAP_DRAW_WALLS   0x01
#define MAP_DRAW_SHADOWS 0x02
#define MAP_DRAW_ROOFS   0x04
#define MAP_DRAW_GRID    0x08
#define MAP_DRAW_BLOCKED 0x10 // collision: red = the map's tiles, blue = objects; exits green, entrances yellow
#define MAP_DRAW_SPECIAL 0x20 // markers the game never shows (warps, spawn points)
#define MAP_DRAW_DEFAULT (MAP_DRAW_WALLS | MAP_DRAW_SHADOWS | MAP_DRAW_ROOFS)

typedef struct MapRenderer MapRenderer;

MapRenderer *MapRenderer_Create(void);
void         MapRenderer_Destroy(MapRenderer **renderer);
void         MapRenderer_ClearCache(MapRenderer *renderer); // call when the map (or its act) changes
void         MapRenderer_Draw(MapRenderer *renderer, const Map *map, const MapCamera *camera, const SDL_Rect *view,
                              const MapDrawable *drawables, int drawable_count, unsigned flags);
// Where the player stands (sub-tiles). Like the game, the roof over a building fades away while they're inside it, so
// you can see in. Call before MapRenderer_Draw; without it (or with x < 0) roofs are always drawn.
void MapRenderer_SetViewer(MapRenderer *renderer, float subtile_x, float subtile_y);
// "See through obstructions": walls and trees in front of the viewer that would hide them turn see-through
void MapRenderer_SetSeeThrough(MapRenderer *renderer, bool see_through);
// A single tile, e.g. a palette preview, scaled to fit the box.
void MapRenderer_DrawTilePreview(MapRenderer *renderer, const Map *map, const DT1Tile *tile, const SDL_Rect *box);
// Darkens the view except for a soft pool of light around (world_x, world_y). darkness: 0 (none) to 255 (black);
// radius in sub-tiles.
void MapRenderer_DrawLight(MapRenderer *renderer, const MapCamera *camera, const SDL_Rect *view, float world_x,
                           float world_y, float radius, uint8_t darkness);
// Outline of a tile's floor diamond.
void MapRenderer_DrawTileOutline(const MapCamera *camera, const SDL_Rect *view, int tx, int ty, SDL_Color color);

// Coordinate helpers
void MapView_SubtileToWorld(float sx, float sy, float *world_x, float *world_y);
void MapView_WorldToSubtile(float world_x, float world_y, float *sx, float *sy);
void MapView_WorldToScreen(const MapCamera *camera, const SDL_Rect *view, float world_x, float world_y, int *screen_x,
                           int *screen_y);
void MapView_ScreenToWorld(const MapCamera *camera, const SDL_Rect *view, int screen_x, int screen_y, float *world_x,
                           float *world_y);
const char *MapView_ActPalette(int act);

#endif // ABYSS_MAP_RENDERER_H
