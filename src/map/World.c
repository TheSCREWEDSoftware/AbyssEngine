#include "World.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "LevelBuilder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXCEL_LEVELS "/data/global/excel/Levels.txt"

// The areas the game joins on foot (it keeps these in its program). Pairs of Levels.txt Ids.
static const int walk_links[][2] = {
    // Act 1: Rogue Encampment - Blood Moor - Cold Plains - Stony Field - Dark Wood - Black Marsh - Tamoe Highland,
    // and the Burial Grounds off the Cold Plains
    {1,   2  },
    {2,   3  },
    {3,   4  },
    {4,   5  },
    {5,   6  },
    {6,   7  },
    {3,   17 },
    // Act 2: Lut Gholein - Rocky Waste - Dry Hills - Far Oasis - Lost City - Valley of Snakes
    {40,  41 },
    {41,  42 },
    {42,  43 },
    {43,  44 },
    {44,  45 },
    // Act 4: The Pandemonium Fortress - Outer Steppes - Plains of Despair - City of the Damned
    {103, 104},
    {104, 105},
    {105, 106},
    // Act 5: Harrogath - Bloody Foothills - Rigid Highlands - Arreat Plateau
    {109, 110},
    {110, 111},
    {111, 112},
};
#define WALK_LINK_COUNT (int)(sizeof(walk_links) / sizeof(walk_links[0]))

#define BLOODY_FOOTHILLS 110

static uint32_t   world_seed = 0;
static DataTable *levels     = NULL;

static uint32_t World__Mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void World_SetSeed(uint32_t seed) {
    if (seed == 0) {
        seed = World__Mix((uint32_t)time(NULL) ^ ((uint32_t)rand() << 8) ^ (uint32_t)clock());
        seed = seed % 1000000000u; // easy to read and type back in
        seed = seed != 0 ? seed : 1;
    }
    world_seed = seed;
    LOG_INFO("World seed %u", world_seed);
}

uint32_t World_GetSeed(void) {
    if (world_seed == 0) {
        World_SetSeed(0);
    }
    return world_seed;
}

uint32_t World_LevelSeed(const int level_id) {
    const uint32_t seed = World__Mix(World_GetSeed() ^ World__Mix((uint32_t)level_id * 0x9E3779B9u));
    return seed != 0 ? seed : 1;
}

int World_PickVariant(const int level_id, const int variants) {
    return variants > 1 ? (int)(World_LevelSeed(level_id) % (uint32_t)variants) : 0;
}

static WorldSide World__Opposite(const WorldSide side) {
    static const WorldSide opposite[4] = {WORLD_SIDE_S, WORLD_SIDE_N, WORLD_SIDE_W, WORLD_SIDE_E};
    return side < WORLD_SIDE_NONE ? opposite[side] : WORLD_SIDE_NONE;
}

// The side a town's way out is on, by which of its versions the seed picked (the encampment's exit is to the north,
// east, south or west; Lut Gholein's to the west or north). Must match the order Places lists the files in.
static WorldSide World__TownSide(const int level_id) {
    switch (level_id) {
    case 1: {
        static const WorldSide sides[4] = {WORLD_SIDE_N, WORLD_SIDE_E, WORLD_SIDE_S, WORLD_SIDE_W}; // TownN1 ... TownW1
        return sides[World_PickVariant(level_id, 4)];
    }
    case 40: {
        static const WorldSide sides[2] = {WORLD_SIDE_W, WORLD_SIDE_N}; // LutW, LutN
        return sides[World_PickVariant(level_id, 2)];
    }
    case 103:
        return WORLD_SIDE_E; // the Pandemonium Fortress
    case 109:
        return WORLD_SIDE_W; // Harrogath
    default:
        return WORLD_SIDE_NONE;
    }
}

// The layout of the areas joined on foot: each link has a side in each of its two areas, always opposite each other
// (leave the encampment to the west, come into the Blood Moor on its east edge). Worked out once per seed, from the
// towns outwards, keeping each area on a grid cell of its own so a chain never doubles back onto itself.
typedef struct WorldLayout {
    uint32_t  seed;
    WorldSide side_a[WALK_LINK_COUNT]; // the side of walk_links[l][0] that leads to walk_links[l][1]
    WorldSide side_b[WALK_LINK_COUNT]; // and back
} WorldLayout;

