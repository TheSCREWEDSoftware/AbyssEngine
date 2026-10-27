#include "Map.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "MonsterStats.h"
#include "Objects.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAP_FILE_MAGIC   "AbyssMap"
#define MAP_FILE_VERSION 1

typedef struct MapTileIndex {
    uint8_t        orientation;
    uint8_t        style;
    uint8_t        sequence;
    int32_t        rarity;
    int            dt1_index;
    const DT1Tile *tile;
} MapTileIndex;

// ---------------------------------------------------------------------------------------------------------------------
// Tile lookup

static int Map__CompareIndex(const void *a, const void *b) {
    const MapTileIndex *x = a;
    const MapTileIndex *y = b;
    if (x->orientation != y->orientation) {
        return x->orientation - y->orientation;
    }
    if (x->style != y->style) {
        return x->style - y->style;
    }
    if (x->sequence != y->sequence) {
        return x->sequence - y->sequence;
    }
    if (x->dt1_index != y->dt1_index) {
        return x->dt1_index - y->dt1_index;
    }
    return (x->tile < y->tile) ? -1 : (x->tile > y->tile);
}

static MapLayerKind Map__KindOf(const int orientation) {
    if (orientation == DT1_ORIENTATION_FLOOR) {
        return MAP_LAYER_FLOOR;
    }
    if (orientation == DT1_ORIENTATION_SHADOW) {
        return MAP_LAYER_SHADOW;
    }
    return MAP_LAYER_WALL;
}

// Animated floors (lava, ...) are stored as several tiles with the same (style, sequence) whose "rarity" values are
// the frame numbers 0, 1, 2, ... (random variants use weights instead, and never this exact pattern).
static void Map__FindAnimations(Map *map) {
    map->anim_frames = calloc(map->tile_index_count > 0 ? (size_t)map->tile_index_count : 1, sizeof(DT1Tile *));
    FAIL_IF_NULL(map->anim_frames);
    int used = 0;
    for (int first = 0; first < map->tile_index_count;) {
        const MapTileIndex *group = &map->tile_index[first];
        int                 count = 1;
        while (first + count < map->tile_index_count && group[count].orientation == group->orientation &&
               group[count].style == group->style && group[count].sequence == group->sequence) {
            count++;
        }

        bool animated = group->orientation == DT1_ORIENTATION_FLOOR && count >= 2;
        for (int frame = 0; frame < count && animated; frame++) {
            int found = 0;
            for (int i = 0; i < count; i++) {
                found += group[i].rarity == frame ? 1 : 0;
            }
            animated = found == 1;
        }
        if (animated) {
            const DT1Tile **frames = &map->anim_frames[used];
            for (int i = 0; i < count; i++) {
                frames[group[i].rarity] = group[i].tile;
            }
            for (int i = 0; i < count; i++) {
                DT1Tile *tile          = (DT1Tile *)group[i].tile; // the map owns its tile sets
                tile->anim_frame_count = count;
                tile->anim_frames      = frames;
            }
            used += count;
        }
        first += count;
    }
}

static void Map__BuildTileIndex(Map *map) {
    free(map->tile_index);
    free(map->anim_frames);
    map->anim_frames = NULL;
    for (int k = 0; k < 3; k++) {
        free(map->choices[k]);
        map->choices[k]       = NULL;
        map->choice_counts[k] = 0;
    }

    int total = 0;
    for (int d = 0; d < map->dt1_count; d++) {
        total += map->dt1s[d] != NULL ? DT1_GetTileCount(map->dt1s[d]) : 0;
    }

    map->tile_index       = calloc(total > 0 ? (size_t)total : 1, sizeof(MapTileIndex));
    map->tile_index_count = 0;
    FAIL_IF_NULL(map->tile_index);
    for (int d = 0; d < map->dt1_count; d++) {
        if (map->dt1s[d] == NULL) {
            continue;
        }
        for (int t = 0; t < DT1_GetTileCount(map->dt1s[d]); t++) {
            const DT1Tile *tile = DT1_GetTile(map->dt1s[d], t);
            if (tile->image == NULL || tile->orientation < 0 || tile->orientation > 255 || tile->main_index < 0 ||
                tile->main_index > 63 || tile->sub_index < 0 || tile->sub_index > 63) {
                continue;
            }
            MapTileIndex *entry = &map->tile_index[map->tile_index_count++];
            entry->orientation  = (uint8_t)tile->orientation;
            entry->style        = (uint8_t)tile->main_index;
            entry->sequence     = (uint8_t)tile->sub_index;
            entry->rarity       = tile->rarity;
            entry->dt1_index    = d;
            entry->tile         = tile;
        }
    }
    qsort(map->tile_index, (size_t)map->tile_index_count, sizeof(MapTileIndex), Map__CompareIndex);
    Map__FindAnimations(map);

    // One choice per distinct (orientation, style, sequence)
    for (int k = 0; k < 3; k++) {
        map->choices[k] = calloc(map->tile_index_count > 0 ? (size_t)map->tile_index_count : 1, sizeof(MapTileChoice));
        FAIL_IF_NULL(map->choices[k]);
    }
    for (int i = 0; i < map->tile_index_count; i++) {
        const MapTileIndex *entry = &map->tile_index[i];
        if (i > 0 && entry->orientation == map->tile_index[i - 1].orientation &&
            entry->style == map->tile_index[i - 1].style && entry->sequence == map->tile_index[i - 1].sequence) {
            continue;
        }
        if (entry->orientation == DT1_ORIENTATION_NW_LEFT) {
            continue; // painted together with its right half
        }
        const MapLayerKind kind   = Map__KindOf(entry->orientation);
        MapTileChoice     *choice = &map->choices[kind][map->choice_counts[kind]++];
        choice->orientation       = entry->orientation;
        choice->style             = entry->style;
        choice->sequence          = entry->sequence;
        choice->preview           = entry->tile;
        choice->dt1_index         = entry->dt1_index;
    }
}

