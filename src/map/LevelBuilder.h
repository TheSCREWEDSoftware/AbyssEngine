#ifndef ABYSS_LEVEL_BUILDER_H
#define ABYSS_LEVEL_BUILDER_H

#include "Map.h"
#include <stdbool.h>
#include <stdint.h>

// The game's randomly built areas, assembled from its presets (LvlPrest.txt) the way the game does it:
//
//  - Wilderness (Blood Moor, Cold Plains, the desert, the Outer Steppes, the Highlands, Kurast): a grid of 8x8 (or
//    16x16) tile blocks with a ring of border pieces round the edge (with the odd dent), the area's landmarks (cave
//    entrances, the Cairn Stones, ...), random filler pieces (camps, cottages, ruins, ...), waypoint and shrines, and
//    small decorations scattered over the open ground (LvlSub.txt).
//  - Dungeons (caves, crypts, the Jail, catacombs, sewers, tombs, the jungle, ...): rooms on a grid joined into a
//    random tree, each room the preset with exactly the exits it needs ("Act 1 - Cave NSE"), the first room the way
//    in and the room furthest from it the way on.
//  - The Bloody Foothills: the siege strips side by side.
//
// Monsters are spread over the area from its Levels.txt list and density (normal difficulty).

bool LevelBuilder_CanBuild(int level_id);
// A new random layout every time (seed 0 = pick one); NULL if the area can't be built
Map *LevelBuilder_Build(int level_id, uint32_t seed);
void LevelBuilder_Release(void);

#endif // ABYSS_LEVEL_BUILDER_H