static WorldLayout layout = {0};

static void World__Step(const WorldSide side, int *x, int *y) {
    *x += side == WORLD_SIDE_E ? 1 : side == WORLD_SIDE_W ? -1 : 0;
    *y += side == WORLD_SIDE_S ? 1 : side == WORLD_SIDE_N ? -1 : 0;
}

static void World__Layout(void) {
    if (layout.seed == World_GetSeed()) {
        return;
    }
    layout.seed = World_GetSeed();

    // Grid cells, per level Id (Levels.txt Ids are below 256); acts are laid out apart from each other
    static int  cell_x[256];
    static int  cell_y[256];
    static bool placed[256];
    memset(placed, 0, sizeof(placed));
    bool used_sides[256][4];
    memset(used_sides, 0, sizeof(used_sides));

    for (int l = 0; l < WALK_LINK_COUNT; l++) {
        const int a = walk_links[l][0];
        const int b = walk_links[l][1];
        if (!placed[a]) {
            placed[a] = true;
            cell_x[a] = a * 100; // a town: its act gets a grid of its own
            cell_y[a] = 0;
        }

        WorldSide side = World__TownSide(a);
        if (a == BLOODY_FOOTHILLS) {
            side = WORLD_SIDE_W; // the strips run west from Harrogath, which is on their east
        }
        if (side == WORLD_SIDE_NONE || used_sides[a][side]) {
            // A free side whose cell is free too, starting from one the seed picks; any free side if none is
            const int first = (int)(World__Mix(World_LevelSeed(a) + (uint32_t)b) % 4u);
            side            = WORLD_SIDE_NONE;
            for (int pass = 0; pass < 2 && side == WORLD_SIDE_NONE; pass++) {
                for (int k = 0; k < 4; k++) {
                    const WorldSide candidate = (WorldSide)((first + k) % 4);
                    if (used_sides[a][candidate]) {
                        continue;
                    }
                    int x = cell_x[a];
                    int y = cell_y[a];
                    World__Step(candidate, &x, &y);
                    bool taken = false;
                    for (int other = 0; other < 256 && pass == 0 && !taken; other++) {
                        taken = placed[other] && cell_x[other] == x && cell_y[other] == y;
                    }
                    if (!taken) {
                        side = candidate;
                        break;
                    }
                }
            }
        }
        if (side == WORLD_SIDE_NONE) {
            side = WORLD_SIDE_N; // an area with more than four links (none in the game's)
        }
        const WorldSide back = World__Opposite(side);
        layout.side_a[l]     = side;
        layout.side_b[l]     = back;
        used_sides[a][side]  = true;
        used_sides[b][back]  = true;
        placed[b]            = true;
        cell_x[b]            = cell_x[a];
        cell_y[b]            = cell_y[a];
        World__Step(side, &cell_x[b], &cell_y[b]);
    }
}

int World_GetWalkLinks(const int level_id, int *targets, WorldSide *sides, const int max) {
    World__Layout();
    int count = 0;
    for (int l = 0; l < WALK_LINK_COUNT && count < max; l++) {
        if (walk_links[l][0] == level_id) {
            targets[count] = walk_links[l][1];
            sides[count++] = layout.side_a[l];
        } else if (walk_links[l][1] == level_id) {
            targets[count] = walk_links[l][0];
            sides[count++] = layout.side_b[l];
        }
    }
    // The way back (lower Id) first
    for (int i = 1; i < count; i++) {
        for (int j = i; j > 0 && targets[j - 1] > targets[j]; j--) {
            const int       t = targets[j - 1];
            const WorldSide s = sides[j - 1];
            targets[j - 1]    = targets[j];
            sides[j - 1]      = sides[j];
            targets[j]        = t;
            sides[j]          = s;
        }
    }
    return count;
}