static bool Map__InMask(const uint64_t mask, const int dt1_index) {
    return mask == 0 || (dt1_index < 64 && (mask & (1ull << dt1_index)) != 0);
}

// Picks a tile for a cell; variants are chosen by their rarity, stable per cell.
static const DT1Tile *Map__FindTileIn(const Map *map, const int orientation, const int style, const int sequence,
                                      const int tx, const int ty, const uint64_t mask) {
    int low  = 0;
    int high = map->tile_index_count;
    while (low < high) {
        const int           mid = (low + high) / 2;
        const MapTileIndex *e   = &map->tile_index[mid];
        const int           cmp = e->orientation != orientation ? e->orientation - orientation
                                  : e->style != style           ? e->style - style
                                                                : e->sequence - sequence;
        if (cmp < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    int first = low;
    int count = 0;
    int total = 0;
    while (first + count < map->tile_index_count) {
        const MapTileIndex *e = &map->tile_index[first + count];
        if (e->orientation != orientation || e->style != style || e->sequence != sequence) {
            break;
        }
        total += (e->rarity > 0 && Map__InMask(mask, e->dt1_index)) ? e->rarity : 0;
        count++;
    }
    int usable = -1; // first entry from an allowed tile set
    for (int i = 0; i < count && usable < 0; i++) {
        usable = Map__InMask(mask, map->tile_index[first + i].dt1_index) ? first + i : -1;
    }
    if (usable < 0) {
        return NULL;
    }
    if (map->tile_index[usable].tile->anim_frame_count > 1) {
        return map->tile_index[usable].tile->anim_frames[0]; // animated: the renderer steps through the frames
    }
    if (total == 0) {
        return map->tile_index[usable].tile;
    }

    uint32_t hash  = ((uint32_t)tx * 73856093u) ^ ((uint32_t)ty * 19349663u) ^ ((uint32_t)orientation * 83492791u);
    hash          ^= hash >> 13;
    hash          *= 0x5bd1e995u;
    hash          ^= hash >> 15;
    int pick       = (int)(hash % (uint32_t)total);
    for (int i = 0; i < count; i++) {
        const MapTileIndex *e = &map->tile_index[first + i];
        if (e->rarity <= 0 || !Map__InMask(mask, e->dt1_index)) {
            continue;
        }
        if (pick < e->rarity) {
            return e->tile;
        }
        pick -= e->rarity;
    }
    return map->tile_index[usable].tile;
}

// Looks in the cell's own tile sets first, then in all of them
static const DT1Tile *Map__FindTile(const Map *map, const int orientation, const int style, const int sequence,
                                    const int tx, const int ty) {
    const uint64_t mask = map->dt1_masks != NULL ? map->dt1_masks[(ty * map->width) + tx] : 0;
    const DT1Tile *tile = Map__FindTileIn(map, orientation, style, sequence, tx, ty, mask);
    return (tile == NULL && mask != 0) ? Map__FindTileIn(map, orientation, style, sequence, tx, ty, 0) : tile;
}

static void Map__ResolveCell(Map *map, const int tx, const int ty) {
    const int c = (ty * map->width) + tx;
    for (int f = 0; f < map->floor_layer_count; f++) {
        const DS1Cell *cell    = &map->floors[f][c];
        map->floor_tiles[f][c] = (cell->prop1 == 0 || cell->hidden)
                                     ? NULL
                                     : Map__FindTile(map, DT1_ORIENTATION_FLOOR, cell->style, cell->sequence, tx, ty);
    }
    for (int w = 0; w < map->wall_layer_count; w++) {
        const DS1Cell *cell         = &map->walls[w][c];
        map->wall_tiles[w][c]       = NULL;
        map->wall_extra_tiles[w][c] = NULL;
        if (cell->prop1 == 0 || cell->hidden || cell->orientation == DT1_ORIENTATION_FLOOR) {
            continue;
        }
        map->wall_tiles[w][c] = Map__FindTile(map, cell->orientation, cell->style, cell->sequence, tx, ty);
        if (cell->orientation == DT1_ORIENTATION_NW_RIGHT) {
            map->wall_extra_tiles[w][c] =
                Map__FindTile(map, DT1_ORIENTATION_NW_LEFT, cell->style, cell->sequence, tx, ty);
        }
    }
    const DS1Cell *shadow = &map->shadows[c];
    map->shadow_tiles[c]  = (shadow->prop1 == 0 || shadow->hidden)
                                ? NULL
                                : Map__FindTile(map, DT1_ORIENTATION_SHADOW, shadow->style, shadow->sequence, tx, ty);
}

static void Map__ResolveAll(Map *map) {
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            Map__ResolveCell(map, tx, ty);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Creation

static Map *Map__Allocate(const int width, const int height, const int wall_layers, const int floor_layers) {
    Map *map = calloc(1, sizeof(Map));
    FAIL_IF_NULL(map);
    map->width             = width;
    map->height            = height;
    map->wall_layer_count  = wall_layers;
    map->floor_layer_count = floor_layers;

    const size_t cells = (size_t)width * (size_t)height;
    for (int w = 0; w < MAP_WALL_LAYERS; w++) {
        map->walls[w]            = calloc(cells, sizeof(DS1Cell));
        map->wall_tiles[w]       = calloc(cells, sizeof(DT1Tile *));
        map->wall_extra_tiles[w] = calloc(cells, sizeof(DT1Tile *));
        FAIL_IF_NULL(map->walls[w]);
        FAIL_IF_NULL(map->wall_tiles[w]);
        FAIL_IF_NULL(map->wall_extra_tiles[w]);
    }
    for (int f = 0; f < MAP_FLOOR_LAYERS; f++) {
        map->floors[f]      = calloc(cells, sizeof(DS1Cell));
        map->floor_tiles[f] = calloc(cells, sizeof(DT1Tile *));
        FAIL_IF_NULL(map->floors[f]);
        FAIL_IF_NULL(map->floor_tiles[f]);
    }
    map->shadows      = calloc(cells, sizeof(DS1Cell));
    map->shadow_tiles = calloc(cells, sizeof(DT1Tile *));
    FAIL_IF_NULL(map->shadows);
    FAIL_IF_NULL(map->shadow_tiles);
    return map;
}

static bool Map__SamePath(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        const char x = *a == '/' ? '\\' : (char)tolower((unsigned char)*a);
        const char y = *b == '/' ? '\\' : (char)tolower((unsigned char)*b);
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

// Index of the tile set in map->dt1s (loaded on first use), -1 when the map has no room for more
static int Map__AddDt1(Map *map, const char *path) {
    for (int d = 0; d < map->dt1_count; d++) {
        if (Map__SamePath(map->dt1_paths[d], path)) {
            return d;
        }
    }
    if (map->dt1_count >= MAP_MAX_DT1) {
        return -1;
    }
    map->dt1_paths[map->dt1_count] = strdup(path);
    FAIL_IF_NULL(map->dt1_paths[map->dt1_count]);
    map->dt1s[map->dt1_count] = DT1_Load(path);
    return map->dt1_count++;
}

// A preset's NPCs, bosses and monsters (type 1) and objects (type 2: chests, doors, shrines, waypoints, torches, ...),
// moved by (tx, ty) tiles
static void Map__AddPresetEntities(Map *map, const DS1 *ds1, const int tx, const int ty, const unsigned flags) {
    if ((flags & MAP_STAMP_NO_ENTITIES) != 0) {
        return;
    }
    for (int o = 0; o < ds1->object_count; o++) {
        const DS1Object *object = &ds1->objects[o];
        const float      x      = (float)(object->x + (tx * MAP_SUBTILES)) + 0.5f;
        const float      y      = (float)(object->y + (ty * MAP_SUBTILES)) + 0.5f;
        if (object->type == 1 && (flags & MAP_STAMP_NO_MONSTERS) == 0) {
            const char *monster = MonsterStats_PresetMonster(map->act, object->id);
            const char *super   = MonsterStats_PresetSuperUnique(map->act, object->id);
            if (monster != NULL && monster[0] != '\0') {
                // A super unique (Bishibosh, Griswold, ...) is named "*<its name>": the game makes it tougher
                char name[MAP_ENTITY_NAME_LEN];
                snprintf(name, sizeof(name), "%s%s", super != NULL ? "*" : "", super != NULL ? super : monster);
                Map_AddEntity(map, "monsters", monster, name, x, y);
            }
        } else if (object->type == 2) {
            ObjectInfo info;
            if (Objects_Get(Objects_PresetRow(map->act, object->id), &info) && info.token[0] != '\0') {
                MapEntity *entity  = Map_AddEntity(map, "objects", info.token, info.name, x, y);
                entity->object_row = info.row;
                snprintf(entity->mode, sizeof(entity->mode), "%s", info.idle_mode);
            }
        }
    }
}

Map *Map_LoadDS1(const char *ds1_path) {
    DS1 *ds1 = DS1_Load(ds1_path);
    if (ds1 == NULL) {
        ds1 = DS1_LoadDiskFile(ds1_path); // one saved from the Map Editor
    }
    if (ds1 == NULL) {
        return NULL;
    }

    Map *map = Map__Allocate(ds1->width, ds1->height, ds1->wall_layer_count, ds1->floor_layer_count);
    map->act = ds1->act;

    const char *slash = strrchr(ds1_path, '\\');
    snprintf(map->name, sizeof(map->name), "%s", slash != NULL ? slash + 1 : ds1_path);
    char *extension = strrchr(map->name, '.');
    if (extension != NULL) {
        *extension = '\0'; // "townN1.ds1" -> "townN1"
    }

    for (int f = 0; f < ds1->file_count; f++) {
        Map__AddDt1(map, ds1->files[f]);
    }

    const size_t cells = (size_t)map->width * (size_t)map->height;
    for (int w = 0; w < ds1->wall_layer_count; w++) {
        memcpy(map->walls[w], ds1->walls[w], cells * sizeof(DS1Cell));
    }
    for (int f = 0; f < ds1->floor_layer_count; f++) {
        memcpy(map->floors[f], ds1->floors[f], cells * sizeof(DS1Cell));
    }
    memcpy(map->shadows, ds1->shadows, cells * sizeof(DS1Cell));

    Map__AddPresetEntities(map, ds1, 0, 0, 0);
    DS1_Destroy(&ds1);

    Map__BuildTileIndex(map);
    Map__ResolveAll(map);
    return map;
}

Map *Map_CreateBlank(const int width, const int height, const int act, const char *const *dt1_paths,
                     const int dt1_count) {
    const int w   = width < 1 ? 1 : (width > MAP_MAX_SIZE ? MAP_MAX_SIZE : width);
    const int h   = height < 1 ? 1 : (height > MAP_MAX_SIZE ? MAP_MAX_SIZE : height);
    Map      *map = Map__Allocate(w, h, 2, 1); // two wall layers: a wall and a roof (or decoration) per cell
    map->act      = act < 0 ? 0 : (act > 4 ? 4 : act);
    snprintf(map->name, sizeof(map->name), "Untitled");
    for (int d = 0; d < dt1_count; d++) {
        Map__AddDt1(map, dt1_paths[d]);
    }
    Map__BuildTileIndex(map);
    Map__ResolveAll(map);
    return map;
}

Map *Map_CreateEmpty(const int width, const int height, const int act) {
    const int w    = width < 1 ? 1 : (width > MAP_MAX_WORLD_SIZE ? MAP_MAX_WORLD_SIZE : width);
    const int h    = height < 1 ? 1 : (height > MAP_MAX_WORLD_SIZE ? MAP_MAX_WORLD_SIZE : height);
    Map      *map  = Map__Allocate(w, h, MAP_WALL_LAYERS, MAP_FLOOR_LAYERS);
    map->act       = act < 0 ? 0 : (act > 4 ? 4 : act);
    map->dt1_masks = calloc((size_t)w * (size_t)h, sizeof(uint64_t));
    FAIL_IF_NULL(map->dt1_masks);
    snprintf(map->name, sizeof(map->name), "Untitled");
    return map;
}

void Map_StampDS1(Map *map, const DS1 *ds1, const int tx, const int ty, const unsigned flags) {
    uint64_t mask = 0;
    for (int f = 0; f < ds1->file_count; f++) {
        const int index  = Map__AddDt1(map, ds1->files[f]);
        mask            |= (index >= 0 && index < 64) ? (1ull << index) : 0;
    }

    const bool filled_only = (flags & MAP_STAMP_FILLED_ONLY) != 0;
    for (int y = 0; y < ds1->height; y++) {
        for (int x = 0; x < ds1->width; x++) {
            const int mx = tx + x;
            const int my = ty + y;
            if (mx < 0 || my < 0 || mx >= map->width || my >= map->height) {
                continue;
            }
            const int source = (y * ds1->width) + x;
            const int target = (my * map->width) + mx;
            bool      used   = false;
            for (int f = 0; f < MAP_FLOOR_LAYERS; f++) {
                const DS1Cell *cell = f < ds1->floor_layer_count ? &ds1->floors[f][source] : NULL;
                if (cell != NULL && cell->prop1 != 0) {
                    map->floors[f][target] = *cell;
                    used                   = true;
                } else if (!filled_only) {
                    memset(&map->floors[f][target], 0, sizeof(DS1Cell));
                }
            }
            for (int w = 0; w < MAP_WALL_LAYERS; w++) {
                const DS1Cell *cell = w < ds1->wall_layer_count ? &ds1->walls[w][source] : NULL;
                if (cell != NULL && cell->prop1 != 0) {
                    map->walls[w][target] = *cell;
                    used                  = true;
                } else if (!filled_only) {
                    memset(&map->walls[w][target], 0, sizeof(DS1Cell));
                }
            }
            if (ds1->shadows[source].prop1 != 0) {
                map->shadows[target] = ds1->shadows[source];
            } else if (!filled_only) {
                memset(&map->shadows[target], 0, sizeof(DS1Cell));
            }
            if (map->dt1_masks != NULL && (used || !filled_only)) {
                // Decorations add their tile sets to what the cell already uses
                map->dt1_masks[target] = filled_only ? (map->dt1_masks[target] | mask) : mask;
            }
        }
    }
    Map__AddPresetEntities(map, ds1, tx, ty, flags);
}

void Map_StampMap(Map *map, const Map *source, const int tx, const int ty) {
    // The source's tile sets, as indices in this map
    int remap[MAP_MAX_DT1];
    for (int d = 0; d < source->dt1_count; d++) {
        remap[d] = Map__AddDt1(map, source->dt1_paths[d]);
    }
    uint64_t all = 0;
    for (int d = 0; d < source->dt1_count; d++) {
        all |= (remap[d] >= 0 && remap[d] < 64) ? (1ull << remap[d]) : 0;
    }
    for (int y = 0; y < source->height; y++) {
        for (int x = 0; x < source->width; x++) {
            const int mx = tx + x;
            const int my = ty + y;
            if (mx < 0 || my < 0 || mx >= map->width || my >= map->height) {
                continue;
            }
            const int s    = (y * source->width) + x;
            const int t    = (my * map->width) + mx;
            bool      used = false;
            for (int f = 0; f < source->floor_layer_count && f < MAP_FLOOR_LAYERS; f++) {
                if (source->floors[f][s].prop1 != 0) {
                    map->floors[f][t] = source->floors[f][s];
                    used              = true;
                }
            }
            for (int w = 0; w < source->wall_layer_count && w < MAP_WALL_LAYERS; w++) {
                if (source->walls[w][s].prop1 != 0) {
                    map->walls[w][t] = source->walls[w][s];
                    used             = true;
                }
            }
            if (source->shadows[s].prop1 != 0) {
                map->shadows[t] = source->shadows[s];
            }
            if (used && map->dt1_masks != NULL) {
                uint64_t mask = all;
                if (source->dt1_masks != NULL && source->dt1_masks[s] != 0) {
                    mask = 0;
                    for (int d = 0; d < source->dt1_count && d < 64; d++) {
                        if ((source->dt1_masks[s] & (1ull << d)) != 0 && remap[d] >= 0 && remap[d] < 64) {
                            mask |= 1ull << remap[d];
                        }
                    }
                }
                map->dt1_masks[t] = mask;
            }
        }
    }
    for (int e = 0; e < source->entity_count; e++) {
        const MapEntity *entity = &source->entities[e];
        MapEntity       *copy   = Map_AddEntity(map, entity->base, entity->id, entity->name,
                                                entity->x + (float)(tx * MAP_SUBTILES), entity->y + (float)(ty * MAP_SUBTILES));
        copy->object_row        = entity->object_row;
        snprintf(copy->mode, sizeof(copy->mode), "%s", entity->mode);
    }
}

const MapArea *Map_AreaAt(const Map *map, const float subtile_x, const float subtile_y) {
    if (map->area_of == NULL || subtile_x < 0.0f || subtile_y < 0.0f) {
        return NULL;
    }
    const int tx = (int)(subtile_x / MAP_SUBTILES);
    const int ty = (int)(subtile_y / MAP_SUBTILES);
    if (tx >= map->width || ty >= map->height) {
        return NULL;
    }
    const uint8_t index = map->area_of[(ty * map->width) + tx];
    return index < map->area_count ? &map->areas[index] : NULL;
}

const MapArea *Map_FindArea(const Map *map, const int level_id) {
    for (int a = 0; a < map->area_count; a++) {
        if (map->areas[a].level_id == level_id) {
            return &map->areas[a];
        }
    }
    return NULL;
}

void Map_Finish(Map *map) {
    Map__BuildTileIndex(map);
    Map__ResolveAll(map);
}

// Outdoors, a cell left without floor between two that have it (pieces put side by side a tile apart) shows as a
// black seam you can't cross: it gets its neighbour's floor. Twice, for seams two tiles wide.
void Map_FillSeams(Map *map) {
    for (int pass = 0; pass < 2; pass++) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                const int c     = (ty * map->width) + tx;
                bool      empty = true;
                for (int f = 0; f < map->floor_layer_count && empty; f++) {
                    empty = map->floor_tiles[f][c] == NULL;
                }
                for (int w = 0; w < map->wall_layer_count && empty; w++) {
                    empty = map->wall_tiles[w][c] == NULL;
                }
                if (!empty) {
                    continue;
                }
                const int  left  = tx > 0 ? c - 1 : -1;
                const int  right = tx + 1 < map->width ? c + 1 : -1;
                const int  up    = ty > 0 ? c - map->width : -1;
                const int  down  = ty + 1 < map->height ? c + map->width : -1;
                const bool row =
                    left >= 0 && right >= 0 && map->floor_tiles[0][left] != NULL && map->floor_tiles[0][right] != NULL;
                const bool col =
                    up >= 0 && down >= 0 && map->floor_tiles[0][up] != NULL && map->floor_tiles[0][down] != NULL;
                if (!row && !col) {
                    continue;
                }
                const int from         = row ? left : up;
                map->floors[0][c]      = map->floors[0][from];
                map->floor_tiles[0][c] = map->floor_tiles[0][from];
            }
        }
    }
}

void Map_AddExit(Map *map, const int target_level, const int x0, const int y0, const int x1, const int y1,
                 const float arrive_x, const float arrive_y) {
    if (map->exit_count >= MAP_MAX_EXITS || target_level <= 0) {
        return;
    }
    MapExit *exit      = &map->exits[map->exit_count++];
    exit->target_level = target_level;
    exit->x0           = x0 < x1 ? x0 : x1;
    exit->y0           = y0 < y1 ? y0 : y1;
    exit->x1           = x0 < x1 ? x1 : x0;
    exit->y1           = y0 < y1 ? y1 : y0;
    exit->arrive_x     = arrive_x;
    exit->arrive_y     = arrive_y;
    exit->entrance     = false;
}

const MapExit *Map_ExitAt(const Map *map, const float subtile_x, const float subtile_y) {
    const int x = (int)subtile_x;
    const int y = (int)subtile_y;
    for (int e = 0; e < map->exit_count; e++) {
        const MapExit *exit = &map->exits[e];
        if (x >= exit->x0 && x <= exit->x1 && y >= exit->y0 && y <= exit->y1) {
            return exit;
        }
    }
    return NULL;
}

const MapExit *Map_ExitTo(const Map *map, const int target_level) {
    for (int e = 0; e < map->exit_count; e++) {
        if (map->exits[e].target_level == target_level) {
            return &map->exits[e];
        }
    }
    return NULL;
}

void Map_Destroy(Map **map_ref) {
    if (map_ref == NULL || *map_ref == NULL) {
        return;
    }
    Map *map = *map_ref;
    for (int d = 0; d < map->dt1_count; d++) {
        free(map->dt1_paths[d]);
        DT1_Destroy(&map->dt1s[d]);
    }
    for (int w = 0; w < MAP_WALL_LAYERS; w++) {
        free(map->walls[w]);
        free(map->wall_tiles[w]);
        free(map->wall_extra_tiles[w]);
    }
    for (int f = 0; f < MAP_FLOOR_LAYERS; f++) {
        free(map->floors[f]);
        free(map->floor_tiles[f]);
    }
    free(map->shadows);
    free(map->shadow_tiles);
    free(map->entities);
    free(map->blocked);
    free(map->dt1_masks);
    free(map->area_of);
    free(map->tile_index);
    free(map->anim_frames);
    for (int k = 0; k < 3; k++) {
        free(map->choices[k]);
    }
    free(map);
    *map_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tiles

static DS1Cell *Map__Cell(const Map *map, const MapLayerKind kind, const int layer, const int tx, const int ty) {
    if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height) {
        return NULL;
    }
    const int c = (ty * map->width) + tx;
    switch (kind) {
    case MAP_LAYER_FLOOR:
        return (layer >= 0 && layer < map->floor_layer_count) ? &map->floors[layer][c] : NULL;
    case MAP_LAYER_WALL:
        return (layer >= 0 && layer < map->wall_layer_count) ? &map->walls[layer][c] : NULL;
    case MAP_LAYER_SHADOW:
        return &map->shadows[c];
    }
    return NULL;
}

const DS1Cell *Map_GetCell(const Map *map, const MapLayerKind kind, const int layer, const int tx, const int ty) {
    return Map__Cell(map, kind, layer, tx, ty);
}

void Map_SetCell(Map *map, const MapLayerKind kind, const int layer, const int tx, const int ty,
                 const MapTileChoice *choice) {
    DS1Cell *cell = Map__Cell(map, kind, layer, tx, ty);
    if (cell == NULL) {
        return;
    }
    memset(cell, 0, sizeof(DS1Cell));
    if (choice != NULL) {
        cell->prop1       = 1;
        cell->style       = choice->style;
        cell->sequence    = choice->sequence;
        cell->orientation = choice->orientation;
    }
    Map__ResolveCell(map, tx, ty);
}

void Map_SetCellRaw(Map *map, const MapLayerKind kind, const int layer, const int tx, const int ty,
                    const DS1Cell *value) {
    DS1Cell *cell = Map__Cell(map, kind, layer, tx, ty);
    if (cell == NULL) {
        return;
    }
    *cell = *value;
    Map__ResolveCell(map, tx, ty);
}

uint16_t Map_GetFloorMaterial(const Map *map, const int subtile_x, const int subtile_y) {
    if (subtile_x < 0 || subtile_y < 0 || subtile_x >= map->width * MAP_SUBTILES ||
        subtile_y >= map->height * MAP_SUBTILES) {
        return 0;
    }
    const int c = ((subtile_y / MAP_SUBTILES) * map->width) + (subtile_x / MAP_SUBTILES);
    for (int f = map->floor_layer_count - 1; f >= 0; f--) { // the top floor is what you stand on
        const DT1Tile *tile = map->floor_tiles[f][c];
        if (tile != NULL && tile->material != 0) {
            return tile->material;
        }
    }
    return 0;
}

bool Map_IsObjectBlocked(const Map *map, const int subtile_x, const int subtile_y) {
    if (map->blocked == NULL || subtile_x < 0 || subtile_y < 0 || subtile_x >= map->width * MAP_SUBTILES ||
        subtile_y >= map->height * MAP_SUBTILES) {
        return false;
    }
    return map->blocked[(subtile_y * map->width * MAP_SUBTILES) + subtile_x] != 0;
}

bool Map_IsWalkable(const Map *map, const int subtile_x, const int subtile_y) {
    if (subtile_x < 0 || subtile_y < 0 || subtile_x >= map->width * MAP_SUBTILES ||
        subtile_y >= map->height * MAP_SUBTILES) {
        return false;
    }
    if (map->blocked != NULL && map->blocked[(subtile_y * map->width * MAP_SUBTILES) + subtile_x] != 0) {
        return false;
    }
    const int tx = subtile_x / MAP_SUBTILES;
    const int ty = subtile_y / MAP_SUBTILES;
    const int c  = (ty * map->width) + tx;
    // Sub-tile flags are stored bottom row first
    const int sx   = subtile_x % MAP_SUBTILES;
    const int sy   = subtile_y % MAP_SUBTILES;
    const int flag = ((MAP_SUBTILES - 1 - sy) * MAP_SUBTILES) + sx;

    bool has_floor = false;
    for (int f = 0; f < map->floor_layer_count; f++) {
        const DT1Tile *tile = map->floor_tiles[f][c];
        if (tile != NULL) {
            has_floor = true;
            if (tile->subtile_flags[flag] & DT1_SUBTILE_BLOCK_WALK) {
                return false;
            }
        }
    }
    for (int w = 0; w < map->wall_layer_count; w++) {
        const DT1Tile *tiles[2] = {map->wall_tiles[w][c], map->wall_extra_tiles[w][c]};
        for (int i = 0; i < 2; i++) {
            if (tiles[i] != NULL && tiles[i]->orientation != DT1_ORIENTATION_ROOF &&
                (tiles[i]->subtile_flags[flag] & DT1_SUBTILE_BLOCK_WALK)) {
                return false;
            }
            // A lower wall with nothing on it is ground too (the walkways joining the Arcane Sanctuary's platforms
            // have no floor tile where two meet, just the edge of the walkway)
            has_floor = has_floor || (tiles[i] != NULL && tiles[i]->orientation >= DT1_ORIENTATION_LOWER_WALL &&
                                      tiles[i]->orientation <= DT1_ORIENTATION_LOWER_WALL + 3);
        }
    }
    return has_floor;
}

int Map_GetChoices(const Map *map, const MapLayerKind kind, const MapTileChoice **choices) {
    *choices = map->choices[kind];
    return map->choice_counts[kind];
}

// ---------------------------------------------------------------------------------------------------------------------
// Entities

MapEntity *Map_AddEntity(Map *map, const char *base, const char *id, const char *name, const float x, const float y) {
    if (map->entity_count >= map->entity_capacity) {
        map->entity_capacity = map->entity_capacity > 0 ? map->entity_capacity * 2 : 16;
        MapEntity *grown     = realloc(map->entities, sizeof(MapEntity) * (size_t)map->entity_capacity);
        FAIL_IF_NULL(grown);
        map->entities = grown;
    }
    MapEntity *entity = &map->entities[map->entity_count++];
    memset(entity, 0, sizeof(MapEntity));
    snprintf(entity->base, sizeof(entity->base), "%s", base);
    snprintf(entity->id, sizeof(entity->id), "%s", id);
    snprintf(entity->name, sizeof(entity->name), "%s", name != NULL ? name : id);
    entity->x          = x;
    entity->y          = y;
    entity->object_row = -1;
    if (strcmp(base, "objects") == 0) {
        // Placed by token (the editor, older saves): the first object using those graphics
        ObjectInfo info;
        if (Objects_Get(Objects_FindToken(id), &info)) {
            entity->object_row = info.row;
            snprintf(entity->mode, sizeof(entity->mode), "%s", info.idle_mode);
        }
    }
    return entity;
}

void Map_RemoveEntity(Map *map, const int index) {
    if (index < 0 || index >= map->entity_count) {
        return;
    }
    memmove(&map->entities[index], &map->entities[index + 1],
            sizeof(MapEntity) * (size_t)(map->entity_count - index - 1));
    map->entity_count--;
}

void Map_SetBlocked(Map *map, const int subtile_x, const int subtile_y, const int width, const int height,
                    const bool blocked) {
    const int map_w = map->width * MAP_SUBTILES;
    const int map_h = map->height * MAP_SUBTILES;
    if (map->blocked == NULL) {
        if (!blocked) {
            return;
        }
        map->blocked = calloc((size_t)map_w * (size_t)map_h, 1);
        FAIL_IF_NULL(map->blocked);
    }
    for (int y = subtile_y; y < subtile_y + height; y++) {
        for (int x = subtile_x; x < subtile_x + width; x++) {
            if (x >= 0 && y >= 0 && x < map_w && y < map_h) {
                map->blocked[(y * map_w) + x] = blocked ? 1 : 0;
            }
        }
    }
}

void Map_ClearBlocked(Map *map) {
    free(map->blocked);
    map->blocked = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// .abm files: plain text, one line per row of cells (hex), so they diff and merge reasonably in git.
//
//   AbyssMap 1
//   name My Map
//   size 24 24
//   act 0
//   layers 1 1          (walls floors)
//   dt1 data\global\tiles\act1\town\floor.dt1
//   floor 0             followed by <height> lines of <width> 8-digit hex cells
//   wall 0
//   shadow
//   entity monsters zombie1 60.50 42.50 0 Zombie
//   object 37 on        (for the entity above: its objects.txt row and animation)
//   end

static uint32_t Map__Encode(const DS1Cell *cell) {
    return (uint32_t)cell->prop1 | ((uint32_t)cell->sequence << 8) | ((uint32_t)(cell->orientation & 0x3F) << 14) |
           ((uint32_t)cell->style << 20) | (cell->hidden ? 0x80000000u : 0u);
}

static DS1Cell Map__Decode(const uint32_t value) {
    DS1Cell cell;
    cell.prop1       = (uint8_t)(value & 0xFF);
    cell.sequence    = (uint8_t)((value >> 8) & 0x3F);
    cell.orientation = (uint8_t)((value >> 14) & 0x3F);
    cell.style       = (uint8_t)((value >> 20) & 0x3F);
    cell.hidden      = (value >> 31) != 0;
    return cell;
}

static void Map__WriteLayer(FILE *file, const Map *map, const DS1Cell *cells) {
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            fprintf(file, tx == 0 ? "%08X" : " %08X", Map__Encode(&cells[(ty * map->width) + tx]));
        }
        fputc('\n', file);
    }
}

