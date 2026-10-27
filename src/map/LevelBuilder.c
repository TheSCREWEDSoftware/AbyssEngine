#include "LevelBuilder.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "../types/DS1.h"
#include "World.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_LEVELS   "/data/global/excel/Levels.txt"
#define EXCEL_LVLPREST "/data/global/excel/LvlPrest.txt"
#define EXCEL_LVLMAZE  "/data/global/excel/LvlMaze.txt"
#define EXCEL_LVLSUB   "/data/global/excel/LvlSub.txt"
#define EXCEL_MONSTATS "/data/global/excel/monstats.txt"

#define PRESET_MAX_FILES  6
#define DS1_CACHE_SIZE    512
#define MAX_GRID          64 // blocks per side
#define MAX_ROOMS         48
#define MAX_CLUSTERS      96
#define CLUSTER_MAX_CELLS 48

// Exits of a dungeon room, in the order the game writes them in preset names ("Act 1 - Cave NSEW")
#define EXIT_N 1
#define EXIT_S 2
#define EXIT_E 4
#define EXIT_W 8

// Which corners of a border piece are blocked: (low x, low y) = top-left in the preset's own grid, ...
#define CORNER_TL 1
#define CORNER_TR 2
#define CORNER_BL 4
#define CORNER_BR 8
#define EDGE_N    (CORNER_TL | CORNER_TR)
#define EDGE_S    (CORNER_BL | CORNER_BR)
#define EDGE_W    (CORNER_TL | CORNER_BL)
#define EDGE_E    (CORNER_TR | CORNER_BR)
#define OUTER_NW  (CORNER_TL | CORNER_TR | CORNER_BL)
#define OUTER_NE  (CORNER_TL | CORNER_TR | CORNER_BR)
#define OUTER_SW  (CORNER_TL | CORNER_BL | CORNER_BR)
#define OUTER_SE  (CORNER_TR | CORNER_BL | CORNER_BR)

// ---------------------------------------------------------------------------------------------------------------------
// What the game hard-codes about each area: which pieces it is built from

typedef struct OutdoorArea {
    int         level_id;
    int         cell;           // block size in tiles
    const char *borders;        // border pieces: presets whose name starts with this
    const char *border_require; // ... and contains this (NULL = anything)
    const char *border_exclude; // ... and doesn't contain this
    int         size_x;         // tiles; 0 = Levels.txt
    int         size_y;
    const char *landmarks[10]; // placed once each: cave entrances, the Cairn Stones, ...
    const char *fills[24];     // random filler pieces
} OutdoorArea;

#define A1_FENCES                                                                                                      \
    "Act 1 - Fence Fill 1", "Act 1 - Fence Fill 2", "Act 1 - Fence Fill 3", "Act 1 - Fence Fill 4",                    \
        "Act 1 - Fence Fill 5", "Act 1 - Fence Fill 6"
#define A2_DESERT                                                                                                      \
    "Act 2 - Desert Fill Mesa 1", "Act 2 - Desert Fill Head 1", "Act 2 - Desert Fill Head 2",                          \
        "Act 2 - Desert Fill Bone 1", "Act 2 - Desert Fill Bone 2", "Act 2 - Desert Fill Wagon 1",                     \
        "Act 2 - Desert Fill Berms 1", "Act 2 - Desert Fill Berms 2", "Act 2 - Desert Fill Berms 3"
#define A4_MESA                                                                                                        \
    "Act 4 - Mesa 1 24X24", "Act 4 - Mesa 1 16X16", "Act 4 - Mesa 1 16X08", "Act 4 - Mesa 1 08X16",                    \
        "Act 4 - Mesa 1 08X08", "Act 4 - Mesa 2 24X24", "Act 4 - Mesa 2 16X16", "Act 4 - Mesa 2 16X08",                \
        "Act 4 - Mesa 2 08X16", "Act 4 - Mesa 2 08X08"
#define A4_PITS                                                                                                        \
    "Act 4 - Pits 1 16X16", "Act 4 - Pits 1 16X08", "Act 4 - Pits 1 08X16", "Act 4 - Pits 1 08X08",                    \
        "Act 4 - Pits 2 16X16", "Act 4 - Pits 2 16X08", "Act 4 - Pits 2 08X16", "Act 4 - Pits 2 08X08"
#define A5_BARRICADES                                                                                                  \
    "Act 5 - Barricade 1", "Act 5 - Barricade 2", "Act 5 - Barricade 3", "Act 5 - Barricade 4", "Act 5 - Barricade 5", \
        "Act 5 - Barricade 6", "Act 5 - Barricade 7", "Act 5 - Barricade 8", "Act 5 - Barricade 9",                    \
        "Act 5 - Barricade 10", "Act 5 - Barricade 11", "Act 5 - Barricade 12", "Act 5 - Barricade 13",                \
        "Act 5 - Barricade 14", "Act 5 - Barricade 15", "Act 5 - Barricade 16"

static const OutdoorArea outdoor_areas[] = {
    // Act 1
    {2,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - DOE Entrance"},
     {"Act 1 - Fallen Camp 1", "Act 1 - Fallen Camp 2", "Act 1 - Stone Fill 1", "Act 1 - Stone Fill 2",
      "Act 1 - Corral Fill", "Act 1 - Pond", "Act 1 - Cottages 1", "Act 1 - Cottages 2", "Act 1 - Fence Fill 4",
      "Act 1 - Fence Fill 5", "Act 1 - Fence Fill 6"}                                                                                   },
    {3,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - Cave Entrance", "Act 1 - Fallen Camp Bishibosh"},
     {A1_FENCES, "Act 1 - Fallen Camp 1", "Act 1 - Fallen Camp 2", "Act 1 - Cottages 1", "Act 1 - Cottages 3",
      "Act 1 - Bivouac", "Act 1 - Camp", "Act 1 - Stone Fill 1"}                                                                        },
    {4,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - Cairn Stones", "Act 1 - Cave Entrance"},
     {"Act 1 - Stone Fill 1", "Act 1 - Stone Fill 2", A1_FENCES, "Act 1 - Cottages 1", "Act 1 - Cottages 2",
      "Act 1 - Pond", "Act 1 - Fallen Camp 1"}                                                                                          },
    {5,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - Inifus", "Act 1 - Cave Entrance"},
     {"Act 1 - Tree Fill", "Act 1 - Tree Fill", "Act 1 - Tree Fill", "Act 1 - Ruin", "Act 1 - Fallen Camp 2",
      "Act 1 - Pond", "Act 1 - Stone Fill 2"}                                                                                           },
    {6,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - Tower 1", "Act 1 - Cave Entrance"},
     {"Act 1 - Swamp Fill 1", "Act 1 - Swamp Fill 2", "Act 1 - Swamp Fill 1", "Act 1 - Tree Fill", "Act 1 - Ruin",
      "Act 1 - Fence Fill 2", "Act 1 - Pond"}                                                                                           },
    {7,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 1 - Cave Entrance"},
     {A1_FENCES, "Act 1 - Stone Fill 1", "Act 1 - Stone Fill 2", "Act 1 - Cottages 2", "Act 1 - Cottages 3",
      "Act 1 - Camp", "Act 1 - Corral Fill"}                                                                                            },
    {17,  8, "Act 1 - Wild Border ",            NULL, NULL,   0, 0,  {"Act 1 - Graveyard"},                     {"Act 1 - Stone Fill 1"}},
    {39,
     8,      "Act 1 - Wild Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {NULL},
     {"Act 1 - Corral Fill", "Act 1 - Corral Fill", A1_FENCES, "Act 1 - Cottages 1", "Act 1 - Cottages 2",
      "Act 1 - Pond"}                                                                                                                   },
    // Act 2
    {41,  8, "Act 2 - Desert Border ",          NULL, NULL,   0, 0,  {"Act 2 - Desert Tomb 1"},                 {A2_DESERT}             },
    {42,  8, "Act 2 - Desert Border ",          NULL, NULL,   0, 0,  {"Act 2 - Desert Tomb 1"},                 {A2_DESERT}             },
    {43,
     8,      "Act 2 - Desert Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 2 - Desert Lair 1"},
     {A2_DESERT, "Act 2 - Desert Oasis 1", "Act 2 - Desert Oasis 2", "Act 2 - Desert Oasis 3",
      "Act 2 - Desert Oasis 1"}                                                                                                         },
    {44,
     8,      "Act 2 - Desert Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 2 - Desert Ruins Sewer"},
     {"Act 2 - Desert Ruins 16X16", "Act 2 - Desert Ruins 16X08", "Act 2 - Desert Ruins 08X16",
      "Act 2 - Desert Ruins 08X08", "Act 2 - Desert Valley Ruin 1", "Act 2 - Desert Valley Ruin 2",
      "Act 2 - Desert Fill Berms 1", "Act 2 - Desert Fill Bone 1"}                                                                      },
    {45,  8, "Act 2 - Desert Border ",          NULL, NULL,   0, 0,  {"Act 2 - Desert Tomb 2"},                 {A2_DESERT}             },
    {46,
     8,      "Act 2 - Desert Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 2 - Desert Valley Warp", "Act 2 - Desert Tomb 1", "Act 2 - Desert Tomb 1", "Act 2 - Desert Tomb 1",
      "Act 2 - Desert Tomb 1", "Act 2 - Desert Tomb 1", "Act 2 - Desert Tomb 1", "Act 2 - Desert Tomb 1"},
     {"Act 2 - Desert Valley Ruin 1", "Act 2 - Desert Valley Ruin 2", "Act 2 - Desert Tomb 1",
      "Act 2 - Desert Fill Mesa 1", "Act 2 - Desert Fill Head 1", "Act 2 - Desert Fill Bone 2"}                                         },
    // Act 3: Kurast, its three districts
    {79,
     8,      "Act 3 - Slums Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {NULL},
     {"Act 3 - Slums 08x08", "Act 3 - Slums 08x16", "Act 3 - Slums 16x08", "Act 3 - Slums 16x16"}                                       },
    {80,
     8,      "Act 3 - Burbs Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 3 - Burbs Waypoint", "Act 3 - Burbs Sewer", "Act 3 - Burbs Temple", "Act 3 - Burbs Temple"},
     {"Act 3 - Burbs 08x08", "Act 3 - Burbs 08x16", "Act 3 - Burbs 16x08", "Act 3 - Burbs 16x16"}                                       },
    {81,
     8,      "Act 3 - Metro Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 3 - Burbs Waypoint", "Act 3 - Metro Sewer", "Act 3 - MetroTemple", "Act 3 - MetroTemple"},
     {"Act 3 - Metro 08x08", "Act 3 - Metro 08x16", "Act 3 - Metro 16x08", "Act 3 - Metro 16x16"}                                       },
    // Act 4
    {104, 8, "Act 4 - Mesa Border ",            NULL, NULL,   0, 0,  {NULL},                                    {A4_MESA}               },
    {105, 8, "Act 4 - Mesa Border ",            NULL, NULL,   0, 0,  {"Act 4 - Mesa 2 Izual"},                  {A4_MESA, A4_PITS}      },
    {106,
     8,      "Act 4 - Mesa Border ",
     NULL,                                            NULL,
     0,                                                          0,
     {"Act 4 - Mesa Warp"},
     {A4_PITS, "Act 4 - Mesa 3 16X16", "Act 4 - Mesa 3 16X08", "Act 4 - Mesa 3 08X16", "Act 4 - Mesa 3 08X08"}                          },
    // Act 5
    {111,
     16,     "Act 5 - Barricade Cliff Border ",
     NULL,                                            "Snow",
     128,                                                        96,
     {"Act 5 - Barricade Waypoint Dirt", "Act 5 - Barricade Hell Portal N"},
     {A5_BARRICADES}                                                                                                                    },
    {112,
     16,     "Act 5 - Barricade Cliff Border ",
     NULL,                                            "Snow",
     128,                                                        96,
     {"Act 5 - Barricade Waypoint Dirt", "Act 5 - Barricade To Cave 32x16", "Act 5 - Barricade Hell Portal N"},
     {A5_BARRICADES}                                                                                                                    },
    {117,
     16,     "Act 5 - Barricade Cliff Border ",
     "Snow",                                          NULL,
     128,                                                        80,
     {"Act 5 - Barricade Waypoint Snow", "Act 5 - Barricade From Cave 32x16 Snow",
      "Act 5 - Barricade To Cave 32x16 Snow", "Act 5 - Barricade Hell Portal N"},
     {"Act 5 - Barricade 1 Snow", "Act 5 - Barricade 2 Snow", "Act 5 - Barricade 3 Snow", "Act 5 - Barricade 4 Snow",
      "Act 5 - Barricade 5 Snow", "Act 5 - Barricade 6 Snow", "Act 5 - Barricade 7 Snow", "Act 5 - Barricade 8 Snow",
      "Act 5 - Barricade 9 Snow", "Act 5 - Barricade 10 Snow", "Act 5 - Barricade 11 Snow",
      "Act 5 - Barricade 12 Snow"}                                                                                                      },
};