int World_GetVis(const int level_id, const int slot) {
    if (slot < 0 || slot > 7) {
        return 0;
    }
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return 0;
    }
    const int c_id = DataTable_GetColumnIndex(levels, "Id");
    char      vis_column[8];
    snprintf(vis_column, sizeof(vis_column), "Vis%d", slot);
    const int c_vis = DataTable_GetColumnIndex(levels, vis_column);
    for (int row = 0; row < DataTable_GetRowCount(levels); row++) {
        if (DataTable_HasValue(levels, row, c_id) && DataTable_GetInt(levels, row, c_id) == level_id) {
            const int target = DataTable_GetInt(levels, row, c_vis);
            return target > 0 ? target : 0;
        }
    }
    return 0;
}

int World_GetWarpLinks(const int level_id, int *targets, const int max) {
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return 0;
    }
    const int c_id  = DataTable_GetColumnIndex(levels, "Id");
    int       count = 0;
    for (int row = 0; row < DataTable_GetRowCount(levels); row++) {
        if (!DataTable_HasValue(levels, row, c_id) || DataTable_GetInt(levels, row, c_id) != level_id) {
            continue;
        }
        for (int v = 0; v < 8 && count < max; v++) {
            char vis_column[8];
            char warp_column[8];
            snprintf(vis_column, sizeof(vis_column), "Vis%d", v);
            snprintf(warp_column, sizeof(warp_column), "Warp%d", v);
            const int target = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, vis_column));
            const int warp   = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, warp_column));
            if (target <= 0 || warp < 0 ||
                !DataTable_HasValue(levels, row, DataTable_GetColumnIndex(levels, warp_column))) {
                continue;
            }
            bool seen = false;
            for (int i = 0; i < count; i++) {
                seen = seen || targets[i] == target;
            }
            if (!seen) {
                targets[count++] = target;
            }
        }
        break;
    }
    return count;
}

// Entrances in a fixed map (stairs, doors to dungeons): special tiles with a low main index, in map order, lead to the
// areas Levels.txt lists for it (the ones not reached on foot)
static void World__AddFixedEntrances(Map *map, const int level_id) {
    int       warp_targets[8];
    const int all = World_GetWarpLinks(level_id, warp_targets, 8);
    int       walk_targets[4];
    WorldSide sides[4];
    const int walks = World_GetWalkLinks(level_id, walk_targets, sides, 4);
    int       count = 0;
    for (int i = 0; i < all; i++) {
        bool walk = false;
        for (int w = 0; w < walks; w++) {
            walk = walk || walk_targets[w] == warp_targets[i];
        }
        if (!walk) {
            warp_targets[count++] = warp_targets[i];
        }
    }

    int found_x[8];
    int found_y[8];
    int found_slot[8];
    int found = 0;
    for (int w = 0; w < map->wall_layer_count; w++) {
        for (int c = 0; c < map->width * map->height && found < 8; c++) {
            const DS1Cell *cell = &map->walls[w][c];
            if (cell->prop1 == 0 || (cell->orientation != 10 && cell->orientation != 11) || cell->style >= 8) {
                continue;
            }
            const int x    = c % map->width;
            const int y    = c / map->width;
            bool      near = false;
            for (int i = 0; i < found && !near; i++) {
                near = abs(found_x[i] - x) <= 1 && abs(found_y[i] - y) <= 1 && found_slot[i] == cell->style;
            }
            if (!near) {
                found_x[found]      = x;
                found_y[found]      = y;
                found_slot[found++] = cell->style;
            }
        }
    }
    int next = 0;
    for (int i = 0; i < found; i++) {
        // Where the tile says it leads; the next area Levels.txt lists if it doesn't say
        int target = World_GetVis(level_id, found_slot[i]);
        if (target <= 0) {
            target = next < count ? warp_targets[next++] : 0;
        }
        if (target <= 0) {
            continue;
        }
        const int x      = found_x[i] * MAP_SUBTILES;
        const int y      = found_y[i] * MAP_SUBTILES;
        const int before = map->exit_count;
        Map_AddExit(map, target, x - 1, y - 1, x + MAP_SUBTILES, y + MAP_SUBTILES, (float)x + 2.5f, (float)y + 2.5f);
        if (map->exit_count > before) {
            map->exits[before].entrance = true;
        }
    }
}