bool Map_SaveFile(const Map *map, const char *file_path) {
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        LOG_WARN("Could not write '%s': %s", file_path, strerror(errno));
        return false;
    }
    fprintf(file, "%s %d\n", MAP_FILE_MAGIC, MAP_FILE_VERSION);
    fprintf(file, "name %s\n", map->name);
    fprintf(file, "size %d %d\n", map->width, map->height);
    fprintf(file, "act %d\n", map->act);
    fprintf(file, "layers %d %d\n", map->wall_layer_count, map->floor_layer_count);
    for (int d = 0; d < map->dt1_count; d++) {
        fprintf(file, "dt1 %s\n", map->dt1_paths[d]);
    }
    for (int f = 0; f < map->floor_layer_count; f++) {
        fprintf(file, "floor %d\n", f);
        Map__WriteLayer(file, map, map->floors[f]);
    }
    for (int w = 0; w < map->wall_layer_count; w++) {
        fprintf(file, "wall %d\n", w);
        Map__WriteLayer(file, map, map->walls[w]);
    }
    fprintf(file, "shadow\n");
    Map__WriteLayer(file, map, map->shadows);
    for (int e = 0; e < map->entity_count; e++) {
        const MapEntity *entity = &map->entities[e];
        fprintf(file, "entity %s %s %.2f %.2f %d %s\n", entity->base, entity->id, (double)entity->x, (double)entity->y,
                entity->direction, entity->name);
        if (entity->object_row >= 0) {
            fprintf(file, "object %d %s\n", entity->object_row, entity->mode[0] != '\0' ? entity->mode : "-");
        }
    }
    fprintf(file, "end\n");
    const bool ok = ferror(file) == 0;
    fclose(file);
    return ok;
}

