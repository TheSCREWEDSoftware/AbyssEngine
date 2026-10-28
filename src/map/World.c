#include "World.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
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
    // ... and on from the Tamoe Highland to the Monastery Gate and the Outer Cloister behind it
    {7,   26 },
    {26,  27 },
    // Act 2: Lut Gholein - Rocky Waste - Dry Hills - Far Oasis - Lost City - Valley of Snakes
    {40,  41 },
    {41,  42 },
    {42,  43 },
    {43,  44 },
    {44,  45 },
    // Act 3: Kurast Docktown - Spider Forest - Great Marsh - Flayer Jungle - Lower Kurast - Kurast Bazaar - Upper
    // Kurast
    // - Kurast Causeway - Travincal
    {75,  76 },
    {76,  77 },
    {77,  78 },
    {78,  79 },
    {79,  80 },
    {80,  81 },
    {81,  82 },
    {82,  83 },
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

// The act an area is in, from its Levels.txt Id
static int World__ActOf(const int level_id) {
    return level_id < 40 ? 0 : level_id < 75 ? 1 : level_id < 103 ? 2 : level_id < 109 ? 3 : 4;
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
    case 75:
        return WORLD_SIDE_N; // Kurast Docktown: the way into the jungle is past the north end of the docks
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

// Links whose side is fixed by the pieces (the Causeway's bridge runs north to south, so Upper Kurast is at one end
// and Travincal at the other; the Monastery's gate stands north of the Tamoe Highland with the Outer Cloister behind
// it)
static const struct {
    int       from;
    int       to;
    WorldSide side; // of "from"
} fixed_sides[] = {
    {7,  26, WORLD_SIDE_N},
    {26, 27, WORLD_SIDE_N},
    {81, 82, WORLD_SIDE_N},
    {82, 83, WORLD_SIDE_N},
    {78, 79, WORLD_SIDE_N}, // Lower Kurast's way in from the Flayer Jungle: the middle of its
  // south-west edge
};

// The game's rules for the other ways on (which edges a way out can be on, as bits of WorldSide; the screen's
// top-right edge is north, bottom-left south): the Black Marsh never leads to the Tamoe Highland from its bottom-left
// edge, the Outer Steppes never come into the Plains of Despair by the plains' south-east edge, and the Frigid
// Highlands lead on to the Arreat Plateau from their far north-west or north-east edge
static const struct {
    int      from;
    int      to;
    unsigned allowed; // of "from"'s sides
} allowed_sides[] = {
    {6,   7,   (1u << WORLD_SIDE_N) | (1u << WORLD_SIDE_E) | (1u << WORLD_SIDE_W)},
    {104, 105, (1u << WORLD_SIDE_N) | (1u << WORLD_SIDE_S) | (1u << WORLD_SIDE_E)},
    {111, 112, (1u << WORLD_SIDE_N) | (1u << WORLD_SIDE_W)                       },
};

static unsigned World__AllowedSides(const int from, const int to) {
    for (size_t i = 0; i < sizeof(allowed_sides) / sizeof(allowed_sides[0]); i++) {
        if (allowed_sides[i].from == from && allowed_sides[i].to == to) {
            return allowed_sides[i].allowed;
        }
    }
    return 0xFu;
}

static bool (*area_size)(int level_id, int *width, int *height) = NULL;

void World_SetAreaSizes(bool (*size_of)(int level_id, int *width, int *height)) {
    area_size   = size_of;
    layout.seed = 0; // lay out again with the real sizes
}

typedef struct WorldRect {
    int x; // tiles
    int y;
    int w;
    int h;
} WorldRect;

// Where an area goes next to another on one of its sides: touching (sharing the edge row), middles lined up
static WorldRect World__Beside(const WorldRect *a, const WorldSide side, const int w, const int h) {
    WorldRect b = {0, 0, w, h};
    switch (side) {
    case WORLD_SIDE_E:
        b.x = a->x + a->w - 1;
        b.y = a->y + (a->h / 2) - (h / 2);
        break;
    case WORLD_SIDE_W:
        b.x = a->x - w + 1;
        b.y = a->y + (a->h / 2) - (h / 2);
        break;
    case WORLD_SIDE_S:
        b.y = a->y + a->h - 1;
        b.x = a->x + (a->w / 2) - (w / 2);
        break;
    default:
        b.y = a->y - h + 1;
        b.x = a->x + (a->w / 2) - (w / 2);
        break;
    }
    return b;
}

// How much two areas would overlap (tiles); touching edges don't count, and a few tiles' slack is kept because the
// ways through aren't always in the middle of a side
static int World__Overlap(const WorldRect *a, const WorldRect *b) {
    const int slack = 24;
    const int w     = (a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w) - (a->x > b->x ? a->x : b->x) + slack;
    const int h     = (a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h) - (a->y > b->y ? a->y : b->y) + slack;
    return (w > 1 && h > 1) ? w * h : 0;
}

static void World__Layout(void) {
    if (layout.seed == World_GetSeed()) {
        return;
    }
    layout.seed = World_GetSeed();

    // Each act's outdoors laid out from its town: each link goes on a free side of the area it leaves from, the one
    // (from where the seed starts looking) where the next area doesn't run into one already placed
    static WorldRect rects[256];
    static bool      placed[256];
    memset(placed, 0, sizeof(placed));
    bool used_sides[256][4];
    memset(used_sides, 0, sizeof(used_sides));
    // The fixed sides are kept free for their own links
    bool reserved[256][4];
    memset(reserved, 0, sizeof(reserved));
    for (int f = 0; f < (int)(sizeof(fixed_sides) / sizeof(fixed_sides[0])); f++) {
        reserved[fixed_sides[f].from][fixed_sides[f].side]                = true;
        reserved[fixed_sides[f].to][World__Opposite(fixed_sides[f].side)] = true;
    }

    for (int l = 0; l < WALK_LINK_COUNT; l++) {
        const int a  = walk_links[l][0];
        const int b  = walk_links[l][1];
        int       aw = 80;
        int       ah = 80;
        int       bw = 80;
        int       bh = 80;
        if (area_size != NULL) {
            area_size(a, &aw, &ah);
            area_size(b, &bw, &bh);
        }
        if (!placed[a]) {
            placed[a] = true;
            rects[a]  = (WorldRect){0, 0, aw, ah};
        }

        WorldSide side = World__TownSide(a);
        for (int f = 0; f < (int)(sizeof(fixed_sides) / sizeof(fixed_sides[0])); f++) {
            side = fixed_sides[f].from == a && fixed_sides[f].to == b ? fixed_sides[f].side : side;
        }
        if (a == BLOODY_FOOTHILLS) {
            side = WORLD_SIDE_W; // the strips run west from Harrogath, which is on their east
        }
        if (side == WORLD_SIDE_NONE || used_sides[a][side]) {
            const int first = (int)(World__Mix(World_LevelSeed(a) + (uint32_t)b) % 4u);
            int       best  = -1;
            int       least = 0;
            for (int k = 0; k < 4; k++) {
                const WorldSide candidate = (WorldSide)((first + k) % 4);
                if (used_sides[a][candidate] || reserved[a][candidate] || reserved[b][World__Opposite(candidate)] ||
                    (World__AllowedSides(a, b) & (1u << candidate)) == 0) {
                    continue;
                }
                const WorldRect next    = World__Beside(&rects[a], candidate, bw, bh);
                int             overlap = 0;
                for (int other = 0; other < 256; other++) {
                    // Only the same act's areas share a map (Levels.txt Ids run act by act)
                    if (placed[other] && other != a && World__ActOf(other) == World__ActOf(a)) {
                        overlap += World__Overlap(&next, &rects[other]);
                    }
                }
                if (best < 0 || overlap < least) {
                    best  = (int)candidate;
                    least = overlap;
                }
            }
            side = best >= 0 ? (WorldSide)best : WORLD_SIDE_N; // more than four links: none in the game's
        }
        const WorldSide back = World__Opposite(side);
        layout.side_a[l]     = side;
        layout.side_b[l]     = back;
        used_sides[a][side]  = true;
        used_sides[b][back]  = true;
        placed[b]            = true;
        rects[b]             = World__Beside(&rects[a], side, bw, bh);
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

bool World_GetAtmosphere(const int level_id, WorldAtmosphere *atmosphere) {
    memset(atmosphere, 0, sizeof(WorldAtmosphere));
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return false;
    }
    const int c_id = DataTable_GetColumnIndex(levels, "Id");
    int       row  = -1;
    for (int r = 0; r < DataTable_GetRowCount(levels) && row < 0; r++) {
        row = DataTable_HasValue(levels, r, c_id) && DataTable_GetInt(levels, r, c_id) == level_id ? r : -1;
    }
    if (row < 0) {
        return false;
    }
    atmosphere->inside = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "IsInside")) != 0;
    atmosphere->rain   = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "Rain")) != 0;
    atmosphere->snow   = atmosphere->rain && World__ActOf(level_id) == 4; // Act V's falls as snow

    // The music: Levels.txt's SoundEnv is a row of SoundEnviron.txt, whose Song is a row of Sounds.txt
    static DataTable *environments = NULL;
    static DataTable *sounds       = NULL;
    if (environments == NULL) {
        environments = DataTable_Load("/data/global/excel/SoundEnviron.txt");
        sounds       = DataTable_Load("/data/global/excel/Sounds.txt");
    }
    const int environment = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "SoundEnv"));
    if (environments == NULL || sounds == NULL || environment <= 0) {
        return true;
    }
    int song           = 0;
    int ambience       = 0;
    int event          = 0;
    int night_ambience = 0;
    int night_event    = 0;
    for (int r = 0; r < DataTable_GetRowCount(environments); r++) {
        if (DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Index")) == environment) {
            song     = DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Song"));
            ambience = DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Day Ambience"));
            event    = DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Day Event"));
            night_ambience =
                DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Night Ambience"));
            night_event = DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Night Event"));
            atmosphere->event_delay =
                DataTable_GetInt(environments, r, DataTable_GetColumnIndex(environments, "Event Delay"));
            break;
        }
    }
    const int c_index = DataTable_GetColumnIndex(sounds, "Index");
    const int c_file  = DataTable_GetColumnIndex(sounds, "FileName");
    const int c_name  = DataTable_GetColumnIndex(sounds, "Sound");
    for (int r = 0;
         r < DataTable_GetRowCount(sounds) && (ambience > 0 || event > 0 || night_ambience > 0 || night_event > 0);
         r++) {
        const int index = DataTable_GetInt(sounds, r, c_index);
        if (ambience > 0 && index == ambience) {
            snprintf(atmosphere->ambience, sizeof(atmosphere->ambience), "%s", DataTable_GetString(sounds, r, c_name));
        }
        if (event > 0 && index == event) {
            snprintf(atmosphere->event, sizeof(atmosphere->event), "%s", DataTable_GetString(sounds, r, c_name));
        }
        if (night_ambience > 0 && index == night_ambience) {
            snprintf(atmosphere->night_ambience, sizeof(atmosphere->night_ambience), "%s",
                     DataTable_GetString(sounds, r, c_name));
        }
        if (night_event > 0 && index == night_event) {
            snprintf(atmosphere->night_event, sizeof(atmosphere->night_event), "%s",
                     DataTable_GetString(sounds, r, c_name));
        }
    }
    for (int r = 0; r < DataTable_GetRowCount(sounds) && song > 0; r++) {
        if (DataTable_GetInt(sounds, r, c_index) == song) {
            // "act1\town1.wav" -> "/data/global/music/act1/town1.wav"
            snprintf(atmosphere->music, sizeof(atmosphere->music), "/data/global/music/%s",
                     DataTable_GetString(sounds, r, c_file));
            for (char *c = atmosphere->music; *c != '\0'; c++) {
                *c = *c == '\\' ? '/' : *c;
            }
            break;
        }
    }
    return true;
}