static void World__Note(const bool walkable, const WorldSide side, const int position, int *open, int *low, int *high) {
    if (!walkable) {
        return;
    }
    open[side]++;
    low[side]  = position < low[side] ? position : low[side];
    high[side] = position > high[side] ? position : high[side];
}

void World_AddFixedExits(Map *map, const int level_id) {
    // The game's start marker (a special tile, main index 30)
    for (int w = 0; w < map->wall_layer_count && !map->has_start; w++) {
        for (int c = 0; c < map->width * map->height; c++) {
            const DS1Cell *cell = &map->walls[w][c];
            if (cell->prop1 != 0 && cell->orientation == 10 && cell->style == 30 && cell->sequence == 0) {
                map->has_start = true;
                map->start_x   = (float)(((c % map->width) * MAP_SUBTILES) + (MAP_SUBTILES / 2)) + 0.5f;
                map->start_y   = (float)(((c / map->width) * MAP_SUBTILES) + (MAP_SUBTILES / 2)) + 0.5f;
                break;
            }
        }
    }

    World__AddFixedEntrances(map, level_id);

    int       targets[4];
    WorldSide sides[4];
    const int links = World_GetWalkLinks(level_id, targets, sides, 4);
    if (links == 0) {
        return;
    }

    // Where paths run off the edge of the map: walkable sub-tiles in the outer strip of each side
    const int depth   = 3;
    const int sub_w   = map->width * MAP_SUBTILES;
    const int sub_h   = map->height * MAP_SUBTILES;
    int       open[4] = {0, 0, 0, 0};
    int       low[4]  = {1 << 30, 1 << 30, 1 << 30, 1 << 30};
    int       high[4] = {-1, -1, -1, -1};
    for (int d = 0; d < depth; d++) {
        for (int x = 0; x < sub_w; x++) {
            World__Note(Map_IsWalkable(map, x, d), WORLD_SIDE_N, x, open, low, high);
            World__Note(Map_IsWalkable(map, x, sub_h - 1 - d), WORLD_SIDE_S, x, open, low, high);
        }
        for (int y = 0; y < sub_h; y++) {
            World__Note(Map_IsWalkable(map, d, y), WORLD_SIDE_W, y, open, low, high);
            World__Note(Map_IsWalkable(map, sub_w - 1 - d, y), WORLD_SIDE_E, y, open, low, high);
        }
    }

    // Each link on the side the layout gave it (the town's way out); the biggest opening left if that side is closed
    bool taken[4] = {false, false, false, false};
    for (int l = 0; l < links; l++) {
        int best = (sides[l] < WORLD_SIDE_NONE && !taken[sides[l]] && open[sides[l]] >= 10) ? (int)sides[l] : -1;
        for (int s = 0; s < 4 && best < 0; s++) {
            if (!taken[s] && open[s] >= 10) {
                best = s;
            }
        }
        for (int s = 0; s < 4; s++) {
            if (best != (int)sides[l] && !taken[s] && open[s] >= 10 && open[s] > open[best]) {
                best = s;
            }
        }
        if (best < 0) {
            break;
        }
        taken[best]       = true;
        const int   mid   = (low[best] + high[best]) / 2;
        const float inset = 14.0f; // arrive this far inside the edge
        switch ((WorldSide)best) {
        case WORLD_SIDE_N:
            Map_AddExit(map, targets[l], low[best], 0, high[best], depth - 1, (float)mid + 0.5f, inset);
            break;
        case WORLD_SIDE_S:
            Map_AddExit(map, targets[l], low[best], sub_h - depth, high[best], sub_h - 1, (float)mid + 0.5f,
                        (float)sub_h - inset);
            break;
        case WORLD_SIDE_W:
            Map_AddExit(map, targets[l], 0, low[best], depth - 1, high[best], inset, (float)mid + 0.5f);
            break;
        default:
            Map_AddExit(map, targets[l], sub_w - depth, low[best], sub_w - 1, high[best], (float)sub_w - inset,
                        (float)mid + 0.5f);
            break;
        }
    }
}

void World_Release(void) { DataTable_Destroy(&levels); }