static void Map__TrimLine(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
}

static bool Map__ReadLayer(FILE *file, const Map *map, DS1Cell *cells) {
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            unsigned int value;
            if (fscanf(file, "%x", &value) != 1) {
                return false;
            }
            cells[(ty * map->width) + tx] = Map__Decode(value);
        }
    }
    return true;
}

Map *Map_LoadFile(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        return NULL;
    }

    char line[1024];
    int  version = 0;
    if (fgets(line, sizeof(line), file) == NULL || sscanf(line, MAP_FILE_MAGIC " %d", &version) != 1 ||
        version > MAP_FILE_VERSION) {
        LOG_WARN("'%s' is not an Abyss map.", file_path);
        fclose(file);
        return NULL;
    }

    char name[64] = "Untitled";
    int  width    = 0;
    int  height   = 0;
    int  act      = 0;
    int  walls    = 1;
    int  floors   = 1;
    Map *map      = NULL;
    bool ok       = true;

    while (ok && fgets(line, sizeof(line), file) != NULL) {
        Map__TrimLine(line);
        int index = 0;
        if (strncmp(line, "name ", 5) == 0) {
            snprintf(name, sizeof(name), "%.63s", line + 5);
        } else if (sscanf(line, "size %d %d", &width, &height) == 2) {
        } else if (sscanf(line, "act %d", &act) == 1) {
        } else if (sscanf(line, "layers %d %d", &walls, &floors) == 2) {
            if (width <= 0 || height <= 0 || width > MAP_MAX_SIZE || height > MAP_MAX_SIZE || walls < 0 ||
                walls > MAP_WALL_LAYERS || floors < 0 || floors > MAP_FLOOR_LAYERS) {
                ok = false;
                break;
            }
            map      = Map__Allocate(width, height, walls, floors);
            map->act = act < 0 ? 0 : (act > 4 ? 4 : act);
            snprintf(map->name, sizeof(map->name), "%s", name);
        } else if (map == NULL) {
            ok = strlen(line) == 0;
        } else if (strncmp(line, "dt1 ", 4) == 0) {
            Map__AddDt1(map, line + 4);
        } else if (sscanf(line, "floor %d", &index) == 1) {
            ok = index >= 0 && index < map->floor_layer_count && Map__ReadLayer(file, map, map->floors[index]);
        } else if (sscanf(line, "wall %d", &index) == 1) {
            ok = index >= 0 && index < map->wall_layer_count && Map__ReadLayer(file, map, map->walls[index]);
        } else if (strcmp(line, "shadow") == 0) {
            ok = Map__ReadLayer(file, map, map->shadows);
        } else if (strncmp(line, "entity ", 7) == 0) {
            char  base[12];
            char  id[40];
            float x;
            float y;
            int   direction;
            int   consumed = 0;
            if (sscanf(line + 7, "%11s %39s %f %f %d %n", base, id, &x, &y, &direction, &consumed) >= 5) {
                MapEntity *entity = Map_AddEntity(map, base, id, consumed > 0 ? line + 7 + consumed : id, x, y);
                entity->direction = direction;
            }
        } else if (strncmp(line, "object ", 7) == 0 && map->entity_count > 0) {
            int  row;
            char mode[4] = "";
            if (sscanf(line + 7, "%d %3s", &row, mode) >= 1) {
                MapEntity *entity  = &map->entities[map->entity_count - 1];
                entity->object_row = row;
                snprintf(entity->mode, sizeof(entity->mode), "%s", strcmp(mode, "-") == 0 ? "" : mode);
            }
        } else if (strcmp(line, "end") == 0) {
            break;
        }
    }
    fclose(file);

    if (!ok || map == NULL) {
        LOG_WARN("'%s' is damaged.", file_path);
        Map_Destroy(&map);
        return NULL;
    }
    Map__BuildTileIndex(map);
    Map__ResolveAll(map);
    return map;
}