int World_WaypointIndex(const int level_id) {
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return -1;
    }
    const int c_id = DataTable_GetColumnIndex(levels, "Id");
    const int c_wp = DataTable_GetColumnIndex(levels, "Waypoint");
    for (int row = 0; row < DataTable_GetRowCount(levels); row++) {
        if (DataTable_HasValue(levels, row, c_id) && DataTable_GetInt(levels, row, c_id) == level_id) {
            const int wp = DataTable_GetInt(levels, row, c_wp);
            return wp >= 0 && wp < 64 ? wp : -1;
        }
    }
    return -1;
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

// Areas joined by a red portal instead of a path or stairs: Tristram through the Cairn Stones, the Arcane Sanctuary
// (in from the Palace Cellar, out to the Canyon of the Magi), Nihlathak's three portals to the hells. Duriel's Lair is
// entered from whichever of the seven tombs is the true one (World_TrueTomb).
static const int portal_links[][3] = {
    {4,   38,  0},
    {54,  74,  0},
    {74,  46,  0},
    {111, 125, 0},
    {112, 126, 0},
    {117, 127, 0},
    // One way, on to the next act: Mephisto's to the Pandemonium Fortress, Diablo's to Harrogath (the game opens them
    // once the boss is dead; here they are always open)
    {102, 103, 1},
    {108, 109, 1},
};
#define PORTAL_LINK_COUNT (int)(sizeof(portal_links) / sizeof(portal_links[0]))
#define TAL_RASHA_FIRST   66
#define DURIELS_LAIR      73

