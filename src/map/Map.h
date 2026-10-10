#ifndef ABYSS_MAP_H
#define ABYSS_MAP_H

#include "../types/DS1.h"
#include "../types/DT1.h"
#include <stdbool.h>

// An editable map: a grid of tiles (floors, walls, shadows) drawn from a set of DT1 files, plus monsters and
// objects placed in it. Loaded from a game DS1 preset, created blank, or saved/loaded in our own .abm format.
//
// Coordinates: tiles (tx, ty); sub-tiles are 1/5 of a tile and are what entities and walkability use.

#define MAP_SUBTILES        5
#define MAP_MAX_SIZE        256
#define MAP_MAX_DT1         48
#define MAP_WALL_LAYERS     DS1_MAX_WALL_LAYERS
#define MAP_FLOOR_LAYERS    DS1_MAX_FLOOR_LAYERS
#define MAP_ENTITY_NAME_LEN 48
#define MAP_DEFAULT_SIZE    24

typedef enum MapLayerKind { MAP_LAYER_FLOOR, MAP_LAYER_WALL, MAP_LAYER_SHADOW } MapLayerKind;

// A kind of tile that can be painted: everything sharing (orientation, main index, sub index) is one choice; the
// variants (by rarity) are picked per cell.
typedef struct MapTileChoice {
    uint8_t        orientation;
    uint8_t        style;
    uint8_t        sequence;
    const DT1Tile *preview;
    int            dt1_index;
} MapTileChoice;

typedef struct MapEntity {
    char  base[12]; // "monsters", "chars" or "objects"
    char  id[40];   // Bestiary id: monstats Id ("zombie1"), object token or player token
    char  name[MAP_ENTITY_NAME_LEN];
    float x; // sub-tiles
    float y;
    int   direction; // 0..15 in turn order (0 = facing south)
} MapEntity;

typedef struct Map {
    char     name[64];
    int      width;
    int      height;
    int      act; // 0..4, picks the palette
    int      dt1_count;
    char    *dt1_paths[MAP_MAX_DT1];
    DT1     *dt1s[MAP_MAX_DT1];
    int      wall_layer_count;
    int      floor_layer_count;
    DS1Cell *walls[MAP_WALL_LAYERS]; // [ty * width + tx]
    DS1Cell *floors[MAP_FLOOR_LAYERS];
    DS1Cell *shadows;

    // Resolved tiles per cell (NULL = nothing drawn); walls can need a second tile (north-west corners)
    const DT1Tile **floor_tiles[MAP_FLOOR_LAYERS];
    const DT1Tile **wall_tiles[MAP_WALL_LAYERS];
    const DT1Tile **wall_extra_tiles[MAP_WALL_LAYERS];
    const DT1Tile **shadow_tiles;

    int        entity_count;
    int        entity_capacity;
    MapEntity *entities;

    // All tiles of all DT1s, sorted by (orientation, style, sequence) for lookups
    int                  tile_index_count;
    struct MapTileIndex *tile_index;
    const DT1Tile      **anim_frames;      // storage for the animated floors' frame lists
    int                  choice_counts[3]; // per MapLayerKind
    MapTileChoice       *choices[3];
} Map;

// Creation
Map *Map_LoadDS1(const char *ds1_path);
Map *Map_CreateBlank(int width, int height, int act, const char *const *dt1_paths, int dt1_count);
Map *Map_LoadFile(const char *file_path); // .abm
bool Map_SaveFile(const Map *map, const char *file_path);
void Map_Destroy(Map **map);

// Tiles
void Map_SetCell(Map *map, MapLayerKind kind, int layer, int tx, int ty, const MapTileChoice *choice); // NULL clears
const DS1Cell *Map_GetCell(const Map *map, MapLayerKind kind, int layer, int tx, int ty);
bool           Map_IsWalkable(const Map *map, int subtile_x, int subtile_y);
// Every paintable tile in the map's DT1s for one layer kind (floors, walls or shadows)
int Map_GetChoices(const Map *map, MapLayerKind kind, const MapTileChoice **choices);

// Entities
MapEntity *Map_AddEntity(Map *map, const char *base, const char *id, const char *name, float x, float y);
void       Map_RemoveEntity(Map *map, int index);

// Folder our own maps are saved in (created on demand)
const char *Map_GetSaveFolder(void);
// Names (without ".abm") of the maps in the save folder, sorted. Free with Map_FreeNames.
int  Map_ListSavedMaps(char ***names);
void Map_FreeNames(char **names, int count);

#endif // ABYSS_MAP_H
