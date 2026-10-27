#include "Places.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "../types/DS1.h"
#include "LevelBuilder.h"
#include "Map.h"
#include "World.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_LVLPREST "/data/global/excel/LvlPrest.txt"
#define EXCEL_LEVELS   "/data/global/excel/Levels.txt"

// Whole-area presets left out
static const int excluded_levels[] = {
    136, // Uber Tristram (a copy of Tristram)
};

static Place *places      = NULL;
static int    place_count = -1;

static bool Places__Excluded(const int level_id) {
    for (size_t i = 0; i < sizeof(excluded_levels) / sizeof(excluded_levels[0]); i++) {
        if (excluded_levels[i] == level_id) {
            return true;
        }
    }
    return false;
}

static int Places__CompareLevel(const void *a, const void *b) {
    return ((const Place *)a)->level_id - ((const Place *)b)->level_id;
}

// An area's name in the game's language ("Hell1" is Abaddon), without the odd trailing space Levels.txt has
static const char *Places__Name(DataTable *levels, const int row, const int column) {
    static char name[64];
    snprintf(name, sizeof(name), "%s", StringTable_Get(DataTable_GetString(levels, row, column)));
    for (size_t n = strlen(name); n > 0 && name[n - 1] == ' '; n--) {
        name[n - 1] = '\0';
    }
    return name;
}

static void Places__Load(void) {
    place_count       = 0;
    DataTable *preset = DataTable_Load(EXCEL_LVLPREST);
    DataTable *levels = DataTable_Load(EXCEL_LEVELS);
    if (preset == NULL || levels == NULL) {
        LOG_WARN("Could not load the level tables for Free Roam");
        DataTable_Destroy(&preset);
        DataTable_Destroy(&levels);
        return;
    }

    const int rows = DataTable_GetRowCount(preset);
    places         = calloc(rows > 0 ? (size_t)rows : 1, sizeof(Place));
    FAIL_IF_NULL(places);

    const int c_level = DataTable_GetColumnIndex(preset, "LevelId");
    int       c_files[PLACE_MAX_FILES];
    for (int f = 0; f < PLACE_MAX_FILES; f++) {
        char column[8];
        snprintf(column, sizeof(column), "File%d", f + 1);
        c_files[f] = DataTable_GetColumnIndex(preset, column);
    }
    const int c_id   = DataTable_GetColumnIndex(levels, "Id");
    const int c_name = DataTable_GetColumnIndex(levels, "LevelName");
    const int c_act  = DataTable_GetColumnIndex(levels, "Act");

    for (int row = 0; row < rows; row++) {
        const int level_id = DataTable_GetInt(preset, row, c_level);
        if (level_id <= 0 || Places__Excluded(level_id)) {
            continue;
        }

        Place *place = &places[place_count];
        memset(place, 0, sizeof(Place));
        place->level_id = level_id;
        for (int f = 0; f < PLACE_MAX_FILES; f++) {
            const char *file = DataTable_GetString(preset, row, c_files[f]);
            if (file[0] == '\0' || strcmp(file, "0") == 0) {
                continue;
            }
            char *path = place->files[place->file_count];
            snprintf(path, sizeof(place->files[0]), "data\\global\\tiles\\%s", file);
            for (char *c = path; *c != '\0'; c++) {
                if (*c == '/') {
                    *c = '\\';
                }
            }
            if (FileManager_FileExists(path)) {
                place->file_count++;
            }
        }
        if (place->file_count == 0) {
            continue;
        }

        snprintf(place->name, sizeof(place->name), "Level %d", level_id);
        for (int l = 0; l < DataTable_GetRowCount(levels); l++) {
            if (DataTable_GetInt(levels, l, c_id) == level_id) {
                snprintf(place->name, sizeof(place->name), "%s", Places__Name(levels, l, c_name));
                place->act = DataTable_GetInt(levels, l, c_act);
                break;
            }
        }
        place_count++;
    }

    // The randomly built areas
    const int level_rows = DataTable_GetRowCount(levels);
    Place    *grown      = realloc(places, sizeof(Place) * (size_t)(place_count + level_rows + 1));
    FAIL_IF_NULL(grown);
    places = grown;
    for (int l = 0; l < level_rows; l++) {
        const int level_id = DataTable_GetInt(levels, l, c_id);
        if (level_id <= 0 || !DataTable_HasValue(levels, l, c_id) || !LevelBuilder_CanBuild(level_id)) {
            continue;
        }
        Place *place = &places[place_count++];
        memset(place, 0, sizeof(Place));
        place->level_id  = level_id;
        place->generated = true;
        place->act       = DataTable_GetInt(levels, l, c_act);
        snprintf(place->name, sizeof(place->name), "%s", Places__Name(levels, l, c_name));
    }
    qsort(places, (size_t)place_count, sizeof(Place), Places__CompareLevel);

    // Areas sharing a name within an act (the seven Tal Rasha's Tombs) get a number
    int *numbers = calloc((size_t)place_count + 1, sizeof(int));
    FAIL_IF_NULL(numbers);
    for (int i = 0; i < place_count; i++) {
        int before = 0;
        int total  = 0;
        for (int j = 0; j < place_count; j++) {
            if (places[j].act == places[i].act && strcmp(places[j].name, places[i].name) == 0) {
                before += j < i ? 1 : 0;
                total++;
            }
        }
        numbers[i] = total > 1 ? before + 1 : 0;
    }
    for (int i = 0; i < place_count; i++) {
        if (numbers[i] > 0) {
            const size_t len = strnlen(places[i].name, sizeof(places[i].name) - 5);
            snprintf(places[i].name + len, sizeof(places[i].name) - len, " %d", numbers[i]);
        }
    }
    free(numbers);

    DataTable_Destroy(&preset);
    DataTable_Destroy(&levels);
}