int World_TrueTomb(void) { return TAL_RASHA_FIRST + World_PickVariant(DURIELS_LAIR, 7); }

int World_GetPortalLinks(const int level_id, int *targets, const int max) {
    int count = 0;
    for (int l = 0; l < PORTAL_LINK_COUNT && count < max; l++) {
        if (portal_links[l][0] == level_id) {
            targets[count++] = portal_links[l][1];
        } else if (portal_links[l][1] == level_id && portal_links[l][2] == 0) {
            targets[count++] = portal_links[l][0];
        }
    }
    if (count < max && level_id == World_TrueTomb()) {
        targets[count++] = DURIELS_LAIR;
    } else if (count < max && level_id == DURIELS_LAIR) {
        targets[count++] = World_TrueTomb();
    }
    return count;
}

int World_GetForwardPortals(const int level_id, int *targets, const int max) {
    int count = 0;
    for (int l = 0; l < PORTAL_LINK_COUNT && count < max; l++) {
        if (portal_links[l][0] == level_id) {
            targets[count++] = portal_links[l][1];
        }
    }
    if (count < max && level_id == World_TrueTomb()) {
        targets[count++] = DURIELS_LAIR;
    }
    return count;
}

// Is that area the way back from this one (where you came in from)?
static bool World__IsWayBack(const int level_id, const int target) {
    if (World_GetVis(level_id, 0) == target) {
        return true;
    }
    for (int l = 0; l < PORTAL_LINK_COUNT; l++) {
        if (portal_links[l][1] == level_id && portal_links[l][0] == target && portal_links[l][2] == 0) {
            return true;
        }
    }
    return level_id == DURIELS_LAIR;
}