const char *Map_GetSaveFolder(void) {
    static char folder[4096];
    snprintf(folder, sizeof(folder), "%s/maps", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
    _mkdir(folder);
#else
    mkdir(folder, 0755);
#endif
    return folder;
}

static int Map__CompareNames(const void *a, const void *b) {
    const char *x = *(const char *const *)a;
    const char *y = *(const char *const *)b;
    for (; *x != '\0' && tolower((unsigned char)*x) == tolower((unsigned char)*y); x++, y++) {
    }
    return tolower((unsigned char)*x) - tolower((unsigned char)*y);
}

static void Map__AddName(char ***names, int *count, int *capacity, const char *file_name) {
    const size_t len = strlen(file_name);
    if (len <= 4 || strcmp(file_name + len - 4, ".abm") != 0) {
        return;
    }
    if (*count == *capacity) {
        *capacity    = *capacity > 0 ? *capacity * 2 : 16;
        char **grown = realloc(*names, sizeof(char *) * (size_t)*capacity);
        FAIL_IF_NULL(grown);
        *names = grown;
    }
    char *name = strdup(file_name);
    FAIL_IF_NULL(name);
    name[len - 4]        = '\0';
    (*names)[(*count)++] = name;
}

int Map_ListSavedMaps(char ***names) {
    *names       = NULL;
    int count    = 0;
    int capacity = 0;
#ifdef _WIN32
    char pattern[4200];
    snprintf(pattern, sizeof(pattern), "%s\\*.abm", Map_GetSaveFolder());
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            Map__AddName(names, &count, &capacity, found.cFileName);
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(Map_GetSaveFolder());
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            Map__AddName(names, &count, &capacity, entry->d_name);
        }
        closedir(dir);
    }