// How big an area's map is (tiles): built areas from Levels.txt, fixed ones from their preset (the version the seed
// picks). For the world layout.
static bool Places__AreaSize(const int level_id, int *width, int *height) {
    static int   sizes[256][2];
    static int   seeds[256];
    const Place *place = NULL;
    for (int i = 0; i < place_count && place == NULL; i++) {
        place = places[i].level_id == level_id ? &places[i] : NULL;
    }
    if (place == NULL || level_id <= 0 || level_id >= 256) {
        return false;
    }
    const int variant = place->generated ? 0 : World_PickVariant(level_id, place->file_count);
    if (sizes[level_id][0] == 0 || seeds[level_id] != variant + 1) {
        sizes[level_id][0] = 0;
        if (place->generated) {
            DataTable *levels = DataTable_Load(EXCEL_LEVELS);
            const int  c_id   = DataTable_GetColumnIndex(levels, "Id");
            for (int row = 0; levels != NULL && row < DataTable_GetRowCount(levels); row++) {
                if (DataTable_HasValue(levels, row, c_id) && DataTable_GetInt(levels, row, c_id) == level_id) {
                    sizes[level_id][0] = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "SizeX")) + 1;
                    sizes[level_id][1] = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "SizeY")) + 1;
                    break;
                }
            }
            DataTable_Destroy(&levels);
        } else {
            DS1 *ds1 = DS1_Load(place->files[variant]);
            if (ds1 != NULL) {
                sizes[level_id][0] = ds1->width;
                sizes[level_id][1] = ds1->height;
                DS1_Destroy(&ds1);
            }
        }
        seeds[level_id] = variant + 1;
    }
    if (sizes[level_id][0] <= 0) {
        return false;
    }
    *width  = sizes[level_id][0];
    *height = sizes[level_id][1];
    return true;
}

int Places_GetCount(void) {
    if (place_count < 0) {
        Places__Load();
        World_SetAreaSizes(Places__AreaSize);
    }
    return place_count;
}

// One area on its own
static Map *Places__LoadArea(const Place *place) {
    Map *map = NULL;
    if (place->generated) {
        map = LevelBuilder_Build(place->level_id, World_LevelSeed(place->level_id));
    } else {
        // Areas with several versions (the encampment's exit to the north, east or south): the world seed picks one
        map = Map_LoadDS1(place->files[World_PickVariant(place->level_id, place->file_count)]);
        if (map != NULL) {
            World_AddFixedExits(map, place->level_id);
        }
    }
    if (map != NULL) {
        snprintf(map->name, sizeof(map->name), "%s", place->name);
        map->seed = World_GetSeed();
        World_EnsureExits(map, place->level_id);
    }
    return map;
}

static const Place *Places__ByLevel(const int level_id) {
    for (int i = 0; i < Places_GetCount(); i++) {
        if (places[i].level_id == level_id) {
            return &places[i];
        }
    }
    return NULL;
}

// The middle of a map's exit to an area (sub-tiles); the middle of that side if it has none
static void Places__ExitMiddle(const Map *map, const int target, const WorldSide side, float *x, float *y) {
    const MapExit *exit = Map_ExitTo(map, target);
    if (exit != NULL) {
        *x = (float)(exit->x0 + exit->x1 + 1) / 2.0f;
        *y = (float)(exit->y0 + exit->y1 + 1) / 2.0f;
        return;
    }
    const float w = (float)(map->width * MAP_SUBTILES);
    const float h = (float)(map->height * MAP_SUBTILES);
    *x            = side == WORLD_SIDE_W ? 0.0f : side == WORLD_SIDE_E ? w : w / 2.0f;
    *y            = side == WORLD_SIDE_N ? 0.0f : side == WORLD_SIDE_S ? h : h / 2.0f;
}