void World_AddPortal(Map *map, const int target, float x, float y) {
    // On open ground: the nearest spot with room around it
    bool found = false;
    for (int r = 0; r <= 30 && !found; r++) {
        for (int dy = -r; dy <= r && !found; dy++) {
            for (int dx = -r; dx <= r && !found; dx++) {
                if (abs(dx) != r && abs(dy) != r) {
                    continue;
                }
                const int sx = (int)x + dx;
                const int sy = (int)y + dy;
                bool      ok = true;
                for (int k = 0; k < 9 && ok; k++) {
                    ok = Map_IsWalkable(map, sx + ((k % 3) - 1) * 2, sy + ((k / 3) - 1) * 2);
                }
                if (ok) {
                    x     = (float)sx + 0.5f;
                    y     = (float)sy + 0.5f;
                    found = true;
                }
            }
        }
    }
    Map_AddEntity(map, "objects", "PP", "Portal", x, y);
    const int sx     = (int)x;
    const int sy     = (int)y;
    const int before = map->exit_count;
    Map_AddExit(map, target, sx - 2, sy - 2, sx + 2, sy + 2, x + 4.0f, y + 4.0f);
    if (map->exit_count > before) {
        map->exits[before].entrance = true;
    }
}

bool World_NearestReachable(const Map *map, const float x, const float y, float *out_x, float *out_y) {
    const int sub_w = map->width * MAP_SUBTILES;
    const int sub_h = map->height * MAP_SUBTILES;
    const int sx    = map->has_start ? (int)map->start_x : sub_w / 2;
    const int sy    = map->has_start ? (int)map->start_y : sub_h / 2;
    if (!Map_IsWalkable(map, sx, sy)) {
        return false;
    }
    uint8_t *seen  = calloc((size_t)sub_w * (size_t)sub_h, 1);
    int     *queue = malloc(sizeof(int) * (size_t)sub_w * (size_t)sub_h);
    FAIL_IF_NULL(seen);
    FAIL_IF_NULL(queue);
    int head                = 0;
    int tail                = 0;
    queue[tail++]           = (sy * sub_w) + sx;
    seen[(sy * sub_w) + sx] = 1;
    int   best              = -1;
    float best_distance     = 0.0f;
    while (head < tail) {
        const int   c  = queue[head++];
        const int   cx = c % sub_w;
        const int   cy = c / sub_w;
        const float dx = (float)cx - x;
        const float dy = (float)cy - y;
        if (best < 0 || (dx * dx) + (dy * dy) < best_distance) {
            best          = c;
            best_distance = (dx * dx) + (dy * dy);
        }
        for (int d = 0; d < 8; d++) {
            static const int step_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            static const int step_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            const int        nx        = cx + step_x[d];
            const int        ny        = cy + step_y[d];
            if (nx < 0 || ny < 0 || nx >= sub_w || ny >= sub_h || seen[(ny * sub_w) + nx] ||
                !Map_IsWalkable(map, nx, ny)) {
                continue;
            }
            seen[(ny * sub_w) + nx] = 1;
            queue[tail++]           = (ny * sub_w) + nx;
        }
    }
    *out_x = (float)(best % sub_w) + 0.5f;
    *out_y = (float)(best / sub_w) + 0.5f;
    free(seen);
    free(queue);
    return true;
}