typedef struct MazeArea {
    int         level_id;
    const char *family;  // rooms: "<family> <exits>"
    const char *entry;   // first room ("<entry> <exit>"), NULL = an ordinary room
    const char *exit;    // the room furthest from the entry
    const char *special; // another dead end, NULL = none
    int         rooms;   // at least this many (LvlMaze.txt's count when larger)
    int         grid_w;  // fixed grid (the jungle), 0 = free
    int         grid_h;
} MazeArea;

static const MazeArea maze_areas[] = {
    // Act 1
    {8,   "Act 1 - Cave",      "Act 1 - Cave Prev",      "Act 1 - Cave Den Of Evil",      NULL,                         7,  0, 0},
    {9,   "Act 1 - Cave",      "Act 1 - Cave Prev",      "Act 1 - Cave Next",             NULL,                         5,  0, 0},
    {10,  "Act 1 - Cave",      "Act 1 - Cave Prev",      "Act 1 - Cave Next",             NULL,                         6,  0, 0},
    {11,  "Act 1 - Cave",      "Act 1 - Cave Prev",      "Act 1 - Cave Next",             NULL,                         5,  0, 0},
    {12,  "Act 1 - Cave",      "Act 1 - Cave Prev",      "Act 1 - Cave Next",             NULL,                         5,  0, 0},
    {18,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Bonebreak",       "Act 1 - Crypt Chest",        12, 0, 0},
    {19,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Chest",           NULL,                         12, 0, 0},
    {21,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Next",            NULL,                         8,  0, 0},
    {22,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Next",            NULL,                         8,  0, 0},
    {23,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Next",            NULL,                         8,  0, 0},
    {24,  "Act 1 - Crypt",     "Act 1 - Crypt Prev",     "Act 1 - Crypt Next",            NULL,                         8,  0, 0},
    {28,  "Act 1 - Barracks",  NULL,                     "Act 1 - Barracks Next",         "Act 1 - Barracks Forge",     10, 0, 0},
    {29,  "Act 1 - Jail",      "Act 1 - Jail Prev",      "Act 1 - Jail Next",             "Act 1 - Jail Waypoint",      12, 0, 0},
    {30,  "Act 1 - Jail",      "Act 1 - Jail Prev",      "Act 1 - Jail Next",             NULL,                         14, 0, 0},
    {31,  "Act 1 - Jail",      "Act 1 - Jail Prev",      "Act 1 - Jail Next",             NULL,                         16, 0, 0},
    {34,  "Act 1 - Catacombs", "Act 1 - Catacombs Prev", "Act 1 - Catacombs Next",        NULL,                         12, 0, 0},
    {35,  "Act 1 - Catacombs", "Act 1 - Catacombs Prev", "Act 1 - Catacombs Next",        "Act 1 - Catacombs Waypoint", 14, 0,
     0                                                                                                                          },
    {36,  "Act 1 - Catacombs", "Act 1 - Catacombs Prev", "Act 1 - Catacombs Next",        NULL,                         16, 0, 0},
    // Act 2
    {47,  "Act 2 - Sewer",     "Act 2 - Sewer Prev",     "Act 2 - Sewer Next",            NULL,                         6,  0, 0},
    {48,  "Act 2 - Sewer",     "Act 2 - Sewer Prev",     "Act 2 - Sewer Next",            "Act 2 - Sewer Waypoint",     12, 0, 0},
    {49,  "Act 2 - Sewer",     "Act 2 - Sewer Prev",     "Act 2 - Sewer Radament's Lair", "Act 2 - Sewer Chest",        14, 0, 0},
    {55,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Next",             NULL,                         5,  0, 0},
    {56,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Next",             NULL,                         6,  0, 0},
    {57,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Next",             "Act 2 - Tomb Waypoint",      6,  0, 0},
    {58,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Next",             NULL,                         12, 0, 0},
    {59,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Treasure",         NULL,                         5,  0, 0},
    {60,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Cube",             NULL,                         6,  0, 0},
    {61,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Treasure",         NULL,                         6,  0, 0},
    {62,  "Act 2 - Lair",      "Act 2 - Lair Prev",      "Act 2 - Lair Next",             NULL,                         8,  0, 0},
    {63,  "Act 2 - Lair",      "Act 2 - Lair Prev",      "Act 2 - Lair Next",             NULL,                         8,  0, 0},
    {64,  "Act 2 - Lair",      "Act 2 - Lair Prev",      "Act 2 - Lair Treasure",         NULL,                         8,  0, 0},
    {65,  "Act 2 - Sewer",     "Act 2 - Sewer Prev",     "Act 2 - Sewer Chest",           NULL,                         10, 0, 0},
    {66,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Talrasha",         "Act 2 - Tomb Chest",         8,  0, 0},
    {67,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {68,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {69,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {70,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {71,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {72,  "Act 2 - Tomb",      "Act 2 - Tomb Prev",      "Act 2 - Tomb Chest",            NULL,                         8,  0, 0},
    {74,  "Act 2 - Arcane",    NULL,                     "Act 2 - Arcane Summoner",       NULL,                         24, 0, 0},
    // Act 3
    {76,  "Act 3 - Jungle",    NULL,                     "Act 3 - Clearing Webby",        "Act 3 - Clearing Pygmy",     12, 2, 6},
    {77,  "Act 3 - Jungle",    NULL,                     "Act 3 - Clearing Boggy",        "Act 3 - Clearing Boggy",     12, 2, 6},
    {78,  "Act 3 - Jungle",    NULL,                     "Act 3 - Clearing Pygmy",        "Act 3 - Clearing Webby",     12, 2, 6},
    {86,  "Act 3 - Dungeon",   "Act 3 - Dungeon Prev",   "Act 3 - Dungeon Next",          NULL,                         8,  0, 0},
    {87,  "Act 3 - Dungeon",   "Act 3 - Dungeon Prev",   "Act 3 - Dungeon Next",          NULL,                         8,  0, 0},
    {88,  "Act 3 - Dungeon",   "Act 3 - Dungeon Prev",   "Act 3 - Dungeon Next",          NULL,                         8,  0, 0},
    {89,  "Act 3 - Dungeon",   "Act 3 - Dungeon Prev",   "Act 3 - Dungeon Next",          NULL,                         8,  0, 0},
    {92,  "Act 3 - Sewer",     "Act 3 - Sewer Prev",     "Act 3 - Sewer Drain",           "Act 3 - Sewer Chest",        10, 0, 0},
    {100, "Act 3 - Mephisto",  "Act 3 - Mephisto Prev",  "Act 3 - Mephisto Next",         NULL,                         12, 0, 0},
    {101, "Act 3 - Mephisto",  "Act 3 - Mephisto Prev",  "Act 3 - Mephisto Next",         "Act 3 - Mephisto Waypoint",  12, 0, 0},
    // Act 4
    {107, "Act 4 - Lava",      "Act 4 - Lava Warp",      "Act 4 - Lava Forge",            NULL,                         8,  0, 0},
    // Act 5
    {113, "Act 5 - Ice",       "Act 5 - Ice Prev",       "Act 5 - Ice Next",              "Act 5 - Ice waypoint",       8,  0, 0},
    {114, "Act 5 - Ice",       "Act 5 - Ice Prev",       NULL,                            NULL,                         6,  0, 0},
    {115, "Act 5 - Ice",       "Act 5 - Ice Prev",       "Act 5 - Ice Next",              NULL,                         8,  0, 0},
    {116, "Act 5 - Ice",       "Act 5 - Ice Prev",       NULL,                            NULL,                         6,  0, 0},
    {118, "Act 5 - Ice",       "Act 5 - Ice Prev",       "Act 5 - Ice Down",              NULL,                         8,  0, 0},
    {119, "Act 5 - Ice",       "Act 5 - Ice Prev",       NULL,                            NULL,                         8,  0, 0},
    {128, "Act 5 - Baal",      "Act 5 - Baal Prev",      "Act 5 - Baal Next",             NULL,                         10, 0, 0},
    {129, "Act 5 - Baal",      "Act 5 - Baal Prev",      "Act 5 - Baal Next",             "Act 5 - Baal Waypoint",      10, 0, 0},
    {130, "Act 5 - Baal",      "Act 5 - Baal Prev",      "Act 5 - Baal Next",             NULL,                         10, 0, 0},
};

#define STRIP_LEVEL_ID 110 // Bloody Foothills

// ---------------------------------------------------------------------------------------------------------------------
// Tables

typedef struct Preset {
    char name[64];
    int  size_x;
    int  size_y;
    int  file_count;
    char files[PRESET_MAX_FILES][96]; // "Act1/Outdoors/Bord1.ds1"
} Preset;

typedef struct LevelInfo {
    int  id;
    char name[48];
    int  act;
    int  size_x;
    int  size_y;
    int  sub_type; // LvlSub.txt decorations
    int  sub_theme;
    int  sub_waypoint;
    int  sub_shrine;
    bool waypoint;
    int  density;
    int  monster_count;
    char monsters[10][32];
    int  group_min[10]; // monstats MinGrp / MaxGrp: how many in one of its packs
    int  group_max[10];
    int  maze_rooms;
} LevelInfo;

typedef struct SubFile {
    int  type;
    char file[96];
    int  prob[5];
    int  trials[5];
    int  max[5];
} SubFile;

static Preset    *presets        = NULL;
static int        preset_count   = -1;
static SubFile   *sub_files      = NULL;
static int        sub_count      = 0;
static DataTable *levels_table   = NULL;
static DataTable *monstats_table = NULL; // (for the pack sizes)
static DataTable *maze_table     = NULL;

typedef struct CachedDS1 {
    char path[128];
    DS1 *ds1;
} CachedDS1;

static CachedDS1 ds1_cache[DS1_CACHE_SIZE];
static int       ds1_cache_count = 0;

static bool Builder__SameName(const char *a, const char *b) {
    // Case-insensitive; some names in the tables end in a stray space
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    while (*a == ' ') {
        a++;
    }
    while (*b == ' ') {
        b++;
    }
    return *a == *b;
}

static bool Builder__StartsWith(const char *text, const char *prefix) {
    for (; *prefix != '\0'; text++, prefix++) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
            return false;
        }
    }
    return true;
}

static void Builder__LoadTables(void) {
    if (preset_count >= 0) {
        return;
    }
    preset_count = 0;

    DataTable *table = DataTable_Load(EXCEL_LVLPREST);
    if (table != NULL) {
        const int rows = DataTable_GetRowCount(table);
        presets        = calloc(rows > 0 ? (size_t)rows : 1, sizeof(Preset));
        FAIL_IF_NULL(presets);
        const int c_name = DataTable_GetColumnIndex(table, "Name");
        const int c_sx   = DataTable_GetColumnIndex(table, "SizeX");
        const int c_sy   = DataTable_GetColumnIndex(table, "SizeY");
        int       c_files[PRESET_MAX_FILES];
        for (int f = 0; f < PRESET_MAX_FILES; f++) {
            char column[8];
            snprintf(column, sizeof(column), "File%d", f + 1);
            c_files[f] = DataTable_GetColumnIndex(table, column);
        }
        for (int row = 0; row < rows; row++) {
            Preset *preset = &presets[preset_count];
            memset(preset, 0, sizeof(Preset));
            snprintf(preset->name, sizeof(preset->name), "%s", DataTable_GetString(table, row, c_name));
            preset->size_x = DataTable_GetInt(table, row, c_sx);
            preset->size_y = DataTable_GetInt(table, row, c_sy);
            for (int f = 0; f < PRESET_MAX_FILES; f++) {
                const char *file = DataTable_GetString(table, row, c_files[f]);
                if (file[0] != '\0' && strcmp(file, "0") != 0) {
                    snprintf(preset->files[preset->file_count++], sizeof(preset->files[0]), "%s", file);
                }
            }
            if (preset->name[0] != '\0' && preset->file_count > 0) {
                preset_count++;
            }
        }
        DataTable_Destroy(&table);
    }

    table = DataTable_Load(EXCEL_LVLSUB);
    if (table != NULL) {
        const int rows = DataTable_GetRowCount(table);
        sub_files      = calloc(rows > 0 ? (size_t)rows : 1, sizeof(SubFile));
        FAIL_IF_NULL(sub_files);
        const int c_type = DataTable_GetColumnIndex(table, "Type");
        const int c_file = DataTable_GetColumnIndex(table, "File");
        for (int row = 0; row < rows; row++) {
            const char *file = DataTable_GetString(table, row, c_file);
            if (file[0] == '\0' || !DataTable_HasValue(table, row, c_type)) {
                continue; // the "Expansion" divider
            }
            SubFile *sub = &sub_files[sub_count++];
            sub->type    = DataTable_GetInt(table, row, c_type);
            snprintf(sub->file, sizeof(sub->file), "%s", file);
            for (int t = 0; t < 5; t++) {
                char column[16];
                snprintf(column, sizeof(column), "Prob%d", t);
                sub->prob[t] = DataTable_GetInt(table, row, DataTable_GetColumnIndex(table, column));
                snprintf(column, sizeof(column), "Trials%d", t);
                sub->trials[t] = DataTable_GetInt(table, row, DataTable_GetColumnIndex(table, column));
                snprintf(column, sizeof(column), "Max%d", t);
                sub->max[t] = DataTable_GetInt(table, row, DataTable_GetColumnIndex(table, column));
            }
        }
        DataTable_Destroy(&table);
    }

    levels_table   = DataTable_Load(EXCEL_LEVELS);
    maze_table     = DataTable_Load(EXCEL_LVLMAZE);
    monstats_table = DataTable_Load(EXCEL_MONSTATS);
}

static const Preset *Builder__FindPreset(const char *name) {
    for (int i = 0; i < preset_count; i++) {
        if (Builder__SameName(presets[i].name, name)) {
            return &presets[i];
        }
    }
    return NULL;
}

// monstats.txt's MinGrp / MaxGrp for a monster (left as they are when it isn't there or has none)
static void Builder__GroupSize(const char *id, int *low, int *high) {
    if (monstats_table == NULL) {
        return;
    }
    const DataTable *t     = monstats_table;
    const int        c_id  = DataTable_GetColumnIndex(t, "Id");
    const int        c_min = DataTable_GetColumnIndex(t, "MinGrp");
    const int        c_max = DataTable_GetColumnIndex(t, "MaxGrp");
    for (int row = 0; row < DataTable_GetRowCount(t) && c_id >= 0; row++) {
        if (strcmp(DataTable_GetString(t, row, c_id), id) != 0) {
            continue;
        }
        const int a = DataTable_GetInt(t, row, c_min);
        const int b = DataTable_GetInt(t, row, c_max);
        if (a > 0 && b >= a) {
            *low  = a;
            *high = b > 12 ? 12 : b;
        }
        return;
    }
}

static bool Builder__GetLevel(const int level_id, LevelInfo *info) {
    memset(info, 0, sizeof(LevelInfo));
    if (levels_table == NULL) {
        return false;
    }
    const DataTable *t    = levels_table;
    const int        c_id = DataTable_GetColumnIndex(t, "Id");
    for (int row = 0; row < DataTable_GetRowCount(t); row++) {
        if (!DataTable_HasValue(t, row, c_id) || DataTable_GetInt(t, row, c_id) != level_id) {
            continue;
        }
        info->id = level_id;
        snprintf(info->name, sizeof(info->name), "%s",
                 DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "LevelName")));
        info->act                            = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "Act"));
        info->size_x                         = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SizeX"));
        info->size_y                         = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SizeY"));
        info->sub_type                       = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SubType"));
        info->sub_theme                      = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SubTheme"));
        info->sub_waypoint                   = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SubWaypoint"));
        info->sub_shrine                     = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "SubShrine"));
        info->waypoint                       = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "Waypoint")) != 255;
        static const char *density_columns[] = {"MonDen", "MonDen(N)", "MonDen(H)"};
        for (int d = 0; d < 3 && info->density <= 0; d++) {
            info->density = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, density_columns[d]));
        }
        for (int m = 0; m < 10; m++) {
            char column[8];
            snprintf(column, sizeof(column), "mon%d", m + 1);
            const char *id = DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column));
            if (id[0] != '\0') {
                info->group_min[info->monster_count] = 2; // (without monstats: 2 to 5)
                info->group_max[info->monster_count] = 5;
                Builder__GroupSize(id, &info->group_min[info->monster_count], &info->group_max[info->monster_count]);
                snprintf(info->monsters[info->monster_count++], sizeof(info->monsters[0]), "%s", id);
            }
        }
        break;
    }
    if (info->id == 0) {
        return false;
    }

    if (maze_table != NULL) {
        const int c_level = DataTable_GetColumnIndex(maze_table, "Level");
        const int c_rooms = DataTable_GetColumnIndex(maze_table, "Rooms");
        for (int row = 0; row < DataTable_GetRowCount(maze_table); row++) {
            if (DataTable_GetInt(maze_table, row, c_level) == level_id) {
                info->maze_rooms = DataTable_GetInt(maze_table, row, c_rooms);
            }
        }
    }
    return true;
}

static const DS1 *Builder__LoadDS1(const char *file) {
    char path[128];
    snprintf(path, sizeof(path), "data\\global\\tiles\\%s", file);
    for (char *c = path; *c != '\0'; c++) {
        *c = *c == '/' ? '\\' : *c;
    }
    for (int i = 0; i < ds1_cache_count; i++) {
        if (Builder__SameName(ds1_cache[i].path, path)) {
            return ds1_cache[i].ds1;
        }
    }
    DS1 *ds1 = DS1_Load(path);
    if (ds1 == NULL) {
        LOG_WARN("Missing preset '%s'", path);
        return NULL;
    }
    if (ds1_cache_count == DS1_CACHE_SIZE) {
        DS1_Destroy(&ds1_cache[0].ds1); // the oldest goes
        memmove(&ds1_cache[0], &ds1_cache[1], sizeof(CachedDS1) * (DS1_CACHE_SIZE - 1));
        ds1_cache_count--;
    }
    snprintf(ds1_cache[ds1_cache_count].path, sizeof(ds1_cache[0].path), "%s", path);
    ds1_cache[ds1_cache_count].ds1 = ds1;
    ds1_cache_count++;
    return ds1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Randomness: our own, so a seed always gives the same layout

typedef struct Rng {
    uint32_t state;
} Rng;

static uint32_t Rng_Next(Rng *rng) {
    uint32_t x  = rng->state;
    x          ^= x << 13;
    x          ^= x >> 17;
    x          ^= x << 5;
    rng->state  = x;
    return x;
}

static int Rng_Below(Rng *rng, const int n) { return n > 0 ? (int)(Rng_Next(rng) % (uint32_t)n) : 0; }

static bool Rng_Chance(Rng *rng, const int percent) { return Rng_Below(rng, 100) < percent; }

static const DS1 *Builder__PickFile(Rng *rng, const Preset *preset) {
    if (preset == NULL || preset->file_count == 0) {
        return NULL;
    }
    const int first = Rng_Below(rng, preset->file_count);
    for (int i = 0; i < preset->file_count; i++) {
        const DS1 *ds1 = Builder__LoadDS1(preset->files[(first + i) % preset->file_count]);
        if (ds1 != NULL) {
            return ds1;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared steps

// A cell of a preset is open ground when it has a floor and nothing standing on it
static bool Builder__IsOpen(const DS1 *ds1, const int x, const int y) {
    const int c     = (y * ds1->width) + x;
    bool      floor = false;
    for (int f = 0; f < ds1->floor_layer_count; f++) {
        floor = floor || ds1->floors[f][c].prop1 != 0;
    }
    for (int w = 0; w < ds1->wall_layer_count; w++) {
        const DS1Cell *cell = &ds1->walls[w][c];
        if (cell->prop1 != 0 && cell->orientation != 15) { // roofs don't block
            return false;
        }
    }
    return floor;
}

// The grounds a piece's open cells use, most used first (up to max). Border pieces are mostly the game's impassable
// grass (the strip behind their trees), so the most used isn't always one you can walk on: see Builder__PickGround.
static int Builder__FindGrounds(const DS1 *ds1, DS1Cell *grounds, const int max) {
    static int counts[64 * 64];
    memset(counts, 0, sizeof(counts));
    for (int y = 0; y < ds1->height; y++) {
        for (int x = 0; x < ds1->width; x++) {
            const DS1Cell *cell = &ds1->floors[0][(y * ds1->width) + x];
            if (ds1->floor_layer_count == 0 || cell->prop1 == 0 || !Builder__IsOpen(ds1, x, y)) {
                continue;
            }
            counts[((cell->style & 63) * 64) + (cell->sequence & 63)]++;
        }
    }
    int found = 0;
    while (found < max) {
        int best = -1;
        for (int key = 0; key < 64 * 64; key++) {
            best = counts[key] > 0 && (best < 0 || counts[key] > counts[best]) ? key : best;
        }
        if (best < 0) {
            break;
        }
        counts[best] = 0;
        memset(&grounds[found], 0, sizeof(DS1Cell));
        grounds[found].prop1    = 1;
        grounds[found].style    = (uint8_t)(best / 64);
        grounds[found].sequence = (uint8_t)(best % 64);
        found++;
    }
    return found;
}

static bool Builder__FindGround(const DS1 *ds1, DS1Cell *ground) { return Builder__FindGrounds(ds1, ground, 1) > 0; }

// Plain ground over a block, using the tile sets of the piece the ground came from
static void Builder__StampGround(Map *map, const DS1 *source, const DS1Cell *ground, const int tx, const int ty,
                                 const int width, const int height) {
    DS1 block;
    memset(&block, 0, sizeof(DS1));
    block.width             = width + 1;
    block.height            = height + 1;
    block.file_count        = source->file_count;
    block.files             = source->files;
    block.floor_layer_count = 1;
    block.floors[0]         = calloc((size_t)block.width * (size_t)block.height, sizeof(DS1Cell));
    block.shadows           = calloc((size_t)block.width * (size_t)block.height, sizeof(DS1Cell));
    FAIL_IF_NULL(block.floors[0]);
    FAIL_IF_NULL(block.shadows);
    for (int c = 0; c < block.width * block.height; c++) {
        block.floors[0][c] = *ground;
    }
    Map_StampDS1(map, &block, tx, ty, MAP_STAMP_FILLED_ONLY);
    free(block.floors[0]);
    free(block.shadows);
}

// The most used ground of a piece that you can walk on (tried on the map's corner, which is covered later anyway)
static bool Builder__PickGround(Map *map, const DS1 *source, DS1Cell *ground) {
    DS1Cell   grounds[8];
    const int count = Builder__FindGrounds(source, grounds, 8);
    for (int g = 0; g < count; g++) {
        Builder__StampGround(map, source, &grounds[g], 0, 0, 0, 0); // adds the piece's tile sets
        Map_Finish(map);                                            // so the tiles can be looked up
        Map_SetCellRaw(map, MAP_LAYER_FLOOR, 0, 0, 0, &grounds[g]);
        bool walkable = true;
        for (int s = 0; s < MAP_SUBTILES * MAP_SUBTILES && walkable; s++) {
            walkable = Map_IsWalkable(map, s % MAP_SUBTILES, s / MAP_SUBTILES);
        }
        if (walkable) {
            *ground = grounds[g];
            return true;
        }
    }
    return count > 0 && (*ground = grounds[0], true);
}

// The biggest stretch of ground you can walk around in: the player starts there (not in a pocket closed off by a
// fence or a cliff), and that's where the monsters go. Returns a mask over the sub-tiles (free it).
static uint8_t *Builder__MainArea(Map *map) {
    const int sub_w = map->width * MAP_SUBTILES;
    const int sub_h = map->height * MAP_SUBTILES;
    const int size  = sub_w * sub_h;
    int      *label = calloc((size_t)size, sizeof(int));
    int      *queue = malloc(sizeof(int) * (size_t)size);
    uint8_t  *main  = calloc((size_t)size, 1);
    FAIL_IF_NULL(label);
    FAIL_IF_NULL(queue);
    FAIL_IF_NULL(main);
    int best_label = 0;
    int best_size  = 0;
    int labels     = 0;
    for (int start = 0; start < size; start++) {
        if (label[start] != 0 || !Map_IsWalkable(map, start % sub_w, start / sub_w)) {
            continue;
        }
        labels++;
        int head = 0, tail = 0;
        queue[tail++] = start;
        label[start]  = labels;
        while (head < tail) {
            const int        c     = queue[head++];
            const int        x     = c % sub_w;
            const int        y     = c / sub_w;
            static const int dx[4] = {1, -1, 0, 0};
            static const int dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; d++) {
                const int nx = x + dx[d];
                const int ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= sub_w || ny >= sub_h) {
                    continue;
                }
                const int n = (ny * sub_w) + nx;
                if (label[n] == 0 && Map_IsWalkable(map, nx, ny)) {
                    label[n]      = labels;
                    queue[tail++] = n;
                }
            }
        }
        if (tail > best_size) {
            best_size  = tail;
            best_label = labels;
        }
    }

    // Start on the main area, as close as possible to where the area's entrance is (or its middle)
    const float want_x = map->has_start ? map->start_x : (float)sub_w / 2.0f;
    const float want_y = map->has_start ? map->start_y : (float)sub_h / 2.0f;
    float       best   = -1.0f;
    for (int c = 0; c < size; c++) {
        if (best_label == 0 || label[c] != best_label) {
            continue;
        }
        main[c]        = 1;
        const float dx = (float)(c % sub_w) + 0.5f - want_x;
        const float dy = (float)(c / sub_w) + 0.5f - want_y;
        if (best < 0.0f || (dx * dx) + (dy * dy) < best) {
            best           = (dx * dx) + (dy * dy);
            map->start_x   = (float)(c % sub_w) + 0.5f;
            map->start_y   = (float)(c / sub_w) + 0.5f;
            map->has_start = true;
        }
    }
    free(label);
    free(queue);
    return main;
}

// Monsters in packs from the area's list, spread over open ground away from the entrance
static void Builder__Populate(Map *map, const LevelInfo *level, const uint8_t *main_area, Rng *rng) {
    if (level->monster_count == 0) {
        return;
    }
    const int area   = map->width * map->height;
    int       groups = (area * (level->density > 0 ? level->density : 500)) / 250000;
    groups           = groups < 3 ? 3 : (groups > 40 ? 40 : groups);
    const int sub_w  = map->width * MAP_SUBTILES;
    const int sub_h  = map->height * MAP_SUBTILES;
    for (int g = 0; g < groups; g++) {
        const int   pick    = Rng_Below(rng, level->monster_count);
        const char *monster = level->monsters[pick];
        for (int attempt = 0; attempt < 60; attempt++) {
            const int x = Rng_Below(rng, sub_w);
            const int y = Rng_Below(rng, sub_h);
            if (!main_area[(y * sub_w) + x]) {
                continue;
            }
            bool crowded = false;
            if (map->has_start) {
                const float dx = (float)x - map->start_x;
                const float dy = (float)y - map->start_y;
                crowded        = (dx * dx) + (dy * dy) < 30.0f * 30.0f;
            }
            for (int e = 0; e < map->exit_count && !crowded; e++) {
                const float dx = (float)x - map->exits[e].arrive_x;
                const float dy = (float)y - map->exits[e].arrive_y;
                crowded        = (dx * dx) + (dy * dy) < 25.0f * 25.0f;
            }
            if (crowded) {
                continue; // let the player get their bearings first
            }
            // Now and then a pack of champions, or a unique with its minions (the game: about one pack in ten)
            const int   kind = Rng_Below(rng, 100);
            const char *mark = kind < 7 ? "@champion" : (kind < 15 ? "@unique" : monster);
            // A pack's size: monstats' MinGrp to MaxGrp (champions 2 to 4, a unique and its minions 2 to 5)
            const int low  = level->group_min[pick];
            const int high = level->group_max[pick];
            const int size = kind < 7 ? 2 + Rng_Below(rng, 3)
                                      : (kind < 15 ? 2 + Rng_Below(rng, 4) : low + Rng_Below(rng, high - low + 1));
            for (int m = 0; m < size; m++) {
                for (int spot = 0; spot < 12; spot++) {
                    const int spread = size > 6 ? 6 : 4; // (a big pack spreads out more)
                    const int mx     = x + Rng_Below(rng, (spread * 2) + 1) - spread;
                    const int my     = y + Rng_Below(rng, (spread * 2) + 1) - spread;
                    if (mx >= 0 && my >= 0 && mx < sub_w && my < sub_h && main_area[(my * sub_w) + mx]) {
                        const char *name = m > 0 && kind >= 7 && kind < 15 ? "@minion" : mark;
                        Map_AddEntity(map, "monsters", monster, name, (float)mx + 0.5f, (float)my + 0.5f);
                        break;
                    }
                }
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Decorations (LvlSub.txt): small groups of trees, stones, puddles, ... cut out of the sub files

typedef struct Cluster {
    const DS1 *ds1;
    int        count;
    int        min_x;
    int        min_y;
    int        width;
    int        height;
    int        cells[CLUSTER_MAX_CELLS]; // indices into the sub file
} Cluster;

static bool Builder__IsDecoration(const DS1 *ds1, const int c, const DS1Cell *ground) {
    for (int w = 0; w < ds1->wall_layer_count; w++) {
        if (ds1->walls[w][c].prop1 != 0) {
            return true;
        }
    }
    for (int f = 0; f < ds1->floor_layer_count; f++) {
        const DS1Cell *cell = &ds1->floors[f][c];
        if (cell->prop1 != 0 && (f > 0 || cell->style != ground->style || cell->sequence != ground->sequence)) {
            return true;
        }
    }
    return false;
}

static int Builder__FindClusters(const DS1 *ds1, const DS1Cell *ground, Cluster *clusters, const int max_clusters) {
    const int size  = ds1->width * ds1->height;
    uint8_t  *taken = calloc((size_t)size, 1);
    FAIL_IF_NULL(taken);
    int found = 0;
    int stack[CLUSTER_MAX_CELLS * 4];
    for (int start = 0; start < size && found < max_clusters; start++) {
        if (taken[start] || !Builder__IsDecoration(ds1, start, ground)) {
            continue;
        }
        Cluster *cluster = &clusters[found];
        memset(cluster, 0, sizeof(Cluster));
        cluster->ds1   = ds1;
        int sp         = 0;
        stack[sp++]    = start;
        taken[start]   = 1;
        int max_x      = 0;
        int max_y      = 0;
        cluster->min_x = ds1->width;
        cluster->min_y = ds1->height;
        while (sp > 0) {
            const int c = stack[--sp];
            if (cluster->count < CLUSTER_MAX_CELLS) {
                cluster->cells[cluster->count++] = c;
            }
            const int x    = c % ds1->width;
            const int y    = c / ds1->width;
            cluster->min_x = x < cluster->min_x ? x : cluster->min_x;
            cluster->min_y = y < cluster->min_y ? y : cluster->min_y;
            max_x          = x > max_x ? x : max_x;
            max_y          = y > max_y ? y : max_y;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= ds1->width || ny >= ds1->height) {
                        continue;
                    }
                    const int n = (ny * ds1->width) + nx;
                    if (!taken[n] && Builder__IsDecoration(ds1, n, ground) &&
                        sp < (int)(sizeof(stack) / sizeof(stack[0]))) {
                        taken[n]    = 1;
                        stack[sp++] = n;
                    }
                }
            }
        }
        cluster->width  = max_x - cluster->min_x + 1;
        cluster->height = max_y - cluster->min_y + 1;
        if (cluster->count < CLUSTER_MAX_CELLS) {
            found++;
        }
    }
    free(taken);
    return found;
}

static void Builder__StampCluster(Map *map, const Cluster *cluster, const int tx, const int ty) {
    // A tiny preset holding just the cluster's cells
    const DS1 *src = cluster->ds1;
    DS1        part;
    memset(&part, 0, sizeof(DS1));
    part.width             = cluster->width;
    part.height            = cluster->height;
    part.file_count        = src->file_count;
    part.files             = src->files;
    part.wall_layer_count  = src->wall_layer_count;
    part.floor_layer_count = src->floor_layer_count;
    const size_t cells     = (size_t)part.width * (size_t)part.height;
    for (int w = 0; w < part.wall_layer_count; w++) {
        part.walls[w] = calloc(cells, sizeof(DS1Cell));
        FAIL_IF_NULL(part.walls[w]);
    }
    for (int f = 0; f < part.floor_layer_count; f++) {
        part.floors[f] = calloc(cells, sizeof(DS1Cell));
        FAIL_IF_NULL(part.floors[f]);
    }
    part.shadows = calloc(cells, sizeof(DS1Cell));
    FAIL_IF_NULL(part.shadows);
    for (int i = 0; i < cluster->count; i++) {
        const int c = cluster->cells[i];
        const int x = (c % src->width) - cluster->min_x;
        const int y = (c / src->width) - cluster->min_y;
        const int d = (y * part.width) + x;
        for (int w = 0; w < part.wall_layer_count; w++) {
            part.walls[w][d] = src->walls[w][c];
        }
        for (int f = 0; f < part.floor_layer_count; f++) {
            part.floors[f][d] = src->floors[f][c];
        }
        part.shadows[d] = src->shadows[c];
    }
    Map_StampDS1(map, &part, tx, ty, MAP_STAMP_FILLED_ONLY);
    for (int w = 0; w < part.wall_layer_count; w++) {
        free(part.walls[w]);
    }
    for (int f = 0; f < part.floor_layer_count; f++) {
        free(part.floors[f]);
    }
    free(part.shadows);
}

// ---------------------------------------------------------------------------------------------------------------------
// Entrances

// The entrances in a preset (cave mouths, stairs, tomb doors): special tiles (orientation 10/11) with a low main index
// (30 and up are start and town portal markers). Neighbouring tiles are one entrance. Returns the count (tiles).
static int Builder__FindWarps(const DS1 *ds1, int *xs, int *ys, int *slots, const int max) {
    int count = 0;
    for (int w = 0; w < ds1->wall_layer_count; w++) {
        for (int c = 0; c < ds1->width * ds1->height; c++) {
            const DS1Cell *cell = &ds1->walls[w][c];
            if (cell->prop1 == 0 || (cell->orientation != 10 && cell->orientation != 11) || cell->style >= 8) {
                continue;
            }
            const int x    = c % ds1->width;
            const int y    = c / ds1->width;
            bool      near = false;
            for (int i = 0; i < count && !near; i++) {
                // Tiles side by side are one entrance, unless they lead to different places (the Forgotten Tower's
                // way out and stairs down are next to each other)
                near = abs(xs[i] - x) <= 1 && abs(ys[i] - y) <= 1 && slots[i] == cell->style;
            }
            if (!near && count < max) {
                xs[count]      = x;
                ys[count]      = y;
                slots[count++] = cell->style; // Levels.txt Vis<slot> is where it leads
            }
        }
    }
    return count;
}

// An exit on an entrance tile; you arrive next to it
static void Builder__AddWarpExit(Map *map, const int target, const int tx, const int ty) {
    const int x      = tx * MAP_SUBTILES;
    const int y      = ty * MAP_SUBTILES;
    const int before = map->exit_count;
    Map_AddExit(map, target, x - 1, y - 1, x + MAP_SUBTILES, y + MAP_SUBTILES, (float)x + 2.5f, (float)y + 2.5f);
    if (map->exit_count > before) {
        map->exits[before].entrance = true;
    }
}

// Is this area reached on foot from that one?
static bool Builder__IsWalkLink(const int level_id, const int target) {
    int       targets[4];
    WorldSide sides[4];
    const int count = World_GetWalkLinks(level_id, targets, sides, 4);
    for (int i = 0; i < count; i++) {
        if (targets[i] == target) {
            return true;
        }
    }
    return false;
}

// The areas entered through this one's entrances (not the ones you walk to)
static int Builder__WarpTargets(const int level_id, int *targets, const int max) {
    int       all[8];
    const int count = World_GetWarpLinks(level_id, all, 8);
    int       kept  = 0;
    for (int i = 0; i < count && kept < max; i++) {
        if (!Builder__IsWalkLink(level_id, all[i])) {
            targets[kept++] = all[i];
        }
    }
    return kept;
}

// ---------------------------------------------------------------------------------------------------------------------
// Wilderness

typedef struct Grid {
    int     width; // blocks
    int     height;
    uint8_t used[MAX_GRID][MAX_GRID];
} Grid;

static bool Grid__Free(const Grid *grid, const int x, const int y, const int w, const int h) {
    if (x < 1 || y < 1 || x + w > grid->width - 1 || y + h > grid->height - 1) {
        return false; // the border ring stays
    }
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (grid->used[j][i]) {
                return false;
            }
        }
    }
    return true;
}

static void Grid__Take(Grid *grid, const int x, const int y, const int w, const int h) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            grid->used[j][i] = 1;
        }
    }
}

// Puts a piece somewhere free inside the ring; false if there's no room
static bool Builder__PlacePiece(Map *map, Grid *grid, Rng *rng, const Preset *preset, const int cell, const bool any,
                                const DS1 **placed, int *placed_tx, int *placed_ty) {
    const DS1 *ds1 = Builder__PickFile(rng, preset);
    if (ds1 == NULL) {
        return false;
    }
    const int w = preset->size_x > 0 ? (preset->size_x + cell - 1) / cell : (ds1->width - 1 + cell - 1) / cell;
    const int h = preset->size_y > 0 ? (preset->size_y + cell - 1) / cell : (ds1->height - 1 + cell - 1) / cell;
    for (int attempt = 0; attempt < (any ? 200 : 12); attempt++) {
        const int x = 1 + Rng_Below(rng, grid->width - 1 - w);
        const int y = 1 + Rng_Below(rng, grid->height - 1 - h);
        if (Grid__Free(grid, x, y, w, h)) {
            Grid__Take(grid, x, y, w, h);
            Map_StampDS1(map, ds1, x * cell, y * cell, 0);
            if (placed != NULL) {
                *placed    = ds1;
                *placed_tx = x * cell;
                *placed_ty = y * cell;
            }
            return true;
        }
    }
    return false;
}

typedef struct BorderSet {
    const DS1 *pieces[16][8]; // by shape (CORNER_* bits)
    int        counts[16];
    const DS1 *openings[16]; // the straight edges with a path through them ("Bord1o.ds1"), NULL = none
} BorderSet;

static bool Builder__IsOpening(const char *file) {
    // "Bord1o.ds1", "Bord1oe.ds1": the same piece with a gap to the next area
    const char *dot = strrchr(file, '.');
    return dot != NULL && dot - file >= 2 &&
           (tolower((unsigned char)dot[-1]) == 'o' ||
            (tolower((unsigned char)dot[-1]) == 'e' && tolower((unsigned char)dot[-2]) == 'o'));
}

// The game numbers its border pieces the same way in every act: which corners each one closes off
static const int border_shapes[12] = {EDGE_S,   EDGE_W,   EDGE_N,    EDGE_E,    OUTER_SW,  OUTER_NW,
                                      OUTER_NE, OUTER_SE, CORNER_BL, CORNER_TL, CORNER_TR, CORNER_BR};

static bool Builder__IsOpeningEnd(const char *file) {
    const char *dot = strrchr(file, '.');
    return dot != NULL && dot - file >= 2 && tolower((unsigned char)dot[-1]) == 'e' &&
           tolower((unsigned char)dot[-2]) == 'o';
}

static void Builder__LoadBorders(const OutdoorArea *area, BorderSet *set) {
    memset(set, 0, sizeof(BorderSet));
    const size_t prefix = strlen(area->borders);
    for (int i = 0; i < preset_count; i++) {
        const Preset *preset = &presets[i];
        if (!Builder__StartsWith(preset->name, area->borders) ||
            (area->border_require != NULL && strstr(preset->name, area->border_require) == NULL) ||
            (area->border_exclude != NULL && strstr(preset->name, area->border_exclude) != NULL)) {
            continue;
        }
        const int number = atoi(preset->name + prefix); // "Act 1 - Wild Border 7" -> 7
        int       shape  = number >= 1 && number <= 12 ? border_shapes[number - 1] : 0;
        if (shape == 0) {
            // Kurast's are named by the side they close off: "Act 3 - Slums Border NE"
            static const char *const names[8] = {"N", "S", "E", "W", "NE", "NW", "SE", "SW"};
            static const int shapes[8] = {EDGE_N, EDGE_S, EDGE_E, EDGE_W, OUTER_NE, OUTER_NW, OUTER_SE, OUTER_SW};
            for (int n = 0; n < 8; n++) {
                shape = strcmp(preset->name + prefix, names[n]) == 0 ? shapes[n] : shape;
            }
        }
        if (shape == 0) {
            continue;
        }
        for (int f = 0; f < preset->file_count && set->counts[shape] < 8; f++) {
            const DS1 *ds1 = Builder__LoadDS1(preset->files[f]);
            if (ds1 == NULL) {
                continue;
            }
            if (!Builder__IsOpening(preset->files[f])) {
                set->pieces[shape][set->counts[shape]++] = ds1;
            } else if (set->openings[shape] == NULL && !Builder__IsOpeningEnd(preset->files[f])) {
                set->openings[shape] = ds1;
            }
        }
    }
}

static const DS1 *Builder__Border(const BorderSet *set, Rng *rng, const int shape) {
    return set->counts[shape] > 0 ? set->pieces[shape][Rng_Below(rng, set->counts[shape])] : NULL;
}

// Pushes a stretch of one side of the ring in by a block, using the inner corner pieces
static void Builder__Dent(Grid *grid, int shapes[MAX_GRID][MAX_GRID], const BorderSet *set, Rng *rng) {
    if (set->counts[CORNER_TL] == 0 || set->counts[CORNER_TR] == 0 || set->counts[CORNER_BL] == 0 ||
        set->counts[CORNER_BR] == 0) {
        return;
    }
    const int side   = Rng_Below(rng, 4); // 0 N, 1 S, 2 W, 3 E
    const int length = (side < 2) ? grid->width : grid->height;
    if (length < 8) {
        return;
    }
    const int run = 1 + Rng_Below(rng, 2);
    const int a   = 3 + Rng_Below(rng, length - 6 - run);
    const int b   = a + run - 1;

    // (outer, inner) block positions along the side, and the pieces the stretch and its two ends need
    for (int i = a - 1; i <= b + 1; i++) {
        int ox, oy, ix, iy;
        switch (side) {
        case 0:
            ox = i, oy = 0, ix = i, iy = 1;
            break;
        case 1:
            ox = i, oy = grid->height - 1, ix = i, iy = grid->height - 2;
            break;
        case 2:
            ox = 0, oy = i, ix = 1, iy = i;
            break;
        default:
            ox = grid->width - 1, oy = i, ix = grid->width - 2, iy = i;
            break;
        }
        if (grid->used[iy][ix] || shapes[iy][ix] != 0) {
            return; // something's already there: no dent this time
        }
        (void)ox;
        (void)oy;
    }
    static const int edge[4]       = {EDGE_N, EDGE_S, EDGE_W, EDGE_E};
    static const int before_out[4] = {OUTER_NE, OUTER_SE, OUTER_SW, OUTER_SE}; // the block before the stretch
    static const int before_in[4]  = {CORNER_TR, CORNER_BR, CORNER_BL, CORNER_BR};
    static const int after_out[4]  = {OUTER_NW, OUTER_SW, OUTER_NW, OUTER_NE};
    static const int after_in[4]   = {CORNER_TL, CORNER_BL, CORNER_TL, CORNER_TR};
    for (int i = a - 1; i <= b + 1; i++) {
        int ox, oy, ix, iy;
        if (side == 0) {
            ox = i, oy = 0, ix = i, iy = 1;
        } else if (side == 1) {
            ox = i, oy = grid->height - 1, ix = i, iy = grid->height - 2;
        } else if (side == 2) {
            ox = 0, oy = i, ix = 1, iy = i;
        } else {
            ox = grid->width - 1, oy = i, ix = grid->width - 2, iy = i;
        }
        if (i == a - 1) {
            shapes[oy][ox] = before_out[side];
            shapes[iy][ix] = before_in[side];
        } else if (i == b + 1) {
            shapes[oy][ox] = after_out[side];
            shapes[iy][ix] = after_in[side];
        } else {
            shapes[oy][ox] = -1; // outside the area: nothing
            shapes[iy][ix] = edge[side];
        }
        grid->used[iy][ix] = 1;
    }
}

static void Builder__Decorate(Map *map, const Grid *grid, const LevelInfo *level, const DS1 *ground_source,
                              const DS1Cell *ground, const int cell, Rng *rng) {
    // Waypoint and shrines first: they need a whole free block
    int free_count = 0;
    int free_x[MAX_GRID * MAX_GRID];
    int free_y[MAX_GRID * MAX_GRID];
    for (int y = 1; y < grid->height - 1; y++) {
        for (int x = 1; x < grid->width - 1; x++) {
            if (!grid->used[y][x]) {
                free_x[free_count]   = x;
                free_y[free_count++] = y;
            }
        }
    }
    for (int i = free_count - 1; i > 0; i--) { // shuffle
        const int j = Rng_Below(rng, i + 1);
        int       t = free_x[i];
        free_x[i]   = free_x[j];
        free_x[j]   = t;
        t           = free_y[i];
        free_y[i]   = free_y[j];
        free_y[j]   = t;
    }
    int next = 0;

    const int specials[2] = {level->waypoint ? level->sub_waypoint : -1, level->sub_shrine};
    const int amounts[2]  = {1, 1 + Rng_Below(rng, 2)};
    for (int s = 0; s < 2; s++) {
        for (int n = 0; n < amounts[s] && specials[s] >= 0 && next < free_count; n++) {
            int candidates[16];
            int count = 0;
            for (int i = 0; i < sub_count && count < 16; i++) {
                if (sub_files[i].type == specials[s]) {
                    candidates[count++] = i;
                }
            }
            if (count == 0) {
                break;
            }
            const DS1 *ds1 = Builder__LoadDS1(sub_files[candidates[Rng_Below(rng, count)]].file);
            if (ds1 == NULL || ds1->width > cell + 1 || ds1->height > cell + 1) {
                continue;
            }
            const int x = (free_x[next] * cell) + (cell - ds1->width + 1) / 2;
            const int y = (free_y[next] * cell) + (cell - ds1->height + 1) / 2;
            next++;
            Map_StampDS1(map, ds1, x, y, MAP_STAMP_FILLED_ONLY | MAP_STAMP_NO_MONSTERS);
        }
    }

    // Scattered trees, stones, puddles, ... over what's left
    if (level->sub_type < 0 || level->sub_theme < 0 || level->sub_theme > 4) {
        return;
    }
    for (int i = 0; i < sub_count; i++) {
        const SubFile *sub = &sub_files[i];
        if (sub->type != level->sub_type || sub->prob[level->sub_theme] <= 0) {
            continue;
        }
        const DS1 *ds1 = Builder__LoadDS1(sub->file);
        if (ds1 == NULL) {
            continue;
        }
        Cluster   clusters[MAX_CLUSTERS];
        const int cluster_count = Builder__FindClusters(ds1, ground, clusters, MAX_CLUSTERS);
        if (cluster_count == 0) {
            continue;
        }
        const int trials = sub->trials[level->sub_theme] > 0 ? sub->trials[level->sub_theme] : 1;
        const int max    = sub->max[level->sub_theme] > 0 ? sub->max[level->sub_theme] : 1;
        for (int b = next; b < free_count; b++) {
            int placed = 0;
            for (int t = 0; t < trials && placed < max; t++) {
                if (!Rng_Chance(rng, sub->prob[level->sub_theme])) {
                    continue;
                }
                const Cluster *cluster = &clusters[Rng_Below(rng, cluster_count)];
                if (cluster->width >= cell - 1 || cluster->height >= cell - 1) {
                    continue;
                }
                const int x = (free_x[b] * cell) + 1 + Rng_Below(rng, cell - 1 - cluster->width);
                const int y = (free_y[b] * cell) + 1 + Rng_Below(rng, cell - 1 - cluster->height);
                Builder__StampCluster(map, cluster, x, y);
                placed++;
            }
        }
    }
    (void)ground_source;
}

static Map *Builder__BuildOutdoor(const OutdoorArea *area, const LevelInfo *level, Rng *rng) {
    const int cell   = area->cell;
    const int size_x = area->size_x > 0 ? area->size_x : level->size_x;
    const int size_y = area->size_y > 0 ? area->size_y : level->size_y;
    Grid      grid;
    memset(&grid, 0, sizeof(Grid));
    grid.width  = size_x / cell;
    grid.height = size_y / cell;
    if (grid.width < 3 || grid.height < 3 || grid.width > MAX_GRID || grid.height > MAX_GRID ||
        size_x + 1 > MAP_MAX_SIZE || size_y + 1 > MAP_MAX_SIZE) {
        return NULL;
    }

    BorderSet borders;
    Builder__LoadBorders(area, &borders);
    static const int needed[8] = {EDGE_N, EDGE_S, EDGE_W, EDGE_E, OUTER_NW, OUTER_NE, OUTER_SW, OUTER_SE};
    for (int i = 0; i < 8; i++) {
        if (borders.counts[needed[i]] == 0) {
            LOG_WARN("%s: no border piece of shape %d", level->name, needed[i]);
            return NULL;
        }
    }

    DS1Cell    ground;
    const DS1 *ground_source = borders.pieces[EDGE_N][0];
    if (!Builder__FindGround(ground_source, &ground)) {
        return NULL;
    }

    Map *map = Map_CreateEmpty(size_x + 1, size_y + 1, level->act);
    snprintf(map->name, sizeof(map->name), "%s", level->name);
    Builder__PickGround(map, ground_source, &ground); // not the impassable grass behind the border's trees

    // The ring of border pieces, with the odd dent
    static int shapes[MAX_GRID][MAX_GRID];
    memset(shapes, 0, sizeof(shapes));
    for (int x = 0; x < grid.width; x++) {
        shapes[0][x]               = EDGE_N;
        shapes[grid.height - 1][x] = EDGE_S;
    }
    for (int y = 0; y < grid.height; y++) {
        shapes[y][0]              = EDGE_W;
        shapes[y][grid.width - 1] = EDGE_E;
    }
    shapes[0][0]                            = OUTER_NW;
    shapes[0][grid.width - 1]               = OUTER_NE;
    shapes[grid.height - 1][0]              = OUTER_SW;
    shapes[grid.height - 1][grid.width - 1] = OUTER_SE;
    const int dents                         = (grid.width + grid.height) / 8;
    for (int d = 0; d < dents; d++) {
        Builder__Dent(&grid, shapes, &borders, rng);
    }

    // Openings to the neighbouring areas: a gap in the border near the middle of the side that leads there
    int       walk_targets[4];
    WorldSide walk_sides[4];
    const int walk_count = World_GetWalkLinks(level->id, walk_targets, walk_sides, 4);
    int       opening_x[4];
    int       opening_y[4];
    int       arrival_x[4] = {-1, -1, -1, -1};
    int       arrival_y[4] = {-1, -1, -1, -1};
    for (int l = 0; l < walk_count; l++) {
        const WorldSide side    = walk_sides[l];
        const int       edge    = side == WORLD_SIDE_N   ? EDGE_N
                                  : side == WORLD_SIDE_S ? EDGE_S
                                  : side == WORLD_SIDE_W ? EDGE_W
                                                         : EDGE_E;
        const bool      along_x = side == WORLD_SIDE_N || side == WORLD_SIDE_S;
        const int       length  = along_x ? grid.width : grid.height;
        opening_x[l]            = -1;
        for (int k = 0; k < length && opening_x[l] < 0; k++) {
            // From the middle outwards: middle, one after, one before, ...
            const int i = (length / 2) + ((k % 2 == 0) ? (k / 2) : -((k + 1) / 2));
            const int x = along_x ? i : (side == WORLD_SIDE_W ? 0 : grid.width - 1);
            const int y = along_x ? (side == WORLD_SIDE_N ? 0 : grid.height - 1) : i;
            if (i > 0 && i < length - 1 && shapes[y][x] == edge) {
                opening_x[l] = x;
                opening_y[l] = y;
                shapes[y][x] = -2 - edge; // an opening of that edge
                // The block inside it is where you arrive: kept clear of filler (landmarks still go anywhere)
                arrival_x[l] = side == WORLD_SIDE_W ? 1 : side == WORLD_SIDE_E ? grid.width - 2 : x;
                arrival_y[l] = side == WORLD_SIDE_N ? 1 : side == WORLD_SIDE_S ? grid.height - 2 : y;
            }
        }
    }

    // Open ground everywhere inside first; pieces go on top
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            if (shapes[y][x] == 0) {
                Builder__StampGround(map, ground_source, &ground, x * cell, y * cell, cell, cell);
            }
        }
    }
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            if (shapes[y][x] > 0) {
                const DS1 *piece = Builder__Border(&borders, rng, shapes[y][x]);
                if (piece != NULL) {
                    Map_StampDS1(map, piece, x * cell, y * cell, MAP_STAMP_NO_MONSTERS);
                }
            } else if (shapes[y][x] <= -2) {
                // The border piece with a path through it where there is one, plain ground otherwise
                const DS1 *piece = borders.openings[-2 - shapes[y][x]];
                if (piece != NULL) {
                    Map_StampDS1(map, piece, x * cell, y * cell, MAP_STAMP_NO_ENTITIES);
                } else {
                    Builder__StampGround(map, ground_source, &ground, x * cell, y * cell, cell, cell);
                }
            }
        }
    }
    for (int l = 0; l < walk_count; l++) {
        if (opening_x[l] < 0) {
            continue;
        }
        // Walking into the outer strip of the opening takes you on; you arrive a block inside it
        const int   span  = cell * MAP_SUBTILES;
        const int   x0    = opening_x[l] * span;
        const int   y0    = opening_y[l] * span;
        const int   sub_w = map->width * MAP_SUBTILES;
        const int   sub_h = map->height * MAP_SUBTILES;
        const float mid_x = (float)x0 + ((float)span / 2.0f);
        const float mid_y = (float)y0 + ((float)span / 2.0f);
        switch (walk_sides[l]) {
        case WORLD_SIDE_N:
            Map_AddExit(map, walk_targets[l], x0, 0, x0 + span, 2, mid_x, (float)span * 1.5f);
            break;
        case WORLD_SIDE_S:
            Map_AddExit(map, walk_targets[l], x0, sub_h - 3, x0 + span, sub_h - 1, mid_x,
                        (float)sub_h - ((float)span * 1.5f));
            break;
        case WORLD_SIDE_W:
            Map_AddExit(map, walk_targets[l], 0, y0, 2, y0 + span, (float)span * 1.5f, mid_y);
            break;
        default:
            Map_AddExit(map, walk_targets[l], sub_w - 3, y0, sub_w - 1, y0 + span, (float)sub_w - ((float)span * 1.5f),
                        mid_y);
            break;
        }
    }

    // Landmarks; their entrances lead to the areas this one opens into, in the order Levels.txt lists them
    int       warp_targets[8];
    const int warp_count = Builder__WarpTargets(level->id, warp_targets, 8);
    int       next_warp  = 0;
    int       portal_targets[4];
    int       portals = World_GetPortalLinks(level->id, portal_targets, 4);
    for (int i = 0; i < 10 && area->landmarks[i] != NULL; i++) {
        const Preset *preset = Builder__FindPreset(area->landmarks[i]);
        const DS1    *placed = NULL;
        int           px     = 0;
        int           py     = 0;
        if (preset == NULL || !Builder__PlacePiece(map, &grid, rng, preset, cell, true, &placed, &px, &py)) {
            LOG_WARN("%s: could not place '%s'", level->name, area->landmarks[i]);
            continue;
        }
        int       wx[4];
        int       wy[4];
        int       slots[4];
        const int warps = Builder__FindWarps(placed, wx, wy, slots, 4);
        if (warps == 0 && portals > 0) {
            // A landmark with no way in of its own (the Cairn Stones, the hell gates): the red portal stands in it
            const int pw = preset->size_x > 0 ? preset->size_x : placed->width - 1;
            const int ph = preset->size_y > 0 ? preset->size_y : placed->height - 1;
            World_AddPortal(map, portal_targets[--portals], (float)(((px * 2) + pw) * MAP_SUBTILES) / 2.0f + 0.5f,
                            (float)(((py * 2) + ph) * MAP_SUBTILES) / 2.0f + 0.5f);
        }
        for (int w = 0; w < warps; w++) {
            // Where the tile says it leads, or the next area Levels.txt lists
            // (two of the same landmark, like the Bazaar's temples, share a slot: the second takes the next one)
            int target = World_GetVis(level->id, slots[w]);
            if (target <= 0 || Builder__IsWalkLink(level->id, target) || Map_ExitTo(map, target) != NULL) {
                target = 0;
                while (target == 0 && next_warp < warp_count) {
                    target = warp_targets[next_warp++];
                    target = Map_ExitTo(map, target) != NULL ? 0 : target;
                }
            }
            if (target > 0) {
                Builder__AddWarpExit(map, target, px + wx[w], py + wy[w]);
            }
        }
    }
    for (int l = 0; l < walk_count; l++) {
        if (arrival_x[l] >= 0) {
            grid.used[arrival_y[l]][arrival_x[l]] = 1;
        }
    }
    int fill_count = 0;
    while (fill_count < 24 && area->fills[fill_count] != NULL) {
        fill_count++;
    }
    const int interior = (grid.width - 2) * (grid.height - 2);
    for (int n = 0; n < interior / 3 && fill_count > 0; n++) {
        const Preset *preset = Builder__FindPreset(area->fills[Rng_Below(rng, fill_count)]);
        if (preset != NULL) {
            Builder__PlacePiece(map, &grid, rng, preset, cell, false, NULL, NULL, NULL);
        }
    }

    Builder__Decorate(map, &grid, level, ground_source, &ground, cell, rng);
    Map_Finish(map);
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dungeons

