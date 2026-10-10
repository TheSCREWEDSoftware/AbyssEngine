#include "CatalogCharacters.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "../drawing/Button.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../managers/InputManager.h"
#include "CatalogTheme.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHARACTERS_MAX_MODES     20
#define CHARACTERS_STAT_LINES    3
#define CHARACTERS_TEXT_LEN      96
#define CHARACTERS_MAX_SCALE     2.0f
#define CHARACTERS_PALETTE       PALETTE_ACT_1
#define CHARACTERS_CATEGORY_H    20
#define CHARACTERS_DEFAULT_SPEED 2 // index into character_speeds: 0.75x

// Playback speed only changes how often frames advance, not the screen refresh rate.
static const float character_speeds[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
#define CHARACTER_SPEED_COUNT (int)(sizeof(character_speeds) / sizeof(character_speeds[0]))
static int characters_speed = CHARACTERS_DEFAULT_SPEED; // kept while browsing

// Detail panel layout, relative to the panel
#define CHAR_NAME_Y     16
#define CHAR_PREVIEW_Y  32
#define CHAR_PREVIEW_H  240
#define CHAR_INFO_Y     284
#define CHAR_INFO_LINE  16
#define CHAR_BUTTONS_Y  342
#define CHAR_BUTTONS_Y2 378

typedef enum CharacterCategory {
    CHARACTER_CATEGORY_PLAYERS,
    CHARACTER_CATEGORY_NPCS,
    CHARACTER_CATEGORY_ENEMIES,
    CHARACTER_CATEGORY_BOSSES,
    CHARACTER_CATEGORY_OTHERS,
    CHARACTER_CATEGORY_COUNT
} CharacterCategory;

static const char *category_names[CHARACTER_CATEGORY_COUNT] = {"Players", "NPCs", "Enemies", "Bosses", "Others"};

typedef struct AnimationMode {
    const char *code;
    const char *name;
    const char *monstats2_flag; // "mNU" etc., NULL for modes not listed there
} AnimationMode;

static const AnimationMode monster_modes[] = {
    {"nu", "Neutral",   "mNU"},
    {"wl", "Walk",      "mWL"},
    {"rn", "Run",       "mRN"},
    {"a1", "Attack 1",  "mA1"},
    {"a2", "Attack 2",  "mA2"},
    {"sc", "Cast",      "mSC"},
    {"s1", "Skill 1",   "mS1"},
    {"s2", "Skill 2",   "mS2"},
    {"s3", "Skill 3",   "mS3"},
    {"s4", "Skill 4",   "mS4"},
    {"gh", "Get Hit",   "mGH"},
    {"bl", "Block",     "mBL"},
    {"kb", "Knockback", "mKB"},
    {"dt", "Death",     "mDT"},
    {"dd", "Dead",      "mDD"},
    {"sq", "Sequence",  "mSQ"},
};

static const AnimationMode player_modes[] = {
    {"nu", "Neutral",      NULL},
    {"tn", "Town Neutral", NULL},
    {"wl", "Walk",         NULL},
    {"tw", "Town Walk",    NULL},
    {"rn", "Run",          NULL},
    {"a1", "Attack 1",     NULL},
    {"a2", "Attack 2",     NULL},
    {"sc", "Cast",         NULL},
    {"th", "Throw",        NULL},
    {"kk", "Kick",         NULL},
    {"s1", "Skill 1",      NULL},
    {"s2", "Skill 2",      NULL},
    {"s3", "Skill 3",      NULL},
    {"s4", "Skill 4",      NULL},
    {"gh", "Get Hit",      NULL},
    {"bl", "Block",        NULL},
    {"kb", "Knockback",    NULL},
    {"dt", "Death",        NULL},
    {"dd", "Dead",         NULL},
    {"sq", "Sequence",     NULL},
};

static const AnimationMode object_modes[] = {
    {"nu", "Neutral",   "Mode0"},
    {"op", "Operating", "Mode1"},
    {"on", "Opened",    "Mode2"},
    {"s1", "Special 1", "Mode3"},
    {"s2", "Special 2", "Mode4"},
    {"s3", "Special 3", "Mode5"},
    {"s4", "Special 4", "Mode6"},
    {"s5", "Special 5", "Mode7"},
};

#define MODE_COUNT(modes) (int)(sizeof(modes) / sizeof((modes)[0]))

// Game direction index for each step of a full turn (starting facing the viewer, turning clockwise on screen).
static const int turn_order_8[8]   = {4, 0, 5, 1, 6, 2, 7, 3};
static const int turn_order_16[16] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};