void World_EnsureExits(Map *map, const int level_id) {
    // Every area this one leads to by stairs or portal (walking off the edge is the layout's job)
    int targets[16];
    int count = 0;
    for (int slot = 0; slot < 8; slot++) {
        targets[count]  = World_GetVis(level_id, slot);
        count          += targets[count] > 0 ? 1 : 0;
    }
    count += World_GetPortalLinks(level_id, targets + count, 16 - count);
    int       walk_targets[4];
    WorldSide sides[4];
    const int walks   = World_GetWalkLinks(level_id, walk_targets, sides, 4);
    int       missing = 0;
    for (int i = 0; i < count; i++) {
        bool skip = Map_ExitTo(map, targets[i]) != NULL;
        for (int w = 0; w < walks && !skip; w++) {
            skip = walk_targets[w] == targets[i];
        }
        for (int k = 0; k < missing && !skip; k++) {
            skip = targets[k] == targets[i];
        }
        if (!skip) {
            targets[missing++] = targets[i];
        }
    }
    if (missing == 0) {
        return;
    }

    // How far each walkable sub-tile is from the start (walking): ways back go near it, ways on as far as can be
    const int sub_w    = map->width * MAP_SUBTILES;
    const int sub_h    = map->height * MAP_SUBTILES;
    int      *distance = malloc(sizeof(int) * (size_t)sub_w * (size_t)sub_h);
    int      *queue    = malloc(sizeof(int) * (size_t)sub_w * (size_t)sub_h);
    FAIL_IF_NULL(distance);
    FAIL_IF_NULL(queue);
    for (int i = 0; i < sub_w * sub_h; i++) {
        distance[i] = -1;
    }
    float start_x = map->has_start ? map->start_x : (float)sub_w / 2.0f;
    float start_y = map->has_start ? map->start_y : (float)sub_h / 2.0f;
    if (!Map_IsWalkable(map, (int)start_x, (int)start_y)) {
        // The nearest open spot (a fixed map's middle can be a wall)
        float best = -1.0f;
        for (int y = 2; y < sub_h; y += MAP_SUBTILES) {
            for (int x = 2; x < sub_w; x += MAP_SUBTILES) {
                const float d =
                    ((float)x - start_x) * ((float)x - start_x) + ((float)y - start_y) * ((float)y - start_y);
                if (Map_IsWalkable(map, x, y) && (best < 0.0f || d < best)) {
                    best    = d;
                    start_x = (float)x;
                    start_y = (float)y;
                }
            }
        }
        if (best < 0.0f) {
            free(distance);
            free(queue);
            return;
        }
    }
    int       head  = 0;
    int       tail  = 0;
    const int first = ((int)start_y * sub_w) + (int)start_x;
    queue[tail++]   = first;
    distance[first] = 0;
    while (head < tail) {
        const int        c         = queue[head++];
        const int        x         = c % sub_w;
        const int        y         = c / sub_w;
        static const int step_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int step_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int d = 0; d < 8; d++) {
            const int nx = x + step_x[d];
            const int ny = y + step_y[d];
            if (nx < 0 || ny < 0 || nx >= sub_w || ny >= sub_h || distance[(ny * sub_w) + nx] >= 0 ||
                !Map_IsWalkable(map, nx, ny)) {
                continue;
            }
            distance[(ny * sub_w) + nx] = distance[c] + 1;
            queue[tail++]               = (ny * sub_w) + nx;
        }
    }

    for (int m = 0; m < missing; m++) {
        const bool back = World__IsWayBack(level_id, targets[m]);
        int        best = -1;
        for (int i = 0; i < tail; i++) {
            const int c  = queue[i];
            const int cx = c % sub_w;
            const int cy = c / sub_w;
            // Clear of the other exits, with room around it to stand
            bool clear = true;
            for (int e = 0; e < map->exit_count && clear; e++) {
                const MapExit *exit = &map->exits[e];
                clear = cx < exit->x0 - 20 || cx > exit->x1 + 20 || cy < exit->y0 - 20 || cy > exit->y1 + 20;
            }
            for (int d = 0; d < 9 && clear; d++) {
                const int nx = cx + (((d % 3) - 1) * 2);
                const int ny = cy + (((d / 3) - 1) * 2);
                clear        = nx >= 0 && ny >= 0 && nx < sub_w && ny < sub_h && distance[(ny * sub_w) + nx] >= 0;
            }
            if (!clear) {
                continue;
            }
            if (back ? (distance[c] >= 15 && (best < 0 || distance[c] < distance[best]))
                     : (best < 0 || distance[c] > distance[best])) {
                best = c;
            }
        }
        if (best < 0) {
            best = first;
        }
        World_AddPortal(map, targets[m], (float)(best % sub_w) + 0.5f, (float)(best / sub_w) + 0.5f);
        LOG_INFO("%s: portal to area %d", map->name, targets[m]);
    }
    free(distance);
    free(queue);
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