#endif
    if (count > 1) {
        qsort(*names, (size_t)count, sizeof(char *), Map__CompareNames);
    }
    return count;
}

void Map_FreeNames(char **names, const int count) {
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

// ---------------------------------------------------------------------------------------------------------------------
// Saving as the game's DS1: the layers as they are, the tile sets, and the monsters and objects that have a preset
// number in the act's tables (MonPreset.txt, objects.txt's presets); the rest (Free Roam drops, the engine's own) are
// left out

static int Map__MonsterPreset(const int act, const char *id) {
    for (int index = 0; index < 1024; index++) {
        const char *monster = MonsterStats_PresetMonster(act, index);
        if (monster != NULL && strcmp(monster, id) == 0) {
            return index;
        }
    }
    return -1;
}

static int Map__ObjectPreset(const int act, const int row) {
    for (int id = 0; id < 1024 && row >= 0; id++) {
        if (Objects_PresetRow(act, id) == row) {
            return id;
        }
    }
    return -1;
}

bool Map_SaveDS1(const Map *map, const char *file_path) {
    DS1 ds1;
    memset(&ds1, 0, sizeof(ds1));
    ds1.version           = 18;
    ds1.width             = map->width;
    ds1.height            = map->height;
    ds1.act               = map->act;
    ds1.file_count        = map->dt1_count;
    ds1.files             = (char **)map->dt1_paths;
    ds1.wall_layer_count  = map->wall_layer_count;
    ds1.floor_layer_count = map->floor_layer_count;
    for (int w = 0; w < map->wall_layer_count; w++) {
        ds1.walls[w] = map->walls[w];
    }
    for (int f = 0; f < map->floor_layer_count; f++) {
        ds1.floors[f] = map->floors[f];
    }
    ds1.shadows = map->shadows;
    ds1.objects = calloc(map->entity_count > 0 ? (size_t)map->entity_count : 1, sizeof(DS1Object));
    FAIL_IF_NULL(ds1.objects);
    int skipped = 0;
    for (int e = 0; e < map->entity_count; e++) {
        const MapEntity *entity = &map->entities[e];
        const bool       object = strcmp(entity->base, "objects") == 0;
        const int        id     = object ? Map__ObjectPreset(map->act, entity->object_row)
                                         : (strcmp(entity->base, "monsters") == 0 ? Map__MonsterPreset(map->act, entity->id) : -1);
        if (id < 0) {
            skipped++;
            continue;
        }
        DS1Object *out = &ds1.objects[ds1.object_count++];
        out->type      = object ? 2 : 1;
        out->id        = id;
        out->x         = (int32_t)entity->x;
        out->y         = (int32_t)entity->y;
    }
    const bool ok = DS1_Save(&ds1, file_path);
    free(ds1.objects);
    if (skipped > 0) {
        LOG_INFO("Map: %d units with no preset number left out of '%s'", skipped, file_path);
    }
    return ok;
}
