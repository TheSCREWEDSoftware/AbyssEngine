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

void World_SetSeed(uint32_t seed); // 0 = pick a random one
// How big each area is (tiles), so the layout keeps areas from running into each other; set by Places
void     World_SetAreaSizes(bool (*size_of)(int level_id, int *width, int *height));
uint32_t World_GetSeed(void);
uint32_t World_LevelSeed(int level_id);                 // the seed one area is built from
int      World_PickVariant(int level_id, int variants); // which of a fixed area's files (the encampment's exits)

// The areas you can walk into from this one, and the side of this area each is on. Returns the count.
int World_GetWalkLinks(int level_id, int *targets, WorldSide *sides, int max);
// The areas reached through entrances in this one (first: the way back up, for dungeons). Returns the count.
int World_GetWarpLinks(int level_id, int *targets, int max);
// Where an entrance leads: an entrance tile in a map (a special tile) has a main index, and Levels.txt's Vis<index>
// column of the area it's in is the area it leads to (the Den of Evil's cave mouth in the Blood Moor is 5: Vis5 = 8).
// 0 if that column is empty.
int World_GetVis(int level_id, int slot);
// How an area feels: indoors (dark, lit around you), rain or snow, its music (Levels.txt, SoundEnviron.txt, Sounds.txt)
typedef struct WorldAtmosphere {
    bool inside;
    bool rain;
    bool snow;
    char music[128]; // "" = none
    // Its sounds (SoundEnviron.txt, by Sounds.txt name): the background loop (the wind, a cave's hum), and the odd
    // sound now and then (birds, drips) every so many frames (25 a second)
    char ambience[48];
    char event[48];
    char night_ambience[48]; // at night outdoors ("" = the same as by day)
    char night_event[48];
    int  event_delay;
} WorldAtmosphere;
bool World_GetAtmosphere(int level_id, WorldAtmosphere *atmosphere);

// The area's waypoint (Levels.txt's Waypoint column: 0 the Rogue Encampment's, ... 38), -1 if it has none
int World_WaypointIndex(int level_id);

// For a fixed map (towns, ...): finds where paths leave the map and adds them as exits to the areas you can walk to
// from it, and puts the start at the game's start marker if it has one.
void World_AddFixedExits(Map *map, int level_id);

// The areas joined to this one by a red portal (Tristram from the Stony Field, ...). Returns the count.
int World_GetPortalLinks(int level_id, int *targets, int max);
int World_GetForwardPortals(int level_id, int *targets, int max); // just the ones leading on (not the way back)
int World_TrueTomb(void); // which of Tal Rasha's seven tombs leads to Duriel's Lair (the seed picks)
// A red portal at (x, y) (sub-tiles): the object, and an entrance to the area
void World_AddPortal(Map *map, int target, float x, float y);
// The spot you can walk to from the start that is nearest to (x, y) (sub-tiles); false if the start is blocked
bool World_NearestReachable(const Map *map, float x, float y, float *out_x, float *out_y);
// Makes sure every area this one leads to (stairs, portals) has a way there: a red portal for any the map's own
// entrance tiles didn't give (near the start for the way back, as far away as can be for the ways on)
void World_EnsureExits(Map *map, int level_id);

// The game's caption when you walk into an area, "Entering The Blood Moor" (from Levels.txt's LevelWarp,
// "To The Blood Moor", in the game's language)
void World_GetEnteringText(int level_id, char *text, int size);
// The way to an area, as the game labels it: "To The Blood Moor" (Levels.txt's LevelWarp)
void World_GetWarpText(int level_id, char *text, int size);

void World_Release(void);

#endif // ABYSS_WORLD_H