typedef struct Room {
    int gx;
    int gy;
    int exits;
} Room;

static void Builder__ExitName(const int exits, char *text) {
    // N, S, E, W in that order, as the presets are named
    int n = 0;
    if (exits & EXIT_N) {
        text[n++] = 'N';
    }
    if (exits & EXIT_S) {
        text[n++] = 'S';
    }
    if (exits & EXIT_E) {
        text[n++] = 'E';
    }
    if (exits & EXIT_W) {
        text[n++] = 'W';
    }
    text[n] = '\0';
}

static const Preset *Builder__RoomPreset(const char *family, const int exits) {
    if (family == NULL) {
        return NULL;
    }
    char exit_name[8];
    char name[96];
    Builder__ExitName(exits, exit_name);
    snprintf(name, sizeof(name), "%s %s", family, exit_name);
    return Builder__FindPreset(name);
}

static const int dir_dx[4]   = {0, 0, 1, -1};
static const int dir_dy[4]   = {-1, 1, 0, 0};
static const int dir_exit[4] = {EXIT_N, EXIT_S, EXIT_E, EXIT_W};
static const int dir_back[4] = {EXIT_S, EXIT_N, EXIT_W, EXIT_E};

static int Builder__RoomAt(const Room *rooms, const int count, const int gx, const int gy) {
    for (int i = 0; i < count; i++) {
        if (rooms[i].gx == gx && rooms[i].gy == gy) {
            return i;
        }
    }
    return -1;
}

