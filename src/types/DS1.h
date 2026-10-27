#ifndef ABYSS_DS1_H
#define ABYSS_DS1_H

#include <stdbool.h>
#include <stdint.h>

// DS1: a map preset (a town, a dungeon room, ...): a grid of floor, wall, shadow layers referencing DT1 tiles by
// (orientation, main index, sub index), the DT1 files it uses and the monsters/objects placed in it.

#define DS1_MAX_WALL_LAYERS  4
#define DS1_MAX_FLOOR_LAYERS 2

typedef struct DS1Cell {
    uint8_t prop1;       // 0 = no tile
    uint8_t sequence;    // DT1 sub index
    uint8_t style;       // DT1 main index
    uint8_t orientation; // walls only; floors are 0, shadows 13
    bool    hidden;
} DS1Cell;

typedef struct DS1Object {
    int32_t type; // 1 = monster/NPC, 2 = object
    int32_t id;   // per-act preset id
    int32_t x;    // sub-tiles (5 per tile)
    int32_t y;
    int32_t flags;
} DS1Object;

typedef struct DS1 {
    int32_t    version;
    int32_t    width; // tiles
    int32_t    height;
    int32_t    act; // 0..4
    int32_t    substitution_type;
    int32_t    file_count;
    char     **files; // DT1 paths, e.g. "data\global\tiles\act1\town\floor.dt1"
    int32_t    wall_layer_count;
    int32_t    floor_layer_count;
    DS1Cell   *walls[DS1_MAX_WALL_LAYERS]; // [y * width + x]
    DS1Cell   *floors[DS1_MAX_FLOOR_LAYERS];
    DS1Cell   *shadows;
    int32_t    object_count;
    DS1Object *objects;
} DS1;

DS1 *DS1_Load(const char *path);                         // from the game's files; NULL if missing or broken
DS1 *DS1_LoadDiskFile(const char *path);                 // a .ds1 file on disk
DS1 *DS1_LoadMemory(const uint8_t *data, uint32_t size); // (a copy is read)
void DS1_Destroy(DS1 **ds1);
bool DS1_Save(const DS1 *ds1, const char *path); // a file on disk (version 18)

#endif // ABYSS_DS1_H
