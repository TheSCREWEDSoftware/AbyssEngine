#ifndef ABYSS_AUTOMAP_H
#define ABYSS_AUTOMAP_H

#include "Map.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// The game's automap (Tab): the parts of an area you've seen, drawn from the game's own automap pieces
// (data/global/ui/automap/MaxiMap.dc6, picked per tile by AutoMap.txt), over the view and centred on you, with the
// ways out labelled ("To The Cold Plains").
//
// What you've seen is kept per area for as long as the world (seed) stays the same, like the game keeps it for one
// game.

typedef struct Automap Automap;

Automap *Automap_Create(void);
void     Automap_Destroy(Automap **automap);

// The area being shown: its map and Levels.txt Id (for the tile set's automap pieces). Call when the map changes.
void Automap_SetArea(Automap *automap, const Map *map, int level_id, uint32_t world_seed);
// Marks what's around (subtile_x, subtile_y) as seen
void Automap_Reveal(Automap *automap, float subtile_x, float subtile_y);
// An object's mark on the automap (waypoints, shrines, wells, stairs, ...): where it is, and which piece of the act's
// MaxiMap.dc6 (objects.txt AutoMap). Shown once its tile has been seen.
typedef struct AutomapMark {
    float x; // sub-tiles
    float y;
    int   cel;
} AutomapMark;

#define AUTOMAP_WAYPOINT_CEL 307 // objects.txt gives it to only some waypoints; the game marks them all

// Draws it over the view, centred on the player, with the objects' marks
void Automap_Draw(Automap *automap, const SDL_Rect *view, float player_x, float player_y, const AutomapMark *marks,
                  int mark_count);

#endif // ABYSS_AUTOMAP_H