// How well two maps join along one side: sub-tiles where a path on one side carries on into the other. offset is
// where the second map is along the join, from the first map's corner (tiles).
static int Places__JoinScore(const Map *a, const Map *b, const WorldSide side, const int offset) {
    const bool along_x = side == WORLD_SIDE_N || side == WORLD_SIDE_S;
    const int  length  = (along_x ? a->width : a->height) * MAP_SUBTILES;
    int        score   = 0;
    for (int p = 0; p < length; p++) {
        const int q = p - (offset * MAP_SUBTILES); // along the second map
        if (q < 0 || q >= (along_x ? b->width : b->height) * MAP_SUBTILES) {
            continue;
        }
        // A path that runs on into both: open a tile and three tiles inside each map's edge (the shared edge row
        // itself is the second map's; a border's outer strip is open ground too, but not three tiles in)
        bool walk_a = true;
        bool walk_b = true;
        for (int depth = 1; depth <= 3; depth += 2) {
            const int in_a = (depth * MAP_SUBTILES) + 2;
            const int in_b = (depth * MAP_SUBTILES) + 2;
            switch (side) {
            case WORLD_SIDE_S:
                walk_a = walk_a && Map_IsWalkable(a, p, (a->height * MAP_SUBTILES) - 1 - in_a);
                walk_b = walk_b && Map_IsWalkable(b, q, in_b);
                break;
            case WORLD_SIDE_N:
                walk_a = walk_a && Map_IsWalkable(a, p, in_a);
                walk_b = walk_b && Map_IsWalkable(b, q, (b->height * MAP_SUBTILES) - 1 - in_b);
                break;
            case WORLD_SIDE_E:
                walk_a = walk_a && Map_IsWalkable(a, (a->width * MAP_SUBTILES) - 1 - in_a, p);
                walk_b = walk_b && Map_IsWalkable(b, in_b, q);
                break;
            default:
                walk_a = walk_a && Map_IsWalkable(a, in_a, p);
                walk_b = walk_b && Map_IsWalkable(b, (b->width * MAP_SUBTILES) - 1 - in_b, q);
                break;
            }
        }
        score += walk_a && walk_b ? 1 : 0;
    }
    return score;
}

// An act's outdoors as one map, like the game: the town and every area you can walk to from it, side by side where
// they join, so walking from one into the next is just walking. Each area is built on its own from the world seed
// (the same as when it's loaded alone), then placed so the way out of one meets the way into the next.
// The area the act's joined outdoors grows from: the lowest Levels.txt Id walkable to from here (the town, mostly).
// Growing from it whichever area you come in by gives everyone the same map (same areas, same places): multiplayer
// games need that, as they tell each other where they are in it.
static int Places__OutdoorsRoot(const int level_id) {
    int seen[64];
    int count     = 0;
    int lowest    = level_id;
    seen[count++] = level_id;
    for (int i = 0; i < count; i++) {
        int       targets[4];
        WorldSide sides[4];
        const int links = World_GetWalkLinks(seen[i], targets, sides, 4);
        for (int l = 0; l < links && count < 64; l++) {
            bool known = Places__ByLevel(targets[l]) == NULL;
            for (int k = 0; k < count && !known; k++) {
                known = seen[k] == targets[l];
            }
            if (!known) {
                seen[count++] = targets[l];
                lowest        = targets[l] < lowest ? targets[l] : lowest;
            }
        }
    }
    return lowest;
}

static Map *Places__LoadOutdoorsFrom(const Place *requested, int root);

static Map *Places__LoadOutdoors(const Place *requested) {
    const int root = Places__OutdoorsRoot(requested->level_id);
    return Places__LoadOutdoorsFrom(
        requested, root != requested->level_id && Places__ByLevel(root) != NULL ? root : requested->level_id);
}