// The sub-tiles you can walk to from the map's start (NULL if it has none, or it's blocked): free it
static uint8_t *World__Reachable(const Map *map) {
    const int sub_w = map->width * MAP_SUBTILES;
    const int sub_h = map->height * MAP_SUBTILES;
    if (!map->has_start || !Map_IsWalkable(map, (int)map->start_x, (int)map->start_y)) {
        return NULL;
    }
    uint8_t *seen  = calloc((size_t)sub_w * (size_t)sub_h, 1);
    int     *queue = malloc(sizeof(int) * (size_t)sub_w * (size_t)sub_h);
    FAIL_IF_NULL(seen);
    FAIL_IF_NULL(queue);
    int head       = 0;
    int tail       = 0;
    queue[tail++]  = ((int)map->start_y * sub_w) + (int)map->start_x;
    seen[queue[0]] = 1;
    while (head < tail) {
        const int        c         = queue[head++];
        static const int step_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int step_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int d = 0; d < 8; d++) {
            const int nx = (c % sub_w) + step_x[d];
            const int ny = (c / sub_w) + step_y[d];
            if (nx >= 0 && ny >= 0 && nx < sub_w && ny < sub_h && !seen[(ny * sub_w) + nx] &&
                Map_IsWalkable(map, nx, ny)) {
                seen[(ny * sub_w) + nx] = 1;
                queue[tail++]           = (ny * sub_w) + nx;
            }
        }
    }
    free(queue);
    return seen;
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
    // The way through is the widest stretch of open edge you can walk to from the start (not everything from the
    // first open spot to the last: a town's edge can have a bit of open ground well away from the road, which would
    // pull the middle off it)
    uint8_t *reach = World__Reachable(map);
    for (int s = 0; s < 4; s++) {
        const int length = (s == WORLD_SIDE_N || s == WORLD_SIDE_S) ? sub_w : sub_h;
        int       run    = 0;
        int       best   = 0;
        for (int p = 0; p <= length; p++) {
            bool walkable = false;
            for (int d = 0; d < depth && p < length && !walkable; d++) {
                const int x = s == WORLD_SIDE_N || s == WORLD_SIDE_S ? p : (s == WORLD_SIDE_W ? d : sub_w - 1 - d);
                const int y = s == WORLD_SIDE_W || s == WORLD_SIDE_E ? p : (s == WORLD_SIDE_N ? d : sub_h - 1 - d);
                walkable    = Map_IsWalkable(map, x, y) && (reach == NULL || reach[(y * sub_w) + x] != 0);
            }
            if (walkable) {
                run++;
            } else {
                if (run > best) {
                    best    = run;
                    low[s]  = p - run;
                    high[s] = p - 1;
                }
                run = 0;
            }
        }
    }
    free(reach);

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