static int Builder__ExitCount(const int exits) {
    return ((exits & EXIT_N) != 0) + ((exits & EXIT_S) != 0) + ((exits & EXIT_E) != 0) + ((exits & EXIT_W) != 0);
}

// Rooms joined into a random tree; false if some room has no preset for its exits
static int Builder__GrowRooms(const MazeArea *area, Room *rooms, const int wanted, const int grid_w, const int grid_h,
                              Rng *rng) {
    int count      = 1;
    rooms[0].gx    = area->grid_w > 0 ? Rng_Below(rng, grid_w) : grid_w / 2;
    rooms[0].gy    = area->grid_w > 0 ? 0 : grid_h / 2;
    rooms[0].exits = 0;

    // Some ways in aren't dead ends (the tombs' stairs up sit in a room with three ways on): when the entry has no
    // one-exit piece, it starts with the exits of one of the pieces it does have, and grows no more
    if (area->entry != NULL && area->grid_w == 0) {
        bool dead_end = false;
        int  shapes[16];
        int  shape_count = 0;
        for (int mask = 1; mask < 16; mask++) {
            if (Builder__RoomPreset(area->entry, mask) != NULL) {
                dead_end              = dead_end || Builder__ExitCount(mask) == 1;
                shapes[shape_count++] = mask;
            }
        }
        if (!dead_end && shape_count > 0) {
            const int mask = shapes[Rng_Below(rng, shape_count)];
            for (int dir = 0; dir < 4 && count < wanted; dir++) {
                if ((mask & dir_exit[dir]) != 0) {
                    rooms[count].gx     = rooms[0].gx + dir_dx[dir];
                    rooms[count].gy     = rooms[0].gy + dir_dy[dir];
                    rooms[count].exits  = dir_back[dir];
                    rooms[0].exits     |= dir_exit[dir];
                    count++;
                }
            }
        }
    }
    for (int attempt = 0; attempt < wanted * 200 && count < wanted; attempt++) {
        const int from = Rng_Below(rng, count);
        if (from == 0 && area->entry != NULL && rooms[0].exits != 0) {
            continue; // the way in is a dead end
        }
        const int dir = Rng_Below(rng, 4);
        const int gx  = rooms[from].gx + dir_dx[dir];
        const int gy  = rooms[from].gy + dir_dy[dir];
        if (gx < 0 || gy < 0 || gx >= grid_w || gy >= grid_h || Builder__RoomAt(rooms, count, gx, gy) >= 0) {
            continue;
        }
        if (Builder__ExitCount(rooms[from].exits) >= 3 && area->grid_w == 0 && Rng_Chance(rng, 70)) {
            continue; // fewer crossroads, longer corridors
        }
        rooms[count].gx     = gx;
        rooms[count].gy     = gy;
        rooms[count].exits  = dir_back[dir];
        rooms[from].exits  |= dir_exit[dir];
        count++;
    }
    return count;
}