static Map *Places__LoadOutdoorsFrom(const Place *requested, const int root) {
    int  levels[MAP_MAX_AREAS];
    Map *maps[MAP_MAX_AREAS];
    int  pos_x[MAP_MAX_AREAS]; // tiles
    int  pos_y[MAP_MAX_AREAS];
    int  count      = 0;
    levels[count++] = root;

    // Every area joined on foot (breadth first, so each one is placed next to one already placed)
    int parent[MAP_MAX_AREAS];
    parent[0] = -1;
    for (int i = 0; i < count; i++) {
        int       targets[4];
        WorldSide sides[4];
        const int links = World_GetWalkLinks(levels[i], targets, sides, 4);
        for (int l = 0; l < links && count < MAP_MAX_AREAS; l++) {
            bool known = Places__ByLevel(targets[l]) == NULL;
            for (int k = 0; k < count && !known; k++) {
                known = levels[k] == targets[l];
            }
            if (!known) {
                parent[count]   = i;
                levels[count++] = targets[l];
            }
        }
    }

    bool has_requested = false;
    for (int i = 0; i < count; i++) {
        has_requested = has_requested || levels[i] == requested->level_id;
    }
    if (!has_requested) {
        return Places__LoadOutdoorsFrom(requested, requested->level_id); // too far from the root to be joined to it
    }
    for (int i = 0; i < count; i++) {
        maps[i] = Places__LoadArea(Places__ByLevel(levels[i]));
        if (maps[i] == NULL) {
            for (int k = 0; k < i; k++) {
                Map_Destroy(&maps[k]);
            }
            return Places__LoadArea(requested);
        }
    }

    // Placed one after another: the next area's side touching this one's, their ways through lined up. Neighbouring
    // maps share their edge row (the last row of a preset is the first of the next).
    pos_x[0] = 0;
    pos_y[0] = 0;
    for (int i = 1; i < count; i++) {
        const int p = parent[i];
        int       targets[4];
        WorldSide sides[4];
        const int links = World_GetWalkLinks(levels[p], targets, sides, 4);
        WorldSide side  = WORLD_SIDE_E;
        for (int l = 0; l < links; l++) {
            side = targets[l] == levels[i] ? sides[l] : side;
        }
        const WorldSide back = side == WORLD_SIDE_N   ? WORLD_SIDE_S
                               : side == WORLD_SIDE_S ? WORLD_SIDE_N
                               : side == WORLD_SIDE_E ? WORLD_SIDE_W
                                                      : WORLD_SIDE_E;
        float           ax;
        float           ay;
        float           bx;
        float           by;
        Places__ExitMiddle(maps[p], levels[i], side, &ax, &ay);
        Places__ExitMiddle(maps[i], levels[p], back, &bx, &by);
        switch (side) {
        case WORLD_SIDE_E:
            pos_x[i] = pos_x[p] + maps[p]->width - 1;
            pos_y[i] = pos_y[p] + (int)lroundf((ay - by) / MAP_SUBTILES);
            break;
        case WORLD_SIDE_W:
            pos_x[i] = pos_x[p] - maps[i]->width + 1;
            pos_y[i] = pos_y[p] + (int)lroundf((ay - by) / MAP_SUBTILES);
            break;
        case WORLD_SIDE_S:
            pos_y[i] = pos_y[p] + maps[p]->height - 1;
            pos_x[i] = pos_x[p] + (int)lroundf((ax - bx) / MAP_SUBTILES);
            break;
        default:
            pos_y[i] = pos_y[p] - maps[i]->height + 1;
            pos_x[i] = pos_x[p] + (int)lroundf((ax - bx) / MAP_SUBTILES);
            break;
        }
        // Then slid along the join to where the most of the two paths meet (the middles of the ways through aren't
        // always where the paths are: a town's edge can be open well away from its road)
        const bool along_x = side == WORLD_SIDE_N || side == WORLD_SIDE_S;
        int        best    = 0;
        int score = Places__JoinScore(maps[p], maps[i], side, along_x ? pos_x[i] - pos_x[p] : pos_y[i] - pos_y[p]);
        for (int shift = -16; shift <= 16; shift++) {
            const int offset = (along_x ? pos_x[i] - pos_x[p] : pos_y[i] - pos_y[p]) + shift;
            const int s      = Places__JoinScore(maps[p], maps[i], side, offset) - abs(shift); // nearer is better
            if (s > score) {
                score = s;
                best  = shift;
            }
        }
        *(along_x ? &pos_x[i] : &pos_y[i]) += best;
    }

    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    for (int i = 0; i < count; i++) {
        min_x = pos_x[i] < min_x ? pos_x[i] : min_x;
        min_y = pos_y[i] < min_y ? pos_y[i] : min_y;
        max_x = pos_x[i] + maps[i]->width > max_x ? pos_x[i] + maps[i]->width : max_x;
        max_y = pos_y[i] + maps[i]->height > max_y ? pos_y[i] + maps[i]->height : max_y;
    }
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < i; k++) {
            const bool overlap = pos_x[i] + 1 < pos_x[k] + maps[k]->width && pos_x[k] + 1 < pos_x[i] + maps[i]->width &&
                                 pos_y[i] + 1 < pos_y[k] + maps[k]->height && pos_y[k] + 1 < pos_y[i] + maps[i]->height;
            if (overlap) {
                LOG_WARN("%s overlaps %s", maps[i]->name, maps[k]->name);
            }
        }
    }
    if (max_x - min_x > MAP_MAX_WORLD_SIZE || max_y - min_y > MAP_MAX_WORLD_SIZE) {
        LOG_WARN("Outdoors too big to join (%dx%d): loading %s alone", max_x - min_x, max_y - min_y, requested->name);
        for (int i = 0; i < count; i++) {
            Map_Destroy(&maps[i]);
        }
        return Places__LoadArea(requested);
    }

    Map *world = Map_CreateEmpty(max_x - min_x, max_y - min_y, maps[0]->act);
    snprintf(world->name, sizeof(world->name), "%s", requested->name);
    world->seed    = World_GetSeed();
    world->area_of = malloc((size_t)world->width * (size_t)world->height);
    FAIL_IF_NULL(world->area_of);
    memset(world->area_of, 0xFF, (size_t)world->width * (size_t)world->height);

    for (int i = 0; i < count; i++) {
        const Map *map = maps[i];
        const int  ox  = pos_x[i] - min_x;
        const int  oy  = pos_y[i] - min_y;
        Map_StampMap(world, map, ox, oy);

        MapArea *area  = &world->areas[world->area_count];
        area->level_id = levels[i];
        snprintf(area->name, sizeof(area->name), "%s", Places__ByLevel(levels[i])->name);
        area->x0 = ox;
        area->y0 = oy;
        area->x1 = ox + map->width - 1;
        area->y1 = oy + map->height - 1;
        area->start_x =
            (float)(ox * MAP_SUBTILES) + (map->has_start ? map->start_x : (float)(map->width * MAP_SUBTILES) / 2.0f);
        area->start_y =
            (float)(oy * MAP_SUBTILES) + (map->has_start ? map->start_y : (float)(map->height * MAP_SUBTILES) / 2.0f);
        // Its tiles (the ones it has something on; shared edge rows go to whoever has something there)
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                const int s   = (y * map->width) + x;
                bool      has = false;
                for (int f = 0; f < map->floor_layer_count && !has; f++) {
                    has = map->floors[f][s].prop1 != 0;
                }
                if (has || world->area_of[((oy + y) * world->width) + ox + x] == 0xFF) {
                    world->area_of[((oy + y) * world->width) + ox + x] = (uint8_t)world->area_count;
                }
            }
        }

        // Its exits, except the ways into the areas joined here (those are just walked through now)
        for (int e = 0; e < map->exit_count; e++) {
            const MapExit *exit   = &map->exits[e];
            bool           joined = false;
            for (int k = 0; k < count && !exit->entrance; k++) {
                joined = joined || levels[k] == exit->target_level;
            }
            if (joined) {
                continue;
            }
            const int dx     = ox * MAP_SUBTILES;
            const int dy     = oy * MAP_SUBTILES;
            const int before = world->exit_count;
            Map_AddExit(world, exit->target_level, exit->x0 + dx, exit->y0 + dy, exit->x1 + dx, exit->y1 + dy,
                        exit->arrive_x + (float)dx, exit->arrive_y + (float)dy);
            if (world->exit_count > before) {
                world->exits[before].entrance = exit->entrance;
            }
        }
        world->area_count++;
    }
    for (int i = 0; i < count; i++) {
        Map_Destroy(&maps[i]);
    }
    Map_Finish(world);

    world->has_start = true;
    world->start_x   = world->areas[0].start_x;
    world->start_y   = world->areas[0].start_y;
    for (int a = 0; a < world->area_count; a++) {
        if (world->areas[a].level_id == requested->level_id) {
            world->start_x = world->areas[a].start_x; // the requested area
            world->start_y = world->areas[a].start_y;
        }
    }
    snprintf(world->name, sizeof(world->name), "%s", requested->name);
    return world;
}

Map *Places_LoadMap(const Place *place) {
    if (place == NULL) {
        return NULL;
    }
    int       targets[4];
    WorldSide sides[4];
    if (World_GetWalkLinks(place->level_id, targets, sides, 4) > 0) {
        return Places__LoadOutdoors(place);
    }
    return Places__LoadArea(place);
}

const Place *Places_Get(const int index) { return (index >= 0 && index < Places_GetCount()) ? &places[index] : NULL; }

void Places_Release(void) {
    free(places);
    places      = NULL;
    place_count = -1;
}