typedef struct CharacterEntry {
    char                 name[64];
    char                 id[40]; // internal id (monstats Id / object token), used to tell equal names apart
    CharacterCategory    category;
    FigureSpec           spec;
    const AnimationMode *modes[CHARACTERS_MAX_MODES];
    int                  mode_count;
    char                 stats[CHARACTERS_STAT_LINES][CHARACTERS_TEXT_LEN];
} CharacterEntry;

struct CatalogCharacters {
    CharacterEntry *entries;
    int             entry_count;
    int            *visible; // entries of the current category
    int             visible_count;
    int             category_counts[CHARACTER_CATEGORY_COUNT];
    int             category;
    int             selected; // entry index, -1 for none

    Figure  *figure;
    int      mode;
    int      turn; // position in the turn order
    int      frame;
    bool     playing;
    uint64_t last_tick;

    Label  *name_label;
    Label  *info_labels[1 + CHARACTERS_STAT_LINES];
    Label  *category_labels[CHARACTER_CATEGORY_COUNT];
    Label  *empty_label;
    Button *turn_left;
    Button *turn_right;
    Button *play;
    Button *pause;
    Button *prev_mode;
    Button *next_mode;
    Button *speed;
    bool    prev_mouse_left;
};

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

static void Characters__Lower(char *out, const size_t out_len, const char *in) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < out_len; i++) {
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[i] = '\0';
}

static void Characters__Name(char *out, const size_t out_len, const char *key, const char *fallback) {
    const char *value = (key != NULL && key[0] != '\0') ? StringTable_Find(key) : NULL;
    if (value == NULL || value[0] == '\0') {
        value = fallback;
    }

    size_t o = 0;
    for (const unsigned char *ch = (const unsigned char *)value; *ch != '\0' && o + 1 < out_len; ch++) {
        if (*ch == 0xFF && ch[1] == 'c' && ch[2] != '\0') { // colour code
            ch += 2;
            continue;
        }
        if (*ch >= 0x20 && *ch < 0x7F) {
            out[o++] = (char)*ch;
        }
    }
    out[o] = '\0';
}

static CharacterEntry *Characters__Add(CatalogCharacters *characters, int *capacity) {
    if (characters->entry_count == *capacity) {
        *capacity           *= 2;
        characters->entries  = realloc(characters->entries, sizeof(CharacterEntry) * (size_t)*capacity);
        FAIL_IF_NULL(characters->entries);
    }

    CharacterEntry *entry = &characters->entries[characters->entry_count];
    memset(entry, 0, sizeof(CharacterEntry));
    return entry;
}

// Keeps the entry only if at least one of its animations has graphics.
static void Characters__Commit(CatalogCharacters *characters, CharacterEntry *entry, const AnimationMode *modes,
                               const int mode_count, const bool *mode_flags) {
    for (int m = 0; m < mode_count && entry->mode_count < CHARACTERS_MAX_MODES; m++) {
        if (mode_flags != NULL && !mode_flags[m]) {
            continue;
        }
        if (Figure_ModeExists(&entry->spec, modes[m].code)) {
            entry->modes[entry->mode_count++] = &modes[m];
        }
    }

    if (entry->mode_count > 0) {
        characters->category_counts[entry->category]++;
        characters->entry_count++;
    }
}

