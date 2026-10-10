#ifndef ABYSS_WORLD_H
#define ABYSS_WORLD_H

#include "Map.h"
#include <stdbool.h>
#include <stdint.h>

// The world: one seed that decides every randomly built area (the same seed always builds the same world), and how
// the areas join up:
//  - on foot: walk out of the Rogue Encampment into the Blood Moor, on into Cold Plains, ... (the game keeps this
//    list in its program; ours is in World.c). Each area picks the side of its border that leads to each neighbour.
//  - through entrances: cave mouths, the Den of Evil, tomb doors, stairs up and down (Levels.txt Vis/Warp columns).

typedef enum WorldSide { WORLD_SIDE_N, WORLD_SIDE_S, WORLD_SIDE_E, WORLD_SIDE_W, WORLD_SIDE_NONE } WorldSide;

void     World_SetSeed(uint32_t seed); // 0 = pick a random one
uint32_t World_GetSeed(void);
uint32_t World_LevelSeed(int level_id);                 // the seed one area is built from
int      World_PickVariant(int level_id, int variants); // which of a fixed area's files (the encampment's exits)

// The areas you can walk into from this one, and the side of this area each is on. Returns the count.
int World_GetWalkLinks(int level_id, int *targets, WorldSide *sides, int max);
// The areas reached through entrances in this one (first: the way back up, for dungeons). Returns the count.
int World_GetWarpLinks(int level_id, int *targets, int max);

// For a fixed map (towns, ...): finds where paths leave the map and adds them as exits to the areas you can walk to
// from it, and puts the start at the game's start marker if it has one.
void World_AddFixedExits(Map *map, int level_id);

void World_Release(void);

#endif // ABYSS_WORLD_H