void World_GetWarpText(const int level_id, char *text, const int size) {
    text[0] = '\0';
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return;
    }
    const int c_id   = DataTable_GetColumnIndex(levels, "Id");
    const int c_warp = DataTable_GetColumnIndex(levels, "LevelWarp");
    const int c_name = DataTable_GetColumnIndex(levels, "LevelName");
    for (int row = 0; row < DataTable_GetRowCount(levels); row++) {
        if (DataTable_HasValue(levels, row, c_id) && DataTable_GetInt(levels, row, c_id) == level_id) {
            const char *warp = DataTable_GetString(levels, row, c_warp);
            if (warp[0] != '\0') {
                snprintf(text, (size_t)size, "%s", StringTable_Get(warp));
            } else {
                snprintf(text, (size_t)size, "To %s", StringTable_Get(DataTable_GetString(levels, row, c_name)));
            }
            return;
        }
    }
}

void World_GetEnteringText(const int level_id, char *text, const int size) {
    text[0] = '\0';
    if (levels == NULL) {
        levels = DataTable_Load(EXCEL_LEVELS);
    }
    if (levels == NULL) {
        return;
    }
    const int c_id   = DataTable_GetColumnIndex(levels, "Id");
    const int c_warp = DataTable_GetColumnIndex(levels, "LevelWarp");
    const int c_name = DataTable_GetColumnIndex(levels, "LevelName");
    for (int row = 0; row < DataTable_GetRowCount(levels); row++) {
        if (!DataTable_HasValue(levels, row, c_id) || DataTable_GetInt(levels, row, c_id) != level_id) {
            continue;
        }
        const char *warp = DataTable_GetString(levels, row, c_warp);
        const char *name = DataTable_GetString(levels, row, c_name);
        warp             = (warp != NULL && warp[0] != '\0') ? StringTable_Get(warp) : NULL;
        if (warp != NULL && strncmp(warp, "To ", 3) == 0) {
            snprintf(text, (size_t)size, "Entering %s", warp + 3);
        } else if (name != NULL && name[0] != '\0') {
            snprintf(text, (size_t)size, "Entering %s", StringTable_Get(name));
        }
        return;
    }
}

void World_Release(void) { DataTable_Destroy(&levels); }