static void Characters__AddStat(CharacterEntry *entry, const int line, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(entry->stats[line], CHARACTERS_TEXT_LEN, format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

static void Characters__LoadPlayers(CatalogCharacters *characters, int *capacity) {
    static const struct {
        const char *name;
        const char *token;
    } players[] = {
        {"Amazon",      "am"},
        {"Sorceress",   "so"},
        {"Necromancer", "ne"},
        {"Paladin",     "pa"},
        {"Barbarian",   "ba"},
        {"Druid",       "dz"},
        {"Assassin",    "ai"}
    };

    for (size_t p = 0; p < sizeof(players) / sizeof(players[0]); p++) {
        CharacterEntry *entry = Characters__Add(characters, capacity);
        entry->category       = CHARACTER_CATEGORY_PLAYERS;
        Characters__Name(entry->name, sizeof(entry->name), players[p].name, players[p].name);

        entry->spec.base = "chars";
        snprintf(entry->spec.token, sizeof(entry->spec.token), "%s", players[p].token);
        snprintf(entry->spec.weapon_class, sizeof(entry->spec.weapon_class), "hth");
        // Light armour everywhere, no weapon or shield
        for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
            if (c != COF_COMPONENT_RH && c != COF_COMPONENT_LH && c != COF_COMPONENT_SH) {
                snprintf(entry->spec.armor[c], sizeof(entry->spec.armor[c]), "lit");
            }
        }

        Characters__AddStat(entry, 0, "Player character");
        Characters__AddStat(entry, 1, "Light armor, no weapon");
        Characters__Commit(characters, entry, player_modes, MODE_COUNT(player_modes), NULL);
    }
}

static void Characters__LoadMonsters(CatalogCharacters *characters, int *capacity) {
    DataTable *stats = DataTable_Load(EXCEL_MONSTATS);
    DataTable *gfx   = DataTable_Load(EXCEL_MONSTATS2);

#define COL(t, n) DataTable_GetColumnIndex(t, n)
    const int c_id = COL(stats, "Id"), c_name = COL(stats, "NameStr"), c_ex = COL(stats, "MonStatsEx"),
              c_code = COL(stats, "Code"), c_enabled = COL(stats, "enabled"), c_npc = COL(stats, "npc"),
              c_boss = COL(stats, "boss"), c_prime = COL(stats, "primeevil"), c_level = COL(stats, "Level"),
              c_level_n = COL(stats, "Level(N)"), c_level_h = COL(stats, "Level(H)"), c_min_hp = COL(stats, "minHP"),
              c_max_hp = COL(stats, "maxHP"), c_exp = COL(stats, "Exp"), c_type = COL(stats, "MonType");
    const int g_id = COL(gfx, "Id"), g_base_w = COL(gfx, "BaseW");
#undef COL

    static const char *present_columns[COF_COMPONENT_COUNT] = {"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                                                               "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
    static const char *variant_columns[COF_COMPONENT_COUNT] = {"HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
                                                               "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v"};
    int                g_present[COF_COMPONENT_COUNT];
    int                g_variant[COF_COMPONENT_COUNT];
    for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
        g_present[c] = DataTable_GetColumnIndex(gfx, present_columns[c]);
        g_variant[c] = DataTable_GetColumnIndex(gfx, variant_columns[c]);
    }
    int g_mode[MODE_COUNT(monster_modes)];
    for (int m = 0; m < MODE_COUNT(monster_modes); m++) {
        g_mode[m] = DataTable_GetColumnIndex(gfx, monster_modes[m].monstats2_flag);
    }

    const int gfx_rows = DataTable_GetRowCount(gfx);

    for (int row = 0; row < DataTable_GetRowCount(stats); row++) {
        if (DataTable_GetInt(stats, row, c_enabled) != 1 || !DataTable_HasValue(stats, row, c_code)) {
            continue;
        }

        // Graphics row in monstats2.txt
        const char *ex      = DataTable_GetString(stats, row, c_ex);
        int         gfx_row = -1;
        for (int g = 0; g < gfx_rows; g++) {
            if (strcmp(DataTable_GetString(gfx, g, g_id), ex) == 0) {
                gfx_row = g;
                break;
            }
        }
        if (gfx_row < 0) {
            continue;
        }

        CharacterEntry *entry = Characters__Add(characters, capacity);
        if (DataTable_GetInt(stats, row, c_boss) == 1 || DataTable_GetInt(stats, row, c_prime) == 1) {
            entry->category = CHARACTER_CATEGORY_BOSSES;
        } else if (DataTable_GetInt(stats, row, c_npc) == 1) {
            entry->category = CHARACTER_CATEGORY_NPCS;
        } else {
            entry->category = CHARACTER_CATEGORY_ENEMIES;
        }

        Characters__Name(entry->name, sizeof(entry->name), DataTable_GetString(stats, row, c_name),
                         DataTable_GetString(stats, row, c_id));
        snprintf(entry->id, sizeof(entry->id), "%s", DataTable_GetString(stats, row, c_id));

        entry->spec.base = "monsters";
        Characters__Lower(entry->spec.token, sizeof(entry->spec.token), DataTable_GetString(stats, row, c_code));
        Characters__Lower(entry->spec.weapon_class, sizeof(entry->spec.weapon_class),
                          DataTable_HasValue(gfx, gfx_row, g_base_w) ? DataTable_GetString(gfx, gfx_row, g_base_w)
                                                                     : "hth");

        // Each present layer uses the first armour variant listed (skipping "nil" = nothing)
        for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
            if (DataTable_GetInt(gfx, gfx_row, g_present[c]) != 1) {
                continue;
            }
            char variants[64];
            snprintf(variants, sizeof(variants), "%s", DataTable_GetString(gfx, gfx_row, g_variant[c]));
            for (char *variant = strtok(variants, ","); variant != NULL; variant = strtok(NULL, ",")) {
                while (*variant == ' ') {
                    variant++;
                }
                if (strcmp(variant, "nil") != 0 && variant[0] != '\0') {
                    Characters__Lower(entry->spec.armor[c], sizeof(entry->spec.armor[c]), variant);
                    break;
                }
            }
        }

        const int level   = DataTable_GetInt(stats, row, c_level);
        const int level_n = DataTable_GetInt(stats, row, c_level_n);
        const int level_h = DataTable_GetInt(stats, row, c_level_h);
        const int max_hp  = DataTable_GetInt(stats, row, c_max_hp);
        if (level == 0 && max_hp == 0) {
            Characters__AddStat(entry, 0, entry->category == CHARACTER_CATEGORY_NPCS ? "Town NPC" : "Not attackable");
        } else if (level_n > 0 || level_h > 0) {
            Characters__AddStat(entry, 0, "Level %d / %d / %d (Normal / Nightmare / Hell)", level, level_n, level_h);
        } else {
            Characters__AddStat(entry, 0, "Level %d", level);
        }
        if (max_hp > 0) {
            Characters__AddStat(entry, 1, "Life %d-%d   Experience %d", DataTable_GetInt(stats, row, c_min_hp), max_hp,
                                DataTable_GetInt(stats, row, c_exp));
        }
        if (DataTable_HasValue(stats, row, c_type)) {
            Characters__AddStat(entry, 2, "Type: %s", DataTable_GetString(stats, row, c_type));
        }

        bool flags[MODE_COUNT(monster_modes)];
        for (int m = 0; m < MODE_COUNT(monster_modes); m++) {
            flags[m] = g_mode[m] < 0 || DataTable_GetInt(gfx, gfx_row, g_mode[m]) == 1;
        }
        Characters__Commit(characters, entry, monster_modes, MODE_COUNT(monster_modes), flags);
    }

    DataTable_Destroy(&gfx);
    DataTable_Destroy(&stats);
}

