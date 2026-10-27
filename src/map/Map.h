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
#define MAP_MAX_SIZE        256  // maps you make or open in the editor
#define MAP_MAX_WORLD_SIZE  1024 // built maps: an act's outdoor areas joined into one
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
    int   direction;  // 0..15 in turn order (0 = facing south)
    int   object_row; // objects: the objects.txt row (-1 = not an object / unknown)
    char  mode[4];    // objects: the animation it shows ("nu", "on" for lit torches); "" = the usual one
} MapEntity;

// A way out of the map into another area: walking off this part of the edge, or stepping on an entrance
#define MAP_MAX_EXITS 48
#define MAP_MAX_AREAS 12
typedef struct MapExit {
    int   target_level; // Levels.txt Id
    int   x0;           // sub-tiles, inclusive
    int   y0;
    int   x1;
    int   y1;
    float arrive_x; // where you come in when arriving from that area
    float arrive_y;
    bool  entrance; // a cave mouth, stairs, ...: taken when you click it, not just by walking over it
} MapExit;

// One of the areas a joined-up map is made of (an act's outdoors: the Rogue Encampment, the Blood Moor, ...)
typedef struct MapArea {
    int   level_id; // Levels.txt Id
    char  name[48];
    int   x0; // tiles, inclusive
    int   y0;
    int   x1;
    int   y1;
    float start_x; // sub-tiles: where you start when you go there from the Places list
    float start_y;
} MapArea;

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

    uint8_t *blocked; // per sub-tile: blocked by something standing there (objects), NULL = nothing

    // Built from several presets (Map_StampDS1): each cell only uses the tile sets of the preset it came from, so
    // presets with clashing tile numbers don't mix. One bit per dt1s[] entry; 0 or NULL = any.
    uint64_t *dt1_masks;

    int     exit_count;
    MapExit exits[MAP_MAX_EXITS];

    // Joined-up maps: the areas in it and which one each tile belongs to (NULL / 0 for a single area)
    int      area_count;
    MapArea  areas[MAP_MAX_AREAS];
    uint8_t *area_of; // per tile: index into areas, 0xFF = none
    uint32_t seed;    // the world seed it was built from (0 = not built from one)

    bool  has_start; // where the player enters (generated areas); otherwise the middle
    float start_x;   // sub-tiles
    float start_y;

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
bool Map_SaveDS1(const Map *map, const char *file_path); // the game's format (the units with a preset number)

// Building a map out of presets (the game's randomly built areas): create it empty, stamp DS1 presets onto it,
// then finish it once to look up all the tiles.
#define MAP_STAMP_FILLED_ONLY 0x01 // only copy cells that have a tile (decorations over existing ground)
#define MAP_STAMP_NO_ENTITIES 0x02 // leave out the preset's monsters and objects
#define MAP_STAMP_NO_MONSTERS 0x04 // leave out its monsters (keep objects)

Map *Map_CreateEmpty(int width, int height, int act);
void Map_StampDS1(Map *map, const DS1 *ds1, int tx, int ty, unsigned flags);
// Copies another map's tiles (where it has any), tile sets and entities in at (tx, ty); exits aren't copied
void Map_StampMap(Map *map, const Map *source, int tx, int ty);
void Map_Finish(Map *map);

// Exits to other areas
void           Map_AddExit(Map *map, int target_level, int x0, int y0, int x1, int y1, float arrive_x, float arrive_y);
const MapExit *Map_ExitAt(const Map *map, float subtile_x, float subtile_y); // NULL if none
const MapExit *Map_ExitTo(const Map *map, int target_level);                 // NULL if none

// Joined-up maps: the area at a sub-tile (NULL if none, or a single-area map), and an area by level
const MapArea *Map_AreaAt(const Map *map, float subtile_x, float subtile_y);
const MapArea *Map_FindArea(const Map *map, int level_id);
void           Map_Destroy(Map **map);

// Tiles
void Map_SetCell(Map *map, MapLayerKind kind, int layer, int tx, int ty, const MapTileChoice *choice); // NULL clears
const DS1Cell *Map_GetCell(const Map *map, MapLayerKind kind, int layer, int tx, int ty);
void Map_SetCellRaw(Map *map, MapLayerKind kind, int layer, int tx, int ty, const DS1Cell *value); // exactly (undo)
bool Map_IsWalkable(const Map *map, int subtile_x, int subtile_y);
// What the floor under a sub-tile is made of (DT1_MATERIAL_* flags), 0 if unknown
uint16_t Map_GetFloorMaterial(const Map *map, int subtile_x, int subtile_y);
// Every paintable tile in the map's DT1s for one layer kind (floors, walls or shadows)
int Map_GetChoices(const Map *map, MapLayerKind kind, const MapTileChoice **choices);

// Entities
MapEntity *Map_AddEntity(Map *map, const char *base, const char *id, const char *name, float x, float y);
void       Map_RemoveEntity(Map *map, int index);

// Blocking sub-tiles that the tiles leave open (objects: chests, closed doors, ...); Map_IsWalkable honours it.
void Map_SetBlocked(Map *map, int subtile_x, int subtile_y, int width, int height, bool blocked);
bool Map_IsObjectBlocked(const Map *map, int subtile_x, int subtile_y); // blocked by Map_SetBlocked (not the tiles)
void Map_ClearBlocked(Map *map);

// Folder our own maps are saved in (created on demand)
const char *Map_GetSaveFolder(void);
// Names (without ".abm") of the maps in the save folder, sorted. Free with Map_FreeNames.
int  Map_ListSavedMaps(char ***names);
void Map_FreeNames(char **names, int count);

#endif // ABYSS_MAP_H