// The Arcane Sanctuary, like the game's: a crossroads in the middle and a long walkway out on each side, winding
// as it goes, with short dead-end spurs off it (the Summoner waits at the end of the furthest one)
static int Builder__GrowArcane(Room *rooms, const int grid_w, const int grid_h, Rng *rng) {
    int count      = 1;
    rooms[0].gx    = grid_w / 2;
    rooms[0].gy    = grid_h / 2;
    rooms[0].exits = 0;
    for (int arm = 0; arm < 4; arm++) {
        int       from   = 0;
        int       dir    = arm; // N, S, E, W
        const int back   = arm == 0 ? 1 : (arm == 1 ? 0 : (arm == 2 ? 3 : 2));
        const int length = 6 + Rng_Below(rng, 3);
        int       path[12];
        int       steps = 0;
        for (int s = 0; s < length && count < MAX_ROOMS - 6; s++) {
            // On the way out: ahead mostly, sometimes to a side (never back towards the middle)
            int tries[3] = {arm, -1, -1};
            if (s > 0 && Rng_Chance(rng, 35)) {
                const int side_a = arm < 2 ? 2 : 0;
                const int side_b = arm < 2 ? 3 : 1;
                tries[0]         = Rng_Chance(rng, 50) ? side_a : side_b;
                tries[1]         = arm;
            }
            bool placed = false;
            for (int t = 0; t < 3 && !placed && tries[t] >= 0; t++) {
                dir = tries[t];
                if (dir == back) {
                    continue;
                }
                const int gx = rooms[from].gx + dir_dx[dir];
                const int gy = rooms[from].gy + dir_dy[dir];
                if (gx < 1 || gy < 1 || gx >= grid_w - 1 || gy >= grid_h - 1 ||
                    Builder__RoomAt(rooms, count, gx, gy) >= 0) {
                    continue;
                }
                rooms[count].gx     = gx;
                rooms[count].gy     = gy;
                rooms[count].exits  = dir_back[dir];
                rooms[from].exits  |= dir_exit[dir];
                from                = count++;
                path[steps++]       = from;
                placed              = true;
            }
            if (!placed) {
                break;
            }
        }
        // A spur or two: a room or two off the walkway to a side, ending there
        for (int spur = 0; spur < 2 && steps > 2 && count < MAX_ROOMS - 2; spur++) {
            const int at   = path[1 + Rng_Below(rng, steps - 2)];
            const int side = arm < 2 ? (Rng_Chance(rng, 50) ? 2 : 3) : (Rng_Chance(rng, 50) ? 0 : 1);
            int       on   = at;
            for (int k = 0; k < 1 + Rng_Below(rng, 2) && count < MAX_ROOMS; k++) {
                const int gx = rooms[on].gx + dir_dx[side];
                const int gy = rooms[on].gy + dir_dy[side];
                if (gx < 1 || gy < 1 || gx >= grid_w - 1 || gy >= grid_h - 1 ||
                    Builder__RoomAt(rooms, count, gx, gy) >= 0) {
                    break;
                }
                rooms[count].gx     = gx;
                rooms[count].gy     = gy;
                rooms[count].exits  = dir_back[side];
                rooms[on].exits    |= dir_exit[side];
                on                  = count++;
            }
        }
    }
    return count;
}