static void Characters__LoadObjects(CatalogCharacters *characters, int *capacity) {
    DataTable *objects = DataTable_Load(EXCEL_OBJECTS);
    const int  c_name  = DataTable_GetColumnIndex(objects, "Name");
    const int  c_desc  = DataTable_GetColumnIndex(objects, "description - not loaded");
    const int  c_token = DataTable_GetColumnIndex(objects, "Token");

    static const char *present_columns[COF_COMPONENT_COUNT] = {"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                                                               "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
    int                c_present[COF_COMPONENT_COUNT];
    for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
        c_present[c] = DataTable_GetColumnIndex(objects, present_columns[c]);
    }
    int c_mode[MODE_COUNT(object_modes)];
    for (int m = 0; m < MODE_COUNT(object_modes); m++) {
        c_mode[m] = DataTable_GetColumnIndex(objects, object_modes[m].monstats2_flag);
    }

    const int first_object = characters->entry_count;

    for (int row = 0; row < DataTable_GetRowCount(objects); row++) {
        char token[8];
        Characters__Lower(token, sizeof(token), DataTable_GetString(objects, row, c_token));
        if (token[0] == '\0') {
            continue;
        }

        // Many object rows share the same graphics; list each token once
        bool seen = false;
        for (int e = first_object; e < characters->entry_count && !seen; e++) {
            seen = strcmp(characters->entries[e].spec.token, token) == 0;
        }
        if (seen) {
            continue;
        }

        CharacterEntry *entry = Characters__Add(characters, capacity);
        entry->category       = CHARACTER_CATEGORY_OTHERS;
        const char *desc      = DataTable_GetString(objects, row, c_desc);
        Characters__Name(entry->name, sizeof(entry->name), DataTable_GetString(objects, row, c_name),
                         desc[0] != '\0' ? desc : token);
        if (desc[0] != '\0' && strcmp(entry->name, desc) != 0) {
            // Object names repeat a lot ("chest"); the description tells them apart
            Characters__Name(entry->name, sizeof(entry->name), NULL, desc);
        }

        snprintf(entry->id, sizeof(entry->id), "%s", token);
        entry->spec.base = "objects";
        snprintf(entry->spec.token, sizeof(entry->spec.token), "%s", token);
        snprintf(entry->spec.weapon_class, sizeof(entry->spec.weapon_class), "hth");
        for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
            if (DataTable_GetInt(objects, row, c_present[c]) == 1) {
                snprintf(entry->spec.armor[c], sizeof(entry->spec.armor[c]), "lit");
            }
        }

        Characters__AddStat(entry, 0, "Object (token %s)", token);

        bool flags[MODE_COUNT(object_modes)];
        for (int m = 0; m < MODE_COUNT(object_modes); m++) {
            flags[m] = c_mode[m] < 0 || DataTable_GetInt(objects, row, c_mode[m]) == 1;
        }
        Characters__Commit(characters, entry, object_modes, MODE_COUNT(object_modes), flags);
    }

    DataTable_Destroy(&objects);
}

