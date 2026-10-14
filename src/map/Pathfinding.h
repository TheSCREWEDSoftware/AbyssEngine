#ifndef ABYSS_PATHFINDING_H
#define ABYSS_PATHFINDING_H

#include "Map.h"
#include <stdbool.h>

// Finding a way around walls on a map's sub-tile grid (A*, 8 directions, no cutting corners), smoothed into a few
// straight legs. If the goal can't be reached (blocked, or too far), the path leads to the reachable spot closest
// to it.

#define PATH_MAX_POINTS 128

typedef struct MapPath {
    int   count;
    int   next;               // the point being walked to
    bool  partial;            // the search stopped short (too far to look at once): plan again from the end of it
    float x[PATH_MAX_POINTS]; // sub-tiles (centres)
    float y[PATH_MAX_POINTS];
} MapPath;

// Returns false if there's nowhere to go (already as close as it gets); the path is empty then.
bool Map_FindPath(const Map *map, float from_x, float from_y, float to_x, float to_y, MapPath *path);
// True when a straight walk from one point to the other doesn't cross any blocked sub-tile.
bool Map_HasClearLine(const Map *map, float from_x, float from_y, float to_x, float to_y);

#endif // ABYSS_PATHFINDING_H