static float builder_exit_x = -1.0f; // sub-tiles: the middle of the last room of the dungeon just built
static float builder_exit_y = -1.0f;

static Map *Builder__BuildMaze(const MazeArea *maze, const LevelInfo *level, Rng *rng) {
    // Of Tal Rasha's seven tombs only the true one ends at the chamber with Duriel's way down; the rest at a chest
    MazeArea  copy = *maze;
    MazeArea *area = &copy;
    if (level->id >= 66 && level->id <= 72) {
        area->exit    = level->id == World_TrueTomb() ? "Act 2 - Tomb Talrasha" : "Act 2 - Tomb Chest";
        area->special = level->id == World_TrueTomb() ? "Act 2 - Tomb Chest" : NULL;
    }
    const Preset *base = Builder__RoomPreset(area->family, EXIT_N | EXIT_S | EXIT_E | EXIT_W);
    if (base == NULL) {
        base = Builder__RoomPreset(area->family, EXIT_N | EXIT_S);
    }
    if (base == NULL || base->size_x <= 0 || base->size_y <= 0) {
        return NULL;
    }
    const int room_w = base->size_x;
    const int room_h = base->size_y;
    int       grid_w = area->grid_w > 0 ? area->grid_w : (MAP_MAX_SIZE - 1) / room_w;
    int       grid_h = area->grid_h > 0 ? area->grid_h : (MAP_MAX_SIZE - 1) / room_h;
    grid_w           = grid_w > MAX_GRID ? MAX_GRID : grid_w;
    grid_h           = grid_h > MAX_GRID ? MAX_GRID : grid_h;
    int wanted       = level->maze_rooms > area->rooms ? level->maze_rooms : area->rooms;
    wanted           = wanted > MAX_ROOMS ? MAX_ROOMS : wanted;
    if (area->grid_w > 0) {
        wanted = grid_w * grid_h;
    }

    Room rooms[MAX_ROOMS];
    int  count   = 0;
    int  exit_at = -1;
    int  special = -1;
    for (int attempt = 0; attempt < 40; attempt++) {
        if (level->id == 74) {
            count  = Builder__GrowArcane(rooms, grid_w, grid_h, rng);
            wanted = count;
        } else {
            count = Builder__GrowRooms(area, rooms, wanted, grid_w, grid_h, rng);
        }
        if (area->grid_w > 0) {
            // The jungle: every block used, with a few extra paths between neighbours
            for (int i = 0; i < count; i++) {
                for (int dir = 0; dir < 4; dir++) {
                    const int j = Builder__RoomAt(rooms, count, rooms[i].gx + dir_dx[dir], rooms[i].gy + dir_dy[dir]);
                    if (j >= 0 && (rooms[i].exits & dir_exit[dir]) == 0 && Rng_Chance(rng, 25)) {
                        rooms[i].exits |= dir_exit[dir];
                        rooms[j].exits |= dir_back[dir];
                    }
                }
            }
        }

        // The way on: the dead end furthest from the way in
        int distance[MAX_ROOMS];
        int queue[MAX_ROOMS];
        for (int i = 0; i < count; i++) {
            distance[i] = -1;
        }
        int head = 0, tail = 0;
        queue[tail++] = 0;
        distance[0]   = 0;
        while (head < tail) {
            const int r = queue[head++];
            for (int dir = 0; dir < 4; dir++) {
                if ((rooms[r].exits & dir_exit[dir]) == 0) {
                    continue;
                }
                const int n = Builder__RoomAt(rooms, count, rooms[r].gx + dir_dx[dir], rooms[r].gy + dir_dy[dir]);
                if (n >= 0 && distance[n] < 0) {
                    distance[n]   = distance[r] + 1;
                    queue[tail++] = n;
                }
            }
        }
        exit_at = -1;
        special = -1;
        for (int i = 1; i < count; i++) {
            if (Builder__ExitCount(rooms[i].exits) == 1 && (exit_at < 0 || distance[i] > distance[exit_at])) {
                exit_at = i;
            }
        }
        for (int i = 1; i < count && area->special != NULL; i++) {
            if (i != exit_at && Builder__ExitCount(rooms[i].exits) == 1 && (special < 0 || Rng_Chance(rng, 50))) {
                special = i;
            }
        }

        // Every room needs a preset with exactly its exits
        bool ok = count >= (wanted > 2 ? wanted - 1 : wanted);
        for (int i = 0; i < count && ok; i++) {
            ok = Builder__RoomPreset(area->family, rooms[i].exits) != NULL;
        }
        if (ok) {
            break;
        }
    }

    // The jungle runs on into the next area: a room on that side gets a way out through the edge
    int       walk_targets[4];
    WorldSide walk_sides[4];
    const int walk_count = area->grid_w > 0 ? World_GetWalkLinks(level->id, walk_targets, walk_sides, 4) : 0;
    int       walk_room[4];
    for (int l = 0; l < walk_count; l++) {
        walk_room[l]  = -1;
        const int dir = walk_sides[l] == WORLD_SIDE_N   ? 0
                        : walk_sides[l] == WORLD_SIDE_S ? 1
                        : walk_sides[l] == WORLD_SIDE_E ? 2
                                                        : 3;
        int       options[MAX_ROOMS];
        int       option_count = 0;
        for (int i = 0; i < count; i++) {
            const bool edge = (dir == 0 && rooms[i].gy == 0) || (dir == 1 && rooms[i].gy == grid_h - 1) ||
                              (dir == 2 && rooms[i].gx == grid_w - 1) || (dir == 3 && rooms[i].gx == 0);
            if (edge && i != exit_at && i != special && (rooms[i].exits & dir_exit[dir]) == 0 &&
                Builder__RoomPreset(area->family, rooms[i].exits | dir_exit[dir]) != NULL) {
                options[option_count++] = i;
            }
        }
        if (option_count > 0) {
            walk_room[l]               = options[Rng_Below(rng, option_count)];
            rooms[walk_room[l]].exits |= dir_exit[dir];
        }
    }

    // Where the rooms sit: shift the used part of the grid to the top-left
    int min_x = MAX_GRID, min_y = MAX_GRID, max_x = 0, max_y = 0;
    for (int i = 0; i < count; i++) {
        min_x = rooms[i].gx < min_x ? rooms[i].gx : min_x;
        min_y = rooms[i].gy < min_y ? rooms[i].gy : min_y;
        max_x = rooms[i].gx > max_x ? rooms[i].gx : max_x;
        max_y = rooms[i].gy > max_y ? rooms[i].gy : max_y;
    }
    const int width  = ((max_x - min_x + 1) * room_w) + 1;
    const int height = ((max_y - min_y + 1) * room_h) + 1;
    Map      *map    = Map_CreateEmpty(width, height, level->act);
    snprintf(map->name, sizeof(map->name), "%s", level->name);

    // The entry room's stairs lead back up (the first area Levels.txt lists), the last room's on down
    int       warp_targets[8];
    const int warp_count = Builder__WarpTargets(level->id, warp_targets, 8);

    for (int i = 0; i < count; i++) {
        const Preset *preset = NULL;
        if (i == 0 && area->entry != NULL) {
            preset = Builder__RoomPreset(area->entry, rooms[i].exits);
        } else if (i == exit_at) {
            preset = Builder__RoomPreset(area->exit, rooms[i].exits);
        } else if (i == special) {
            preset = Builder__RoomPreset(area->special, rooms[i].exits);
        }
        if (preset == NULL && Rng_Chance(rng, 20)) {
            char theme[96];
            snprintf(theme, sizeof(theme), "%s Theme", area->family);
            preset = Builder__RoomPreset(theme, rooms[i].exits);
        }
        if (preset == NULL) {
            preset = Builder__RoomPreset(area->family, rooms[i].exits);
        }
        const DS1 *ds1 = Builder__PickFile(rng, preset);
        if (ds1 == NULL) {
            continue;
        }
        const int tx = (rooms[i].gx - min_x) * room_w;
        const int ty = (rooms[i].gy - min_y) * room_h;
        // Rooms share their edge row; later rooms must not wipe a neighbour's walls with empty cells
        Map_StampDS1(map, ds1, tx, ty, MAP_STAMP_FILLED_ONLY);
        const int target = (i == 0 && warp_count > 0)         ? warp_targets[0]
                           : (i == exit_at && warp_count > 1) ? warp_targets[1]
                                                              : 0;
        if (target > 0) {
            int       wx[2];
            int       wy[2];
            int       slots[2];
            const int warps = Builder__FindWarps(ds1, wx, wy, slots, 2);
            if (warps > 0) {
                // The tile's own slot wins when Levels.txt fills it in (stairs to a side cave, ...)
                const int vis = World_GetVis(level->id, slots[0]);
                Builder__AddWarpExit(map, vis > 0 ? vis : target, tx + wx[0], ty + wy[0]);
            }
        }
        if (i == 0) {
            map->has_start = true;
            map->start_x   = (float)((tx * MAP_SUBTILES) + ((room_w * MAP_SUBTILES) / 2));
            map->start_y   = (float)((ty * MAP_SUBTILES) + ((room_h * MAP_SUBTILES) / 2));
        }
    }
    for (int l = 0; l < walk_count; l++) {
        if (walk_room[l] < 0) {
            continue;
        }
        // Walking into the outer strip of that room's open side takes you on; you arrive a little inside it
        const int   x0    = (rooms[walk_room[l]].gx - min_x) * room_w * MAP_SUBTILES;
        const int   y0    = (rooms[walk_room[l]].gy - min_y) * room_h * MAP_SUBTILES;
        const int   sw    = room_w * MAP_SUBTILES;
        const int   sh    = room_h * MAP_SUBTILES;
        const int   sub_w = map->width * MAP_SUBTILES;
        const int   sub_h = map->height * MAP_SUBTILES;
        const float mid_x = (float)x0 + ((float)sw / 2.0f);
        const float mid_y = (float)y0 + ((float)sh / 2.0f);
        switch (walk_sides[l]) {
        case WORLD_SIDE_N:
            Map_AddExit(map, walk_targets[l], x0, 0, x0 + sw, 2, mid_x, 14.0f);
            break;
        case WORLD_SIDE_S:
            Map_AddExit(map, walk_targets[l], x0, sub_h - 3, x0 + sw, sub_h - 1, mid_x, (float)sub_h - 14.0f);
            break;
        case WORLD_SIDE_W:
            Map_AddExit(map, walk_targets[l], 0, y0, 2, y0 + sh, 14.0f, mid_y);
            break;
        default:
            Map_AddExit(map, walk_targets[l], sub_w - 3, y0, sub_w - 1, y0 + sh, (float)sub_w - 14.0f, mid_y);
            break;
        }
    }
    Map_Finish(map);

    // Where the red portal on goes (the Summoner's room, the true tomb's chamber), once the start is settled
    builder_exit_x =
        exit_at >= 0 ? (float)((((rooms[exit_at].gx - min_x) * room_w) * MAP_SUBTILES) + ((room_w * MAP_SUBTILES) / 2))
                     : -1.0f;
    builder_exit_y =
        exit_at >= 0 ? (float)((((rooms[exit_at].gy - min_y) * room_h) * MAP_SUBTILES) + ((room_h * MAP_SUBTILES) / 2))
                     : -1.0f;
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
// The Bloody Foothills: siege strips side by side, from the barricade in the west to Harrogath's gate in the east (the
// strips are drawn to join up in that order: each one's west edge meets the next one's east edge)

static bool Builder__HasFloor(const DS1 *ds1, const int x, const int y) {
    if (x < 0 || y < 0 || x >= ds1->width || y >= ds1->height) {
        return false;
    }
    for (int f = 0; f < ds1->floor_layer_count; f++) {
        if (ds1->floors[f][(y * ds1->width) + x].prop1 != 0) {
            return true;
        }
    }
    return false;
}

// How far down the next strip has to sit for its ground to carry on from this one's
static int Builder__StripShift(const DS1 *left, const DS1 *right) {
    int best_shift = 0;
    int best_score = -1000000;
    for (int shift = -24; shift <= 24; shift++) {
        int score = 0;
        for (int y = 0; y < left->height; y++) {
            const bool a  = Builder__HasFloor(left, left->width - 1, y);
            const bool b  = Builder__HasFloor(right, 0, y - shift);
            score        += (a && b) ? 2 : (a != b ? -1 : 0);
        }
        score -= abs(shift) / 4; // prefer the straight line when it's a tie
        if (score > best_score) {
            best_score = score;
            best_shift = shift;
        }
    }
    return best_shift;
}

static Map *Builder__BuildStrips(const LevelInfo *level, Rng *rng) {
    const Preset *first = Builder__FindPreset("Act 5 - Siege To Town");
    const Preset *last  = Builder__FindPreset("Act 5 - Siege To Barricade");
    if (first == NULL || last == NULL || first->size_x <= 0) {
        return NULL;
    }
    const int  strip = first->size_x;
    int        count = level->size_x / strip;
    const DS1 *strips[32];
    int        offsets[32];
    count = count > 32 ? 32 : count;
    for (int i = 0; i < count; i++) {
        const Preset *preset = i == 0 ? first : (i == count - 1 ? last : NULL);
        if (preset == NULL) {
            char name[64];
            snprintf(name, sizeof(name), "Act 5 - Siege Strip %d", i); // they follow on from each other
            preset = Builder__FindPreset(name);
        }
        strips[i] = Builder__PickFile(rng, preset);
        if (strips[i] == NULL) {
            return NULL;
        }
        // Strip i sits west of strip i - 1
        offsets[i] = i == 0 ? 0 : offsets[i - 1] - Builder__StripShift(strips[i], strips[i - 1]);
    }
    int low  = 0;
    int high = 0;
    for (int i = 0; i < count; i++) {
        low  = offsets[i] < low ? offsets[i] : low;
        high = offsets[i] > high ? offsets[i] : high;
    }
    const int height = first->size_y + (high - low) + 1;
    if (height > MAP_MAX_SIZE) {
        return NULL;
    }
    Map *map = Map_CreateEmpty((count * strip) + 1, height, level->act);
    snprintf(map->name, sizeof(map->name), "%s", level->name);
    for (int i = 0; i < count; i++) {
        Map_StampDS1(map, strips[i], (count - 1 - i) * strip, offsets[i] - low, i == 0 ? 0 : MAP_STAMP_FILLED_ONLY);
    }
    const int sub_w = map->width * MAP_SUBTILES;
    map->has_start  = true;
    map->start_x    = (float)sub_w - ((float)(strip * MAP_SUBTILES) / 2.0f);
    map->start_y    = (float)(((offsets[0] - low) * MAP_SUBTILES) + ((first->size_y * MAP_SUBTILES) / 2));

    // Harrogath's gate at the east end, the way on to the Highlands at the west end
    int       walk_targets[4];
    WorldSide walk_sides[4];
    const int walk_count = World_GetWalkLinks(level->id, walk_targets, walk_sides, 4);
    Map_Finish(map);
    for (int l = 0; l < walk_count; l++) {
        const bool west = walk_sides[l] == WORLD_SIDE_W;
        const int  oy   = (west ? offsets[count - 1] : offsets[0]) - low;
        int        y0   = oy * MAP_SUBTILES;
        int        y1   = (oy + first->size_y) * MAP_SUBTILES;
        // Just the stretch of the edge you can walk off (the road to Harrogath's gate), so the town lines up with it
        int open_low  = -1;
        int open_high = -1;
        for (int y = y0; y < y1; y++) {
            const int x = west ? 1 : sub_w - 2;
            if (Map_IsWalkable(map, x, y)) {
                open_low  = open_low < 0 ? y : open_low;
                open_high = y;
            }
        }
        if (open_low >= 0) {
            y0 = open_low;
            y1 = open_high;
        }
        Map_AddExit(map, walk_targets[l], west ? 0 : sub_w - 3, y0, west ? 2 : sub_w - 1, y1,
                    west ? (float)(strip * MAP_SUBTILES) / 2.0f : (float)sub_w - ((float)(strip * MAP_SUBTILES) / 2.0f),
                    (float)(y0 + y1) / 2.0f);
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
// Areas the game puts together from a fixed set of pieces in a fixed arrangement: the Harem and Palace Cellars (four
// quarters), the Spider caves, the Causeway, Travincal, the Chaos Sanctum (a cross with the way in below it),
// Nihlathak's halls (four quarters, one with the stairs down), the hells (a row of lava rooms)

static const DS1 *Builder__StampPiece(Map *map, Rng *rng, const char *name, const int tx, const int ty) {
    const Preset *preset = Builder__FindPreset(name);
    const DS1    *ds1    = preset != NULL ? Builder__PickFile(rng, preset) : NULL;
    if (ds1 == NULL) {
        LOG_WARN("%s: no piece '%s'", map->name, name);
        return NULL;
    }
    Map_StampDS1(map, ds1, tx, ty, MAP_STAMP_FILLED_ONLY);
    return ds1;
}

static bool Builder__IsComposite(const int level_id) {
    return (level_id >= 51 && level_id <= 54) || (level_id >= 82 && level_id <= 85) || level_id == 108 ||
           level_id == 122 || level_id == 123 || (level_id >= 125 && level_id <= 127);
}

static Map *Builder__BuildComposite(const LevelInfo *level, Rng *rng) {
    const int id      = level->id;
    Map      *map     = NULL;
    float     start_x = -1.0f;
    float     start_y = -1.0f;
    if (id == 51 || (id >= 52 && id <= 54) || id == 84 || id == 85) {
        // Four 16x16 quarters
        const char *family  = id == 51 ? "Act 2 - Corrupt Harem" : id <= 54 ? "Act 2 - Basement" : "Act 3 - Spider";
        const char *quad[4] = {"NW", "NE", "SW", "SE"};
        map                 = Map_CreateEmpty(33, 33, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        for (int q = 0; q < 4; q++) {
            char name[64];
            // The spider caves: one quarter is the nest with the chest (Spider Cave's to the north-east)
            const bool chest = (id == 84 && q == 1) || (id == 85 && q == 0);
            snprintf(name, sizeof(name), "%s %s%s", family, chest ? "Chest " : "", quad[q]);
            // The quarters are named the other way round: the north-west one is at the far corner (its open sides
            // face the others), the south-east one at the near corner
            const int slot = 3 - q;
            Builder__StampPiece(map, rng, name, (slot % 2) * 16, (slot / 2) * 16);
        }
    } else if (id == 82) {
        map = Map_CreateEmpty(49, 17, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        Builder__StampPiece(map, rng, "Act 3 - Bridge", 0, 0);
    } else if (id == 83) {
        map = Map_CreateEmpty(65, 65, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        static const char *const names[6] = {"Act 3 - Travincal NW", "Act 3 - Travincal N ", "Act 3 - Travincal NE",
                                             "Act 3 - Travincal SW", "Act 3 - Travincal S ", "Act 3 - Travincal SE"};
        static const int         xs[3]    = {0, 16, 48};
        for (int p = 0; p < 6; p++) {
            Builder__StampPiece(map, rng, names[p], xs[p % 3], (p / 3) * 32);
        }
    } else if (id == 108) {
        // Diablo's heart in the middle, a wing on each side, the way in from the River of Flame below the south wing
        map = Map_CreateEmpty(73, 97, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        // (named the other way round, like the Harem's quarters: the north wing is at the far end)
        // (the wings are named the other way round from the Harem's quarters: north is towards the far end, and
        // east on the same side as the Harem's)
        Builder__StampPiece(map, rng, "Act 4 - Diablo Entry", 24, 0);
        Builder__StampPiece(map, rng, "Act 4 - Diablo Arm S", 24, 24);
        Builder__StampPiece(map, rng, "Act 4 - Diablo Heart", 24, 48);
        Builder__StampPiece(map, rng, "Act 4 - Diablo Arm W", 0, 48);
        Builder__StampPiece(map, rng, "Act 4 - Diablo Arm E", 48, 48);
        Builder__StampPiece(map, rng, "Act 4 - Diablo Arm N", 24, 72);
        start_x = (float)(36 * MAP_SUBTILES);
        start_y = (float)(12 * MAP_SUBTILES);
        Map_Finish(map);
        // (the north wing's doorway is closed off by the flags of the walls either side of it)
        Map_OpenThinWalls(map, (int)start_x + 2, (int)start_y + 2, 2);

    } else if (id == 122 || id == 123) {
        // Four 40x40 quarters: the stairs up in the south-east, the stairs down in one of the others, and the
        // waypoint (Halls of Death's Calling) in another
        map = Map_CreateEmpty(81, 81, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        const char *quad[3] = {"NW", "NE", "SW"};
        const int   down    = Rng_Below(rng, 3);
        const int   wp      = id == 123 ? (down + 1 + Rng_Below(rng, 2)) % 3 : -1;
        for (int q = 0; q < 3; q++) {
            char name[64];
            snprintf(name, sizeof(name), "Act 5 - Temple %s%s", quad[q],
                     q == down ? " Down"
                     : q == wp ? " Waypoint"
                               : "");
            const int slot = 3 - q; // the far corner is the north-west one, as with the Harem's
            Builder__StampPiece(map, rng, name, (slot % 2) * 40, (slot / 2) * 40);
        }
        Builder__StampPiece(map, rng, "Act 5 - Temple SE up", 0, 0);
        start_x = (float)(20 * MAP_SUBTILES);
        start_y = (float)(20 * MAP_SUBTILES);
    } else {
        // The hells: a row of lava rooms, east to west or north to south
        const bool across = Rng_Chance(rng, 50);
        const int  rooms  = 5 + Rng_Below(rng, 3);
        map               = Map_CreateEmpty(across ? (rooms * 24) + 1 : 25, across ? 25 : (rooms * 24) + 1, level->act);
        snprintf(map->name, sizeof(map->name), "%s", level->name);
        for (int r = 0; r < rooms; r++) {
            const char *shape = r == 0           ? (across ? "E" : "S")
                                : r == rooms - 1 ? (across ? "W" : "N")
                                                 : (across ? "EW" : "NS");
            char        name[64];
            snprintf(name, sizeof(name), "Act 5 - Lava %s", shape);
            Builder__StampPiece(map, rng, name, across ? r * 24 : 0, across ? 0 : r * 24);
        }
        start_x = (float)(12 * MAP_SUBTILES);
        start_y = (float)(12 * MAP_SUBTILES);
    }
    Map_Finish(map);
    if (start_x >= 0.0f) {
        map->has_start = true;
        map->start_x   = start_x;
        map->start_y   = start_y;
    }
    // Stairs and doors from the pieces' own entrance tiles; paths off the edge to the areas walked to
    World_AddFixedExits(map, id);
    return map;
}

static const OutdoorArea *Builder__Outdoor(const int level_id) {
    for (size_t i = 0; i < sizeof(outdoor_areas) / sizeof(outdoor_areas[0]); i++) {
        if (outdoor_areas[i].level_id == level_id) {
            return &outdoor_areas[i];
        }
    }
    return NULL;
}

static const MazeArea *Builder__Maze(const int level_id) {
    for (size_t i = 0; i < sizeof(maze_areas) / sizeof(maze_areas[0]); i++) {
        if (maze_areas[i].level_id == level_id) {
            return &maze_areas[i];
        }
    }
    return NULL;
}

bool LevelBuilder_CanBuild(const int level_id) {
    return level_id == STRIP_LEVEL_ID || Builder__Outdoor(level_id) != NULL || Builder__Maze(level_id) != NULL ||
           Builder__IsComposite(level_id);
}

Map *LevelBuilder_Build(const int level_id, uint32_t seed) {
    if (!LevelBuilder_CanBuild(level_id)) {
        return NULL;
    }
    Builder__LoadTables();
    LevelInfo level;
    if (!Builder__GetLevel(level_id, &level)) {
        return NULL;
    }
    if (seed == 0) {
        seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
    }
    Rng rng = {seed != 0 ? seed : 0x9E3779B9u};
    LOG_INFO("Building %s (seed %u)", level.name, seed);

    Map *map = NULL;
    if (level_id == STRIP_LEVEL_ID) {
        map = Builder__BuildStrips(&level, &rng);
    } else if (Builder__IsComposite(level_id)) {
        map = Builder__BuildComposite(&level, &rng);
    } else if (Builder__Outdoor(level_id) != NULL) {
        map = Builder__BuildOutdoor(Builder__Outdoor(level_id), &level, &rng);
    } else {
        map = Builder__BuildMaze(Builder__Maze(level_id), &level, &rng);
    }
    if (map != NULL && (level_id == STRIP_LEVEL_ID || Builder__Outdoor(level_id) != NULL)) {
        Map_FillSeams(map); // (outdoors: no black gaps between the pieces)
    }
    if (map != NULL && level_id == 74) {
        Map_JoinPieces(map, 4); // (the Arcane Sanctuary's walkways stop short of each other)
    }
    if (map != NULL && level_id == STRIP_LEVEL_ID) {
        Map_FillGaps(map, 24); // (the siege strips' ragged top and bottom edges)
    }
    if (map != NULL) {
        uint8_t *main_area = Builder__MainArea(map);
        Builder__Populate(map, &level, main_area, &rng);
        free(main_area);

        // A dungeon's red portal on, in its last room (or as near as you can walk to it)
        int       portal_targets[4];
        const int portals = Builder__Maze(level_id) != NULL && builder_exit_x >= 0.0f
                                ? World_GetForwardPortals(level_id, portal_targets, 4)
                                : 0;
        for (int p = 0; p < portals; p++) {
            float x = builder_exit_x + (float)(p * 6);
            float y = builder_exit_y;
            World_NearestReachable(map, x, y, &x, &y);
            World_AddPortal(map, portal_targets[p], x, y);
        }
    }
    return map;
}

void LevelBuilder_Release(void) {
    for (int i = 0; i < ds1_cache_count; i++) {
        DS1_Destroy(&ds1_cache[i].ds1);
    }
    ds1_cache_count = 0;
    free(presets);
    presets      = NULL;
    preset_count = -1;
    free(sub_files);
    sub_files = NULL;
    sub_count = 0;
    DataTable_Destroy(&levels_table);
    DataTable_Destroy(&maze_table);
    DataTable_Destroy(&monstats_table);
}