// Several monsters share a name (Diablo, his clone and Uber Diablo): add the internal id to tell them apart.
static void Characters__DisambiguateNames(CatalogCharacters *characters) {
    // Decide first, rename afterwards, so every member of a group of equal names gets its id
    bool *duplicate = calloc((size_t)characters->entry_count + 1, sizeof(bool));
    FAIL_IF_NULL(duplicate);
    for (int a = 0; a < characters->entry_count; a++) {
        for (int b = a + 1; b < characters->entry_count; b++) {
            if (characters->entries[a].category == characters->entries[b].category &&
                strcmp(characters->entries[a].name, characters->entries[b].name) == 0) {
                duplicate[a] = true;
                duplicate[b] = true;
            }
        }
    }

    for (int a = 0; a < characters->entry_count; a++) {
        CharacterEntry *entry = &characters->entries[a];
        if (duplicate[a] && entry->id[0] != '\0') {
            entry->name[sizeof(entry->name) - sizeof(entry->id) - 4] = '\0';
            strncat(entry->name, " (", sizeof(entry->name) - strlen(entry->name) - 1);
            strncat(entry->name, entry->id, sizeof(entry->name) - strlen(entry->name) - 1);
            strncat(entry->name, ")", sizeof(entry->name) - strlen(entry->name) - 1);
        }
    }
    free(duplicate);
}

static void Characters__BuildVisible(CatalogCharacters *characters) {
    characters->visible_count = 0;
    for (int e = 0; e < characters->entry_count; e++) {
        if ((int)characters->entries[e].category == characters->category) {
            characters->visible[characters->visible_count++] = e;
        }
    }
}

CatalogCharacters *CatalogCharacters_Create(void) {
    CatalogCharacters *characters = malloc(sizeof(CatalogCharacters));
    FAIL_IF_NULL(characters);
    memset(characters, 0, sizeof(CatalogCharacters));
    characters->selected = -1;
    characters->category = CHARACTER_CATEGORY_ENEMIES;

    int capacity        = 256;
    characters->entries = malloc(sizeof(CharacterEntry) * (size_t)capacity);
    FAIL_IF_NULL(characters->entries);

    Characters__LoadPlayers(characters, &capacity);
    Characters__LoadMonsters(characters, &capacity);
    Characters__LoadObjects(characters, &capacity);
    Characters__DisambiguateNames(characters);

    characters->visible = calloc((size_t)characters->entry_count + 1, sizeof(int));
    FAIL_IF_NULL(characters->visible);
    Characters__BuildVisible(characters);

    LOG_INFO(
        "Catalog: %d players, %d NPCs, %d enemies, %d bosses, %d objects",
        characters->category_counts[CHARACTER_CATEGORY_PLAYERS], characters->category_counts[CHARACTER_CATEGORY_NPCS],
        characters->category_counts[CHARACTER_CATEGORY_ENEMIES], characters->category_counts[CHARACTER_CATEGORY_BOSSES],
        characters->category_counts[CHARACTER_CATEGORY_OTHERS]);

    characters->name_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(characters->name_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int i = 0; i < 1 + CHARACTERS_STAT_LINES; i++) {
        characters->info_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(characters->info_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }
    for (int c = 0; c < CHARACTER_CATEGORY_COUNT; c++) {
        characters->category_labels[c] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetText(characters->category_labels[c], category_names[c]);
        Label_SetAlignment(characters->category_labels[c], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }
    characters->empty_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(characters->empty_label, "No graphics for this animation");
    Label_SetAlignment(characters->empty_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    characters->turn_left  = Button_Create(BUTTON_TYPE_MEDIUM, "Left");
    characters->turn_right = Button_Create(BUTTON_TYPE_MEDIUM, "Right");
    characters->play       = Button_Create(BUTTON_TYPE_MEDIUM, "Play");
    characters->pause      = Button_Create(BUTTON_TYPE_MEDIUM, "Pause");
    characters->prev_mode  = Button_Create(BUTTON_TYPE_MEDIUM, "Prev Anim");
    characters->next_mode  = Button_Create(BUTTON_TYPE_MEDIUM, "Next Anim");
    characters->speed      = Button_Create(BUTTON_TYPE_MEDIUM, "Speed");

    return characters;
}

void CatalogCharacters_Destroy(CatalogCharacters **characters_ref) {
    if (characters_ref == NULL || *characters_ref == NULL) {
        return;
    }
    CatalogCharacters *characters = *characters_ref;

    Figure_Destroy(&characters->figure);
    free(characters->entries);
    free(characters->visible);

    Label_Destroy(&characters->name_label);
    for (int i = 0; i < 1 + CHARACTERS_STAT_LINES; i++) {
        Label_Destroy(&characters->info_labels[i]);
    }
    for (int c = 0; c < CHARACTER_CATEGORY_COUNT; c++) {
        Label_Destroy(&characters->category_labels[c]);
    }
    Label_Destroy(&characters->empty_label);
    Button_Destroy(&characters->turn_left);
    Button_Destroy(&characters->turn_right);
    Button_Destroy(&characters->play);
    Button_Destroy(&characters->pause);
    Button_Destroy(&characters->prev_mode);
    Button_Destroy(&characters->next_mode);
    Button_Destroy(&characters->speed);

    free(characters);
    *characters_ref = NULL;
}

int CatalogCharacters_GetCount(const CatalogCharacters *characters) { return characters->visible_count; }

const char *CatalogCharacters_GetName(const CatalogCharacters *characters, const int index) {
    return characters->entries[characters->visible[index]].name;
}

// ---------------------------------------------------------------------------------------------------------------------
// Category bar

// Tabs share the bar width in proportion to their text, so longer names get more room.
static SDL_Rect Characters__CategoryRect(const CatalogCharacters *characters, const int x, const int y, const int width,
                                         const int category) {
    int text_w[CHARACTER_CATEGORY_COUNT];
    int total = 0;
    for (int c = 0; c < CHARACTER_CATEGORY_COUNT; c++) {
        Label_GetSize(characters->category_labels[c], &text_w[c], NULL);
        total += text_w[c];
    }

    const int padding = (width - total) / CHARACTER_CATEGORY_COUNT;
    int       left    = x;
    for (int c = 0; c < category; c++) {
        left += text_w[c] + padding;
    }

    const int      right = (category == CHARACTER_CATEGORY_COUNT - 1) ? x + width : left + text_w[category] + padding;
    const SDL_Rect rect  = {left, y, right - left, CHARACTERS_CATEGORY_H};
    return rect;
}

bool CatalogCharacters_UpdateCategories(CatalogCharacters *characters, const int x, const int y, const int width) {
    int  mouse_x;
    int  mouse_y;
    bool mouse_left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool clicked          = !mouse_left && characters->prev_mouse_left;
    characters->prev_mouse_left = mouse_left;
    if (!clicked) {
        return false;
    }

    for (int c = 0; c < CHARACTER_CATEGORY_COUNT; c++) {
        const SDL_Rect rect = Characters__CategoryRect(characters, x, y, width, c);
        if (mouse_x >= rect.x && mouse_x < rect.x + rect.w && mouse_y >= rect.y && mouse_y < rect.y + rect.h &&
            c != characters->category) {
            characters->category = c;
            Characters__BuildVisible(characters);
            return true;
        }
    }
    return false;
}

void CatalogCharacters_DrawCategories(const CatalogCharacters *characters, const int x, const int y, const int width) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int c = 0; c < CHARACTER_CATEGORY_COUNT; c++) {
        const SDL_Rect rect     = Characters__CategoryRect(characters, x, y, width, c);
        const bool     selected = c == characters->category;

        SDL_SetRenderDrawColor(sdl_renderer, selected ? 120 : 0, selected ? 95 : 0, selected ? 40 : 0,
                               selected ? 220 : 170);
        SDL_RenderFillRect(sdl_renderer, &rect);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
        SDL_RenderDrawRect(sdl_renderer, &rect);

        Label *label = characters->category_labels[c];
        if (selected) {
            Label_SetColor(label, 0xFF, 0xE8, 0xA0, 0xFF);
        } else {
            Label_SetColor(label, 0xC7, 0xB3, 0x77, 0xFF);
        }
        Label_Draw(label, rect.x + (rect.w / 2), rect.y + (rect.h / 2));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Detail panel

static int Characters__Direction(const CatalogCharacters *characters) {
    const int directions = Figure_GetDirectionCount(characters->figure);
    if (directions == 8) {
        return turn_order_8[characters->turn % 8];
    }
    if (directions == 16) {
        return turn_order_16[characters->turn % 16];
    }
    return characters->turn % directions;
}

static void Characters__UpdateInfo(CatalogCharacters *characters) {
    if (characters->selected < 0) {
        for (int i = 0; i < 1 + CHARACTERS_STAT_LINES; i++) {
            Label_SetText(characters->info_labels[i], "");
        }
        return;
    }

    const CharacterEntry *entry = &characters->entries[characters->selected];
    char                  line[128];

    if (characters->figure != NULL) {
        snprintf(line, sizeof(line), "%s - frame %d/%d - facing %d/%d - speed %gx",
                 entry->modes[characters->mode]->name, characters->frame + 1,
                 Figure_GetFramesPerDirection(characters->figure), characters->turn + 1,
                 Figure_GetDirectionCount(characters->figure), (double)character_speeds[characters_speed]);
    } else {
        snprintf(line, sizeof(line), "%s", entry->modes[characters->mode]->name);
    }
    Label_SetText(characters->info_labels[0], line);
    Label_SetColor(characters->info_labels[0], 0xFF, 0xFF, 0xFF, 0xFF);

    for (int i = 0; i < CHARACTERS_STAT_LINES; i++) {
        Label_SetText(characters->info_labels[1 + i], entry->stats[i]);
        Label_SetColor(characters->info_labels[1 + i], 0xD8, 0xD8, 0xD8, 0xFF);
    }
}

static void Characters__LoadMode(CatalogCharacters *characters) {
    Figure_Destroy(&characters->figure);

    const CharacterEntry *entry = &characters->entries[characters->selected];
    characters->figure          = Figure_Create(&entry->spec, entry->modes[characters->mode]->code, CHARACTERS_PALETTE);
    characters->frame           = 0;
    characters->last_tick       = SDL_GetTicks64();
    characters->playing         = true;

    if (characters->figure != NULL) {
        characters->turn %= Figure_GetDirectionCount(characters->figure);
    }
    Characters__UpdateInfo(characters);
}

void CatalogCharacters_Select(CatalogCharacters *characters, const int index) {
    const int entry_index = (index >= 0 && index < characters->visible_count) ? characters->visible[index] : -1;
    if (entry_index == characters->selected) {
        return;
    }

    characters->selected = entry_index;
    Figure_Destroy(&characters->figure);
    if (entry_index < 0) {
        Label_SetText(characters->name_label, "");
        Characters__UpdateInfo(characters);
        return;
    }

    Label_SetText(characters->name_label, characters->entries[entry_index].name);
    Label_SetColor(characters->name_label, 0xC7, 0xB3, 0x77, 0xFF);

    characters->mode = 0;
    characters->turn = 0;
    Characters__LoadMode(characters);
}

void CatalogCharacters_UpdateDetail(CatalogCharacters *characters) {
    if (characters->selected < 0) {
        return;
    }

    const CharacterEntry *entry       = &characters->entries[characters->selected];
    Button               *play_button = characters->playing ? characters->pause : characters->play;
    const int             directions  = characters->figure != NULL ? Figure_GetDirectionCount(characters->figure) : 1;

    if (Button_Update(characters->turn_left)) {
        characters->turn = (characters->turn + directions - 1) % directions;
        Characters__UpdateInfo(characters);
    }
    if (Button_Update(characters->turn_right)) {
        characters->turn = (characters->turn + 1) % directions;
        Characters__UpdateInfo(characters);
    }
    if (Button_Update(play_button)) {
        characters->playing   = !characters->playing;
        characters->last_tick = SDL_GetTicks64();
    }
    if (Button_Update(characters->prev_mode)) {
        characters->mode = (characters->mode + entry->mode_count - 1) % entry->mode_count;
        Characters__LoadMode(characters);
        return;
    }
    if (Button_Update(characters->next_mode)) {
        characters->mode = (characters->mode + 1) % entry->mode_count;
        Characters__LoadMode(characters);
        return;
    }

    if (Button_Update(characters->speed)) {
        characters_speed = (characters_speed + 1) % CHARACTER_SPEED_COUNT;
        Characters__UpdateInfo(characters);
    }

    if (characters->playing && characters->figure != NULL) {
        const uint64_t now = SDL_GetTicks64();
        const int      frame_ms =
            (int)((float)Figure_GetFrameMilliseconds(characters->figure) / character_speeds[characters_speed]);
        const int frames  = Figure_GetFramesPerDirection(characters->figure);
        bool      changed = false;
        while (frame_ms > 0 && now - characters->last_tick >= (uint64_t)frame_ms) {
            characters->last_tick += (uint64_t)frame_ms;
            characters->frame      = (characters->frame + 1) % frames;
            changed                = true;
        }
        if (changed) {
            Characters__UpdateInfo(characters);
        }
    }
}

void CatalogCharacters_DrawDetail(CatalogCharacters *characters, const SDL_Rect *panel) {
    if (characters->selected < 0) {
        return;
    }

    const int center_x = panel->x + (panel->w / 2);
    Label_Draw(characters->name_label, center_x, panel->y + CHAR_NAME_Y);

    const SDL_Rect preview = {panel->x + 10, panel->y + CHAR_PREVIEW_Y, panel->w - 20, CHAR_PREVIEW_H};
    CatalogTheme_FillPreview(&preview);

    if (characters->figure == NULL) {
        Label_SetColor(characters->empty_label, 0x90, 0x90, 0x90, 0xFF);
        Label_Draw(characters->empty_label, center_x, preview.y + (preview.h / 2));
    } else {
        int xmin;
        int ymin;
        int xmax;
        int ymax;
        Figure_GetBounds(characters->figure, &xmin, &ymin, &xmax, &ymax);
        const int width  = xmax - xmin > 0 ? xmax - xmin : 1;
        const int height = ymax - ymin > 0 ? ymax - ymin : 1;

        float scale = (float)(preview.w - 16) / (float)width;
        if ((float)(preview.h - 16) / (float)height < scale) {
            scale = (float)(preview.h - 16) / (float)height;
        }
        if (scale >= 1.0f) {
            scale = (float)(int)scale > CHARACTERS_MAX_SCALE ? CHARACTERS_MAX_SCALE : (float)(int)scale;
        }

        // Centre the figure's bounding box; the figure is drawn relative to its anchor (its feet)
        const int anchor_x = preview.x + (preview.w / 2) - (int)((float)(xmin + (width / 2)) * scale);
        const int anchor_y = preview.y + ((preview.h - (int)((float)height * scale)) / 2) - (int)((float)ymin * scale);

        SDL_RenderSetClipRect(sdl_renderer, &preview);
        Figure_Draw(characters->figure, Characters__Direction(characters), characters->frame, anchor_x, anchor_y,
                    scale);
        SDL_RenderSetClipRect(sdl_renderer, NULL);
    }

    int y = panel->y + CHAR_INFO_Y;
    for (int i = 0; i < 1 + CHARACTERS_STAT_LINES; i++) {
        Label_Draw(characters->info_labels[i], center_x, y);
        y += CHAR_INFO_LINE;
    }

    Button   *play_button = characters->playing ? characters->pause : characters->play;
    const int left        = panel->x + ((panel->w - (3 * 128) - 8) / 2);
    Button_SetPosition(characters->turn_left, left, panel->y + CHAR_BUTTONS_Y);
    Button_SetPosition(play_button, left + 132, panel->y + CHAR_BUTTONS_Y);
    Button_SetPosition(characters->turn_right, left + 264, panel->y + CHAR_BUTTONS_Y);
    Button_SetPosition(characters->prev_mode, left, panel->y + CHAR_BUTTONS_Y2);
    Button_SetPosition(characters->next_mode, left + 132, panel->y + CHAR_BUTTONS_Y2);
    Button_SetPosition(characters->speed, left + 264, panel->y + CHAR_BUTTONS_Y2);

    const CharacterEntry *entry   = &characters->entries[characters->selected];
    const bool            turning = characters->figure != NULL && Figure_GetDirectionCount(characters->figure) > 1;
    Button_SetEnabled(characters->turn_left, turning);
    Button_SetEnabled(characters->turn_right, turning);
    Button_SetEnabled(characters->prev_mode, entry->mode_count > 1);
    Button_SetEnabled(characters->next_mode, entry->mode_count > 1);
    Button_SetEnabled(play_button, characters->figure != NULL && Figure_GetFramesPerDirection(characters->figure) > 1);

    Button_Draw(characters->turn_left);
    Button_Draw(play_button);
    Button_Draw(characters->turn_right);
    Button_Draw(characters->prev_mode);
    Button_Draw(characters->next_mode);
    Button_Draw(characters->speed);
}
