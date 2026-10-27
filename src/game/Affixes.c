#include "Affixes.h"
#include "Sockets.h"
static void Affixes__AddMod(Item *item, int type, int value);
static void Affixes__AddModParam(Item *item, int type, int value, int param);
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "SkillData.h"
#include "Skills.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_MAGIC_PREFIX "/data/global/excel/MagicPrefix.txt"
#define EXCEL_MAGIC_SUFFIX "/data/global/excel/MagicSuffix.txt"
#define EXCEL_RARE_PREFIX  "/data/global/excel/RarePrefix.txt"
#define EXCEL_RARE_SUFFIX  "/data/global/excel/RareSuffix.txt"
#define EXCEL_UNIQUES      "/data/global/excel/UniqueItems.txt"
#define TYPES_MAX          7
#define PROPS_MAX          12

// ---------------------------------------------------------------------------------------------------------------------
// Properties

typedef struct ModInfo {
    const char *save; // the name in save files
    const char *text; // how the game words it (%d: the value)
} ModInfo;

static const ModInfo mod_info[MOD_TYPE_COUNT] = {
    [MOD_STRENGTH]             = {"str",          "+%d to Strength"                                 },
    [MOD_DEXTERITY]            = {"dex",          "+%d to Dexterity"                                },
    [MOD_VITALITY]             = {"vit",          "+%d to Vitality"                                 },
    [MOD_ENERGY]               = {"enr",          "+%d to Energy"                                   },
    [MOD_LIFE]                 = {"hp",           "+%d to Life"                                     },
    [MOD_MANA]                 = {"mana",         "+%d to Mana"                                     },
    [MOD_STAMINA]              = {"stam",         "+%d Maximum Stamina"                             },
    [MOD_ATTACK_RATING]        = {"att",          "+%d to Attack Rating"                            },
    [MOD_ATTACK_RATING_PCT]    = {"att%",         "%d%% Bonus to Attack Rating"                     },
    [MOD_DEFENSE]              = {"ac",           "+%d Defense"                                     },
    [MOD_DEFENSE_PCT]          = {"ac%",          "+%d%% Enhanced Defense"                          },
    [MOD_DAMAGE_PCT]           = {"dmg%",         "+%d%% Enhanced Damage"                           },
    [MOD_DAMAGE_MIN]           = {"dmg-min",      "+%d to Minimum Damage"                           },
    [MOD_DAMAGE_MAX]           = {"dmg-max",      "+%d to Maximum Damage"                           },
    [MOD_FIRE_MIN]             = {"fire-min",     "+%d to Minimum Fire Damage"                      },
    [MOD_FIRE_MAX]             = {"fire-max",     "+%d to Maximum Fire Damage"                      },
    [MOD_COLD_MIN]             = {"cold-min",     "+%d to Minimum Cold Damage"                      },
    [MOD_COLD_MAX]             = {"cold-max",     "+%d to Maximum Cold Damage"                      },
    [MOD_LIGHTNING_MIN]        = {"ltng-min",     "+%d to Minimum Lightning Damage"                 },
    [MOD_LIGHTNING_MAX]        = {"ltng-max",     "+%d to Maximum Lightning Damage"                 },
    [MOD_POISON]               = {"pois",         "+%d Poison Damage"                               },
    [MOD_RES_FIRE]             = {"res-fire",     "Fire Resist +%d%%"                               },
    [MOD_RES_COLD]             = {"res-cold",     "Cold Resist +%d%%"                               },
    [MOD_RES_LIGHTNING]        = {"res-ltng",     "Lightning Resist +%d%%"                          },
    [MOD_RES_POISON]           = {"res-pois",     "Poison Resist +%d%%"                             },
    [MOD_RES_ALL]              = {"res-all",      "All Resistances +%d"                             },
    [MOD_LIFE_STEAL]           = {"lifesteal",    "%d%% Life Stolen per Hit"                        },
    [MOD_MANA_STEAL]           = {"manasteal",    "%d%% Mana Stolen per Hit"                        },
    [MOD_MAGIC_FIND]           = {"mag%",         "%d%% Better Chance of Getting Magic Items"       },
    [MOD_GOLD_FIND]            = {"gold%",        "%d%% Extra Gold from Monsters"                   },
    [MOD_REPLENISH_LIFE]       = {"regen",        "Replenish Life +%d"                              },
    [MOD_FASTER_RUN]           = {"move",         "+%d%% Faster Run/Walk"                           },
    [MOD_ATTACK_SPEED]         = {"swing",        "+%d%% Increased Attack Speed"                    },
    [MOD_HIT_RECOVERY]         = {"balance",      "+%d%% Faster Hit Recovery"                       },
    [MOD_BLOCK]                = {"block",        "%d%% Increased Chance of Blocking"               },
    [MOD_LIGHT_RADIUS]         = {"light",        "+%d to Light Radius"                             },
    [MOD_THORNS]               = {"thorns",       "Attacker Takes Damage of %d"                     },
    [MOD_DAMAGE_REDUCED]       = {"red-dmg",      "Damage Reduced by %d"                            },
    [MOD_MAGIC_DAMAGE_REDUCED] = {"red-mag",      "Magic Damage Reduced by %d"                      },
    [MOD_ALL_SKILLS]           = {"allskills",    "+%d to All Skills"                               },
    [MOD_CLASS_SKILLS]         = {"clskills",     "+%d to Class Skills"                             },
    [MOD_TAB_SKILLS]           = {"skilltab",     "+%d to Skills"                                   },
    [MOD_SKILL]                = {"skill",        "+%d to a Skill"                                  },
    [MOD_OSKILL]               = {"oskill",       "+%d to a Skill"                                  },
    [MOD_DURABILITY]           = {"dur",          "+%d Maximum Durability"                          },
    [MOD_INDESTRUCTIBLE]       = {"indestruct",   "Indestructible"                                  },
    [MOD_MANA_REGEN]           = {"regen-mana",   "Regenerate Mana %d%%"                            },
    [MOD_MANA_PCT]             = {"mana%",        "Increase Maximum Mana %d%%"                      },
    [MOD_DEFENSE_PER_LEVEL]    = {"ac/lvl",       "+%d Defense (Based on Character Level)"          },
    [MOD_MAX_DAMAGE_PER_LEVEL] = {"dmg/lvl",      "+%d to Maximum Damage (Based on Character Level)"},
    [MOD_DAMAGE_UNDEAD]        = {"dmg-undead",   "+%d%% Damage to Undead"                          },
    [MOD_DAMAGE_DEMON]         = {"dmg-demon",    "+%d%% Damage to Demons"                          },
    [MOD_ATTACK_UNDEAD]        = {"att-undead",   "+%d to Attack Rating against Undead"             },
    [MOD_ATTACK_DEMON]         = {"att-demon",    "+%d to Attack Rating against Demons"             },
    [MOD_CRUSHING_BLOW]        = {"crush",        "%d%% Chance of Crushing Blow"                    },
    [MOD_DEADLY_STRIKE]        = {"deadly",       "%d%% Deadly Strike"                              },
    [MOD_IGNORE_DEFENSE]       = {"ignore-ac",    "Ignore Target's Defense"                         },
    [MOD_MANA_PER_KILL]        = {"mana-kill",    "+%d to Mana after each Kill"                     },
    [MOD_LIFE_PER_KILL]        = {"heal-kill",    "+%d Life after each Kill"                        },
    [MOD_DAMAGE_TO_MANA]       = {"dmg-to-mana",  "%d%% Damage Taken Goes To Mana"                  },
    [MOD_DAMAGE_REDUCED_PCT]   = {"red-dmg%",     "Damage Reduced by %d%%"                          },
    [MOD_LIGHTNING_THORNS]     = {"light-thorns", "Attacker Takes Lightning Damage of %d"           },
};

// Properties.txt codes we know: one property, or a pair (minimum to the first, maximum to the second). IGNORE: fine
// to have, nothing to show (a cold affix's length).
#define IGNORE (-2)
static const struct {
    const char *code;
    int         type;
    int         pair; // -1: one value
} codes[] = {
    {"str",          MOD_STRENGTH,             -1               },
    {"dex",          MOD_DEXTERITY,            -1               },
    {"vit",          MOD_VITALITY,             -1               },
    {"enr",          MOD_ENERGY,               -1               },
    {"hp",           MOD_LIFE,                 -1               },
    {"mana",         MOD_MANA,                 -1               },
    {"stam",         MOD_STAMINA,              -1               },
    {"att",          MOD_ATTACK_RATING,        -1               },
    {"att%",         MOD_ATTACK_RATING_PCT,    -1               },
    {"ac",           MOD_DEFENSE,              -1               },
    {"ac%",          MOD_DEFENSE_PCT,          -1               },
    {"dmg%",         MOD_DAMAGE_PCT,           -1               },
    {"dmg-min",      MOD_DAMAGE_MIN,           -1               },
    {"dmg-max",      MOD_DAMAGE_MAX,           -1               },
    {"dmg-norm",     MOD_DAMAGE_MIN,           MOD_DAMAGE_MAX   },
    {"fire-min",     MOD_FIRE_MIN,             -1               },
    {"fire-max",     MOD_FIRE_MAX,             -1               },
    {"dmg-fire",     MOD_FIRE_MIN,             MOD_FIRE_MAX     },
    {"cold-min",     MOD_COLD_MIN,             -1               },
    {"cold-max",     MOD_COLD_MAX,             -1               },
    {"dmg-cold",     MOD_COLD_MIN,             MOD_COLD_MAX     },
    {"ltng-min",     MOD_LIGHTNING_MIN,        -1               },
    {"ltng-max",     MOD_LIGHTNING_MAX,        -1               },
    {"dmg-ltng",     MOD_LIGHTNING_MIN,        MOD_LIGHTNING_MAX},
    {"dmg-pois",     MOD_POISON,               -1               },
    {"pois-min",     MOD_POISON,               -1               },
    {"res-fire",     MOD_RES_FIRE,             -1               },
    {"res-cold",     MOD_RES_COLD,             -1               },
    {"res-ltng",     MOD_RES_LIGHTNING,        -1               },
    {"res-pois",     MOD_RES_POISON,           -1               },
    {"res-all",      MOD_RES_ALL,              -1               },
    {"lifesteal",    MOD_LIFE_STEAL,           -1               },
    {"manasteal",    MOD_MANA_STEAL,           -1               },
    {"mag%",         MOD_MAGIC_FIND,           -1               },
    {"gold%",        MOD_GOLD_FIND,            -1               },
    {"regen",        MOD_REPLENISH_LIFE,       -1               },
    {"move1",        MOD_FASTER_RUN,           -1               },
    {"move2",        MOD_FASTER_RUN,           -1               },
    {"move3",        MOD_FASTER_RUN,           -1               },
    {"swing1",       MOD_ATTACK_SPEED,         -1               },
    {"swing2",       MOD_ATTACK_SPEED,         -1               },
    {"swing3",       MOD_ATTACK_SPEED,         -1               },
    {"balance1",     MOD_HIT_RECOVERY,         -1               },
    {"balance2",     MOD_HIT_RECOVERY,         -1               },
    {"balance3",     MOD_HIT_RECOVERY,         -1               },
    {"block",        MOD_BLOCK,                -1               },
    {"light",        MOD_LIGHT_RADIUS,         -1               },
    {"thorns",       MOD_THORNS,               -1               },
    {"red-dmg",      MOD_DAMAGE_REDUCED,       -1               },
    {"red-mag",      MOD_MAGIC_DAMAGE_REDUCED, -1               },
    {"cold-len",     IGNORE,                   -1               },
    {"pois-len",     IGNORE,                   -1               },
    {"allskills",    MOD_ALL_SKILLS,           -1               },
    {"ama",          MOD_CLASS_SKILLS,         -1               }, // (the class from the code: Affixes__Param)
    {"sor",          MOD_CLASS_SKILLS,         -1               },
    {"nec",          MOD_CLASS_SKILLS,         -1               },
    {"pal",          MOD_CLASS_SKILLS,         -1               },
    {"bar",          MOD_CLASS_SKILLS,         -1               },
    {"dru",          MOD_CLASS_SKILLS,         -1               },
    {"ass",          MOD_CLASS_SKILLS,         -1               },
    {"skilltab",     MOD_TAB_SKILLS,           -1               },
    {"skill",        MOD_SKILL,                -1               },
    {"oskill",       MOD_OSKILL,               -1               },
    {"dur",          MOD_DURABILITY,           -1               },
    {"indestruct",   MOD_INDESTRUCTIBLE,       -1               },
    {"sock",         IGNORE,                   -1               }, // (sockets: made part of the item, Affixes__Apply)
    {"regen-mana",   MOD_MANA_REGEN,           -1               },
    {"mana%",        MOD_MANA_PCT,             -1               },
    {"ac/lvl",       MOD_DEFENSE_PER_LEVEL,    -1               },
    {"dmg/lvl",      MOD_MAX_DAMAGE_PER_LEVEL, -1               },
    {"dmg-undead",   MOD_DAMAGE_UNDEAD,        -1               },
    {"dmg-demon",    MOD_DAMAGE_DEMON,         -1               },
    {"att-undead",   MOD_ATTACK_UNDEAD,        -1               },
    {"att-demon",    MOD_ATTACK_DEMON,         -1               },
    {"crush",        MOD_CRUSHING_BLOW,        -1               },
    {"deadly",       MOD_DEADLY_STRIKE,        -1               },
    {"ignore-ac",    MOD_IGNORE_DEFENSE,       -1               },
    {"mana-kill",    MOD_MANA_PER_KILL,        -1               },
    {"heal-kill",    MOD_LIFE_PER_KILL,        -1               },
    {"dmg-to-mana",  MOD_DAMAGE_TO_MANA,       -1               },
    {"red-dmg%",     MOD_DAMAGE_REDUCED_PCT,   -1               },
    {"light-thorns", MOD_LIGHTNING_THORNS,     -1               },
};

static const char *const class_codes[CHARACTER_CLASS_COUNT] = {"ama", "sor", "nec", "pal", "bar", "dru", "ass"};

// A property's parameter (the table's "param" / "par" column) for what the engine keeps: the class of a class-skills
// property (from its code), the tree, the skill (by number or by name); per-level ones keep theirs as the value
static int Affixes__Param(const char *code, const int type, const char *param) {
    switch (type) {
    case MOD_CLASS_SKILLS:
        for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
            if (strcmp(code, class_codes[c]) == 0) {
                return c;
            }
        }
        return 0;
    case MOD_TAB_SKILLS:
        return atoi(param);
    case MOD_SKILL:
    case MOD_OSKILL: {
        if (param[0] >= '0' && param[0] <= '9') {
            return atoi(param);
        }
        const SkillRow *skill = SkillData_Get(param);
        return skill != NULL ? skill->row : -1;
    }
    default:
        return 0;
    }
}

static int Affixes__Code(const char *code, int *pair) {
    *pair = -1;
    if (code[0] == '\0') {
        return IGNORE;
    }
    for (size_t c = 0; c < sizeof(codes) / sizeof(codes[0]); c++) {
        if (strcmp(codes[c].code, code) == 0) {
            *pair = codes[c].pair;
            return codes[c].type;
        }
    }
    return -1; // not one we know
}

// ---------------------------------------------------------------------------------------------------------------------
// Tables

typedef struct Prop {
    char code[16];
    char param[32]; // "par" / "param": a skill, a tree, a per-level amount
    int  min;
    int  max;
} Prop;

typedef struct Affix {
    char name[48];
    int  level;
    int  max_level;
    int  frequency;
    int  group;
    bool rare; // can be on rare items
    Prop props[3];
    int  prop_count;
    char itypes[TYPES_MAX][8];
    int  itype_count;
    char etypes[5][8];
    int  etype_count;
} Affix;

typedef struct RareName {
    char name[48];
    char itypes[TYPES_MAX][8];
    int  itype_count;
    char etypes[4][8];
    int  etype_count;
} RareName;

typedef struct Unique {
    char name[64];
    char code[ITEM_CODE_MAX];
    int  level;
    int  rarity;
    char inv_file[32];
    Prop props[PROPS_MAX];
    int  prop_count;
} Unique;

// A set's piece (SetItems.txt): its own magic, and more for each other piece worn (aprop1: two worn, aprop2: three,
// ...)
typedef struct SetItem {
    char name[64];
    char set[64];
    char code[ITEM_CODE_MAX];
    int  level;
    int  rarity;
    char inv_file[32];
    Prop props[PROPS_MAX];
    int  prop_count;
    Prop worn[5][2]; // [pieces worn - 2]
} SetItem;

// A set (Sets.txt): what wearing two, three, ... of it gives, and the whole set
typedef struct ItemSet {
    char name[64];
    Prop partial[4][2]; // [pieces worn - 2]
    Prop full[8];
    int  full_count;
    int  pieces; // how many items it has (SetItems.txt)
} ItemSet;

typedef struct AffixTable {
    Affix *rows;
    int    count;
} AffixTable;

static AffixTable prefixes;
static AffixTable suffixes;
static RareName  *rare_prefixes;
static int        rare_prefix_count;
static RareName  *rare_suffixes;
static int        rare_suffix_count;
static Unique    *uniques;
static int        unique_count;
static SetItem   *set_items;
static int        set_item_count;
static ItemSet   *item_sets;
static int        item_set_count;
static bool       loaded = false;

// A name from the string tables (the files hold their keys), cleaned of the game's colour codes
static void Affixes__Text(char *out, const size_t size, const char *key) {
    const char *found = StringTable_Find(key);
    const char *text  = found != NULL ? found : key;
    size_t      o     = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0' && o + 1 < size; c++) {
        if (*c == 0xFF && c[1] == 'c' && c[2] != '\0') {
            c += 2;
            continue;
        }
        if (*c >= 0x20 && *c < 0x7F) {
            out[o++] = (char)*c;
        }
    }
    out[o] = '\0';
}

static int Affixes__Types(const DataTable *t, const int row, const char *prefix, const int most, char out[][8]) {
    int count = 0;
    for (int i = 1; i <= most; i++) {
        char column[16];
        snprintf(column, sizeof(column), "%s%d", prefix, i);
        const char *type = DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column));
        if (type[0] != '\0') {
            snprintf(out[count++], 8, "%s", type);
        }
    }
    return count;
}

static void Affixes__LoadAffixes(const char *path, AffixTable *table) {
    DataTable *t = DataTable_Load(path);
    if (t == NULL) {
        return;
    }
    const int rows = DataTable_GetRowCount(t);
    table->rows    = calloc((size_t)(rows > 0 ? rows : 1), sizeof(Affix));
    FAIL_IF_NULL(table->rows);
    table->count = rows; // every row, so row numbers stay the file's (saves refer to them)
#define COL(n) DataTable_GetColumnIndex(t, n)
    const int c_name = COL("Name"), c_spawn = COL("spawnable"), c_rare = COL("rare"), c_level = COL("level"),
              c_max = COL("maxlevel"), c_class = COL("classspecific"), c_freq = COL("frequency"),
              c_group = COL("group");
#undef COL
    for (int row = 0; row < rows; row++) {
        Affix *affix = &table->rows[row];
        Affixes__Text(affix->name, sizeof(affix->name), DataTable_GetString(t, row, c_name));
        // Not spawnable, or for one class only (class skills aren't in yet): never picked
        const bool usable = DataTable_GetInt(t, row, c_spawn) == 1 && DataTable_GetString(t, row, c_class)[0] == '\0';
        affix->frequency  = usable ? DataTable_GetInt(t, row, c_freq) : 0;
        affix->level      = DataTable_GetInt(t, row, c_level);
        affix->max_level  = DataTable_GetInt(t, row, c_max);
        affix->group      = DataTable_GetInt(t, row, c_group);
        affix->rare       = DataTable_GetInt(t, row, c_rare) == 1;
        for (int m = 1; m <= 3; m++) {
            char column[16];
            snprintf(column, sizeof(column), "mod%dcode", m);
            const char *code = DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column));
            if (code[0] == '\0') {
                continue;
            }
            int pair = -1;
            if (Affixes__Code(code, &pair) == -1) {
                affix->frequency = 0; // does something the engine doesn't: not used
            }
            Prop *prop = &affix->props[affix->prop_count++];
            snprintf(prop->code, sizeof(prop->code), "%s", code);
            snprintf(column, sizeof(column), "mod%dparam", m);
            snprintf(prop->param, sizeof(prop->param), "%s",
                     DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column)));
            snprintf(column, sizeof(column), "mod%dmin", m);
            prop->min = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, column));
            snprintf(column, sizeof(column), "mod%dmax", m);
            prop->max = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, column));
        }
        affix->itype_count = Affixes__Types(t, row, "itype", TYPES_MAX, affix->itypes);
        affix->etype_count = Affixes__Types(t, row, "etype", 5, affix->etypes);
    }
    DataTable_Destroy(&t);
}

static RareName *Affixes__LoadRareNames(const char *path, int *count) {
    DataTable *t = DataTable_Load(path);
    if (t == NULL) {
        return NULL;
    }
    const int rows  = DataTable_GetRowCount(t);
    RareName *names = calloc((size_t)(rows > 0 ? rows : 1), sizeof(RareName));
    FAIL_IF_NULL(names);
    for (int row = 0; row < rows; row++) {
        Affixes__Text(names[row].name, sizeof(names[row].name),
                      DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "name")));
        names[row].itype_count = Affixes__Types(t, row, "itype", TYPES_MAX, names[row].itypes);
        names[row].etype_count = Affixes__Types(t, row, "etype", 4, names[row].etypes);
    }
    *count = rows;
    DataTable_Destroy(&t);
    return names;
}

static void Affixes__LoadUniques(void) {
    DataTable *t = DataTable_Load(EXCEL_UNIQUES);
    if (t == NULL) {
        return;
    }
    const int rows = DataTable_GetRowCount(t);
    uniques        = calloc((size_t)(rows > 0 ? rows : 1), sizeof(Unique));
    FAIL_IF_NULL(uniques);
    unique_count = rows;
#define COL(n) DataTable_GetColumnIndex(t, n)
    const int c_index = COL("index"), c_enabled = COL("enabled"), c_level = COL("lvl"), c_code = COL("code"),
              c_rarity = COL("rarity"), c_inv = COL("invfile");
#undef COL
    for (int row = 0; row < rows; row++) {
        Unique *unique = &uniques[row];
        Affixes__Text(unique->name, sizeof(unique->name), DataTable_GetString(t, row, c_index));
        snprintf(unique->code, sizeof(unique->code), "%s",
                 DataTable_GetInt(t, row, c_enabled) == 1 ? DataTable_GetString(t, row, c_code) : "");
        unique->level  = DataTable_GetInt(t, row, c_level);
        unique->rarity = DataTable_GetInt(t, row, c_rarity) > 0 ? DataTable_GetInt(t, row, c_rarity) : 1;
        snprintf(unique->inv_file, sizeof(unique->inv_file), "%s", DataTable_GetString(t, row, c_inv));
        for (int p = 1; p <= PROPS_MAX; p++) {
            char column[16];
            snprintf(column, sizeof(column), "prop%d", p);
            const char *code = DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column));
            if (code[0] == '\0') {
                continue;
            }
            Prop *prop = &unique->props[unique->prop_count++];
            snprintf(prop->code, sizeof(prop->code), "%s", code);
            snprintf(column, sizeof(column), "par%d", p);
            snprintf(prop->param, sizeof(prop->param), "%s",
                     DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column)));
            snprintf(column, sizeof(column), "min%d", p);
            prop->min = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, column));
            snprintf(column, sizeof(column), "max%d", p);
            prop->max = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, column));
        }
    }
    DataTable_Destroy(&t);
}

static void Affixes__ReadProp(const DataTable *t, const int row, Prop *prop, const char *code, const char *param,
                              const char *min, const char *max) {
    snprintf(prop->code, sizeof(prop->code), "%s", DataTable_GetString(t, row, DataTable_GetColumnIndex(t, code)));
    snprintf(prop->param, sizeof(prop->param), "%s", DataTable_GetString(t, row, DataTable_GetColumnIndex(t, param)));
    prop->min = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, min));
    prop->max = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, max));
}

static void Affixes__LoadSets(void) {
    DataTable *t = DataTable_Load("/data/global/excel/SetItems.txt");
    if (t == NULL) {
        return;
    }
    const int rows = DataTable_GetRowCount(t);
    set_items      = calloc((size_t)(rows > 0 ? rows : 1), sizeof(SetItem));
    FAIL_IF_NULL(set_items);
    set_item_count = rows;
    char a[24];
    char b[24];
    char c[24];
    char d[24];
    for (int row = 0; row < rows; row++) {
        SetItem *item = &set_items[row];
        Affixes__Text(item->name, sizeof(item->name),
                      DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "index")));
        snprintf(item->set, sizeof(item->set), "%s", DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "set")));
        snprintf(item->code, sizeof(item->code), "%s",
                 DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "item")));
        item->level  = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "lvl"));
        item->rarity = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, "rarity"));
        item->rarity = item->rarity > 0 ? item->rarity : 1;
        snprintf(item->inv_file, sizeof(item->inv_file), "%s",
                 DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "invfile")));
        for (int p = 1; p <= PROPS_MAX; p++) {
            snprintf(a, sizeof(a), "prop%d", p);
            snprintf(b, sizeof(b), "min%d", p);
            snprintf(c, sizeof(c), "max%d", p);
            snprintf(d, sizeof(d), "par%d", p);
            Prop prop;
            Affixes__ReadProp(t, row, &prop, a, d, b, c);
            if (prop.code[0] != '\0') {
                item->props[item->prop_count++] = prop;
            }
        }
        for (int n = 1; n <= 5; n++) {
            for (int s = 0; s < 2; s++) {
                snprintf(a, sizeof(a), "aprop%d%c", n, 'a' + s);
                snprintf(b, sizeof(b), "amin%d%c", n, 'a' + s);
                snprintf(c, sizeof(c), "amax%d%c", n, 'a' + s);
                snprintf(d, sizeof(d), "apar%d%c", n, 'a' + s);
                Affixes__ReadProp(t, row, &item->worn[n - 1][s], a, d, b, c);
            }
        }
    }
    DataTable_Destroy(&t);
    t = DataTable_Load("/data/global/excel/Sets.txt");
    if (t == NULL) {
        return;
    }
    const int set_rows = DataTable_GetRowCount(t);
    item_sets          = calloc((size_t)(set_rows > 0 ? set_rows : 1), sizeof(ItemSet));
    FAIL_IF_NULL(item_sets);
    item_set_count = set_rows;
    for (int row = 0; row < set_rows; row++) {
        ItemSet *set = &item_sets[row];
        snprintf(set->name, sizeof(set->name), "%s", DataTable_GetString(t, row, DataTable_GetColumnIndex(t, "index")));
        for (int n = 2; n <= 5; n++) {
            for (int s = 0; s < 2; s++) {
                snprintf(a, sizeof(a), "PCode%d%c", n, 'a' + s);
                snprintf(b, sizeof(b), "PMin%d%c", n, 'a' + s);
                snprintf(c, sizeof(c), "PMax%d%c", n, 'a' + s);
                snprintf(d, sizeof(d), "PParam%d%c", n, 'a' + s);
                Affixes__ReadProp(t, row, &set->partial[n - 2][s], a, d, b, c);
            }
        }
        for (int f = 1; f <= 8; f++) {
            snprintf(a, sizeof(a), "FCode%d", f);
            snprintf(b, sizeof(b), "FMin%d", f);
            snprintf(c, sizeof(c), "FMax%d", f);
            snprintf(d, sizeof(d), "FParam%d", f);
            Prop prop;
            Affixes__ReadProp(t, row, &prop, a, d, b, c);
            if (prop.code[0] != '\0') {
                set->full[set->full_count++] = prop;
            }
        }
        for (int i = 0; i < set_item_count; i++) {
            set->pieces += strcmp(set_items[i].set, set->name) == 0 ? 1 : 0;
        }
    }
    DataTable_Destroy(&t);
}

static void Affixes__Load(void) {
    if (loaded) {
        return;
    }
    loaded = true;
    Affixes__LoadAffixes(EXCEL_MAGIC_PREFIX, &prefixes);
    Affixes__LoadAffixes(EXCEL_MAGIC_SUFFIX, &suffixes);
    rare_prefixes = Affixes__LoadRareNames(EXCEL_RARE_PREFIX, &rare_prefix_count);
    rare_suffixes = Affixes__LoadRareNames(EXCEL_RARE_SUFFIX, &rare_suffix_count);
    Affixes__LoadUniques();
    Affixes__LoadSets();
    LOG_INFO("Affixes: %d prefixes, %d suffixes, %d uniques", prefixes.count, suffixes.count, unique_count);
}

// ---------------------------------------------------------------------------------------------------------------------
// Rolling

static int Affixes__Roll(const int low, const int high) { return high > low ? low + (rand() % (high - low + 1)) : low; }

static bool Affixes__Fits(const ItemBase *base, char itypes[][8], const int itype_count, char etypes[][8],
                          const int etype_count) {
    bool any = false;
    for (int i = 0; i < itype_count && !any; i++) {
        any = Items_IsA(base, itypes[i]);
    }
    for (int e = 0; e < etype_count && any; e++) {
        any = !Items_IsA(base, etypes[e]);
    }
    return any;
}

void Affixes_AddMod(Item *item, const int type, const int value) { Affixes__AddModParam(item, type, value, 0); }

static void Affixes__AddMod(Item *item, const int type, const int value) { Affixes__AddModParam(item, type, value, 0); }

static void Affixes__AddModParam(Item *item, const int type, const int value, const int param) {
    if (type < 0 || item->mod_count >= ITEM_MODS_MAX ||
        (value == 0 && type != MOD_INDESTRUCTIBLE && type != MOD_IGNORE_DEFENSE)) {
        return;
    }
    for (int m = 0; m < item->mod_count; m++) { // two affixes giving the same thing add up
        if (item->mods[m].type == type && item->mods[m].param == param) {
            item->mods[m].value = (int16_t)(item->mods[m].value + value);
            return;
        }
    }
    item->mods[item->mod_count].type    = (uint8_t)type;
    item->mods[item->mod_count].param   = (int16_t)param;
    item->mods[item->mod_count++].value = (int16_t)(value > 32000 ? 32000 : (value < -32000 ? -32000 : value));
}

// One property onto an item: most become a mod; durability, indestructible and sockets change the item itself
static void Affixes__ApplyOne(Item *item, const char *code, const char *param, const int min, const int max) {
    int       pair = -1;
    const int type = Affixes__Code(code, &pair);
    if (strcmp(code, "sock") == 0) {
        const int sockets = param[0] != '\0' ? atoi(param) : Affixes__Roll(min, max);
        item->sockets     = (uint8_t)(sockets > ITEM_SOCKETS_MAX ? ITEM_SOCKETS_MAX : (sockets > 0 ? sockets : 1));
        return;
    }
    if (type < 0) {
        return; // unknown (uniques keep the rest) or nothing to show
    }
    if (pair >= 0) {
        Affixes__AddMod(item, type, min);
        Affixes__AddMod(item, pair, max);
        return;
    }
    int value = Affixes__Roll(min, max);
    if (type == MOD_DEFENSE_PER_LEVEL || type == MOD_MAX_DAMAGE_PER_LEVEL) {
        value = atoi(param); // eighths a level
    } else if (type == MOD_IGNORE_DEFENSE || type == MOD_INDESTRUCTIBLE) {
        value = 1;
    }
    const int which = Affixes__Param(code, type, param);
    if ((type == MOD_SKILL || type == MOD_OSKILL) && which < 0) {
        return; // a skill we don't know
    }
    if (type == MOD_DURABILITY && item->max_durability > 0) {
        item->max_durability = (int16_t)(item->max_durability + value);
        item->durability     = item->max_durability;
    }
    if (type == MOD_INDESTRUCTIBLE) {
        item->max_durability = 0; // never wears down
        item->durability     = 0;
    }
    Affixes__AddModParam(item, type, value, which);
}

void Affixes_AddProperty(Item *item, const char *code, const int min, const int max) {
    Affixes__ApplyOne(item, code, "", min, max);
}

void Affixes_AddPropertyParam(Item *item, const char *code, const char *param, const int min, const int max) {
    Affixes__ApplyOne(item, code, param != NULL ? param : "", min, max);
}

static void Affixes__Apply(Item *item, const Prop *props, const int count) {
    for (int p = 0; p < count; p++) {
        Affixes__ApplyOne(item, props[p].code, props[p].param, props[p].min, props[p].max);
    }
}

// One affix from a table for the item, weighted by frequency; -1 if none fits. used_groups: groups already on it.
static int Affixes__Pick(const AffixTable *table, const ItemBase *base, const int level, const bool rare,
                         const int *used_groups, const int used_count) {
    int total = 0;
    for (int pass = 0; pass < 2; pass++) {
        int roll = pass == 1 && total > 0 ? rand() % total : 0;
        for (int row = 0; row < table->count; row++) {
            const Affix *affix = &table->rows[row];
            if (affix->frequency <= 0 || affix->level > level || (affix->max_level > 0 && affix->max_level < level) ||
                (rare && !affix->rare) ||
                !Affixes__Fits(base, (char(*)[8])affix->itypes, affix->itype_count, (char(*)[8])affix->etypes,
                               affix->etype_count)) {
                continue;
            }
            bool taken = false;
            for (int g = 0; g < used_count && !taken; g++) {
                taken = used_groups[g] == affix->group;
            }
            if (taken) {
                continue;
            }
            if (pass == 0) {
                total += affix->frequency;
            } else if (roll < affix->frequency) {
                return row;
            } else {
                roll -= affix->frequency;
            }
        }
        if (total == 0) {
            return -1;
        }
    }
    return -1;
}

static int Affixes__PickRareName(RareName *names, const int count, const ItemBase *base) {
    int found  = 0;
    int chosen = -1;
    for (int r = 0; r < count; r++) {
        if (names[r].name[0] != '\0' &&
            Affixes__Fits(base, names[r].itypes, names[r].itype_count, names[r].etypes, names[r].etype_count) &&
            rand() % ++found == 0) {
            chosen = r;
        }
    }
    return chosen;
}

static bool Affixes__MakeUnique(Item *item, const ItemBase *base, const int level) {
    int total = 0;
    for (int u = 0; u < unique_count; u++) {
        total += strcmp(uniques[u].code, base->code) == 0 && uniques[u].level <= level ? uniques[u].rarity : 0;
    }
    if (total == 0) {
        return false;
    }
    int roll = rand() % total;
    for (int u = 0; u < unique_count; u++) {
        if (strcmp(uniques[u].code, base->code) != 0 || uniques[u].level > level) {
            continue;
        }
        if (roll < uniques[u].rarity) {
            item->quality = ITEM_UNIQUE;
            item->name1   = (int16_t)(u + 1);
            Affixes__Apply(item, uniques[u].props, uniques[u].prop_count);
            return true;
        }
        roll -= uniques[u].rarity;
    }
    return false;
}

static bool Affixes__MakeSet(Item *item, const ItemBase *base, const int level) {
    int total = 0;
    for (int s = 0; s < set_item_count; s++) {
        total += strcmp(set_items[s].code, base->code) == 0 && set_items[s].level <= level ? set_items[s].rarity : 0;
    }
    if (total == 0) {
        return false;
    }
    int roll = rand() % total;
    for (int s = 0; s < set_item_count; s++) {
        if (strcmp(set_items[s].code, base->code) != 0 || set_items[s].level > level) {
            continue;
        }
        if (roll < set_items[s].rarity) {
            item->quality = ITEM_SET;
            item->name1   = (int16_t)(s + 1);
            Affixes__Apply(item, set_items[s].props, set_items[s].prop_count);
            return true;
        }
        roll -= set_items[s].rarity;
    }
    return false;
}

static bool Affixes__MakeRare(Item *item, const ItemBase *base, int level);

bool Affixes_MakeRare(Item *item, const int item_level) {
    Affixes__Load();
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return false;
    }
    item->mod_count = 0;
    return Affixes__MakeRare(item, base, item_level > 0 ? item_level : 1);
}

static bool Affixes__MakeRare(Item *item, const ItemBase *base, const int level) {
    const int first  = Affixes__PickRareName(rare_prefixes, rare_prefix_count, base);
    const int second = Affixes__PickRareName(rare_suffixes, rare_suffix_count, base);
    if (first < 0 || second < 0) {
        return false;
    }
    int       groups[6];
    int       group_count = 0;
    int       prefix_left = 3;
    int       suffix_left = 3;
    const int wanted      = Affixes__Roll(3, 6);
    for (int a = 0; a < wanted * 3 && group_count < wanted; a++) {
        const bool        prefix = (prefix_left > 0 && rand() % 2 == 0) || suffix_left == 0;
        const AffixTable *table  = prefix ? &prefixes : &suffixes;
        const int         row    = Affixes__Pick(table, base, level, true, groups, group_count);
        if (row < 0) {
            continue;
        }
        groups[group_count++]  = table->rows[row].group;
        prefix_left           -= prefix ? 1 : 0;
        suffix_left           -= prefix ? 0 : 1;
        Affixes__Apply(item, table->rows[row].props, table->rows[row].prop_count);
    }
    if (item->mod_count == 0) {
        return false;
    }
    item->quality = ITEM_RARE;
    item->name1   = (int16_t)(first + 1);
    item->name2   = (int16_t)(second + 1);
    return true;
}

static bool Affixes__MakeMagic(Item *item, const ItemBase *base, const int level) {
    // A prefix, a suffix, or both (about half each have one, a quarter both, like the game)
    const int  kind   = rand() % 4; // 0 prefix only, 1 suffix only, 2-3 both
    const int  prefix = kind != 1 ? Affixes__Pick(&prefixes, base, level, false, NULL, 0) : -1;
    const int  suffix = kind != 0 ? Affixes__Pick(&suffixes, base, level, false, NULL, 0) : -1;
    const bool any    = prefix >= 0 || suffix >= 0;
    if (!any) {
        return false;
    }
    item->quality = ITEM_MAGIC;
    if (prefix >= 0) {
        item->name1 = (int16_t)(prefix + 1);
        Affixes__Apply(item, prefixes.rows[prefix].props, prefixes.rows[prefix].prop_count);
    }
    if (suffix >= 0) {
        item->name2 = (int16_t)(suffix + 1);
        Affixes__Apply(item, suffixes.rows[suffix].props, suffixes.rows[suffix].prop_count);
    }
    return true;
}

bool Affixes_RollQuality(Item *item, const int item_level, const ItemQuality quality) {
    Affixes__Load();
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return false;
    }
    const int level = item_level > 0 ? item_level : 1;
    item->mod_count = 0;
    switch (quality) {
    case ITEM_UNIQUE:
        return Affixes__MakeUnique(item, base, level);
    case ITEM_SET:
        return Affixes__MakeSet(item, base, level);
    case ITEM_RARE:
        return Affixes__MakeRare(item, base, level);
    case ITEM_MAGIC:
        return Affixes__MakeMagic(item, base, level);
    default:
        return quality == ITEM_NORMAL;
    }
}

static int magic_find = 0; // Affixes_SetMagicFind

void Affixes_SetMagicFind(const int percent) { magic_find = percent > 0 ? percent : 0; }

void Affixes_Roll(Item *item, const int item_level) {
    Affixes__Load();
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return;
    }
    const bool jewelry =
        Items_IsA(base, "ring") || Items_IsA(base, "amul") || Items_IsA(base, "jewl") || Items_IsA(base, "char");
    if (!jewelry && base->kind == ITEM_KIND_MISC) {
        return; // potions, scrolls, keys, gold: always normal
    }
    const int level = item_level > 0 ? item_level : 1;
    // Out of 1,000: unique 10 (1 in 100), set 20 (1 in 50), rare 30 (1 in 20), magic 250 (1 in 4); Magic Find makes
    // each likelier, with the game's diminishing returns for the better ones
    const int mf     = magic_find;
    const int unique = 10 * (100 + (mf * 250 / (mf + 250))) / 100;
    const int set    = unique + (20 * (100 + (mf * 500 / (mf + 500))) / 100);
    const int rare   = set + (30 * (100 + (mf * 600 / (mf + 600))) / 100);
    const int magic  = rare + (250 * (100 + mf) / 100);
    const int roll   = rand() % 1000;
    if (roll < unique && Affixes__MakeUnique(item, base, level)) {
        return;
    }
    if (roll < set && Affixes__MakeSet(item, base, level)) {
        return;
    }
    if (roll < rare && Affixes__MakeRare(item, base, level)) {
        return;
    }
    if (roll < magic || jewelry) { // (jewelry is never plain)
        Affixes__MakeMagic(item, base, level);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Showing

void Affixes_Name(const Item *item, char *line1, const size_t size1, char *line2, const size_t size2) {
    Affixes__Load();
    const ItemBase *base      = Items_Find(item->code);
    const char     *base_name = base != NULL ? base->name : item->code;
    line2[0]                  = '\0';
    if (item->unidentified) {
        snprintf(line1, size1, "%s", base_name); // (in its quality's colour: what it is shows once identified)
        return;
    }
    switch (item->quality) {
    case ITEM_MAGIC: {
        const char *prefix =
            item->name1 > 0 && item->name1 <= prefixes.count ? prefixes.rows[item->name1 - 1].name : "";
        const char *suffix =
            item->name2 > 0 && item->name2 <= suffixes.count ? suffixes.rows[item->name2 - 1].name : "";
        snprintf(line1, size1, "%s%s%s%s%s", prefix, prefix[0] != '\0' ? " " : "", base_name,
                 suffix[0] != '\0' ? " " : "", suffix);
        return;
    }
    case ITEM_RARE:
        snprintf(line1, size1, "%s %s",
                 item->name1 > 0 && item->name1 <= rare_prefix_count ? rare_prefixes[item->name1 - 1].name : "?",
                 item->name2 > 0 && item->name2 <= rare_suffix_count ? rare_suffixes[item->name2 - 1].name : "?");
        snprintf(line2, size2, "%s", base_name);
        return;
    case ITEM_UNIQUE:
        snprintf(line1, size1, "%s",
                 item->name1 > 0 && item->name1 <= unique_count ? uniques[item->name1 - 1].name : base_name);
        snprintf(line2, size2, "%s", base_name);
        return;
    case ITEM_SET:
        snprintf(line1, size1, "%s",
                 item->name1 > 0 && item->name1 <= set_item_count ? set_items[item->name1 - 1].name : base_name);
        snprintf(line2, size2, "%s", base_name);
        return;
    case ITEM_RUNEWORD: {
        const char *word = Sockets_RunewordName(item->name1 - 1);
        snprintf(line1, size1, "%s", word != NULL ? word : base_name);
        snprintf(line2, size2, "%s", base_name);
        return;
    }
    default:
        snprintf(line1, size1, "%s", base_name);
        return;
    }
}

void Affixes_QualityColor(const Item *item, uint8_t *r, uint8_t *g, uint8_t *b) {
    static const uint8_t colors[ITEM_QUALITY_COUNT][3] = {
        {0xFF, 0xFF, 0xFF}, // normal: white
        {0x69, 0x69, 0xFF}, // magic: blue
        {0xFF, 0xFF, 0x64}, // rare: yellow
        {0xC7, 0xB3, 0x77}, // unique: gold
        {0xC7, 0xB3, 0x77}, // runeword: gold too
        {0x00, 0xFF, 0x00}, // set: green
    };
    const int q = item->quality < ITEM_QUALITY_COUNT ? item->quality : 0;
    *r          = colors[q][0];
    *g          = colors[q][1];
    *b          = colors[q][2];
}

// The game's words for a class's skills: "to Amazon Skill Levels", its trees' "+%d to Bow and Crossbow Skills" (in the
// skill tree's page order) and "(Amazon Only)"
static const char *const class_skill_keys[CHARACTER_CLASS_COUNT]  = {"ModStr3a", "ModStr3d",  "ModStr3c", "ModStr3b",
                                                                     "ModStr3e", "ModStre8a", "ModStre8b"};
static const int         class_tab_keys[CHARACTER_CLASS_COUNT][3] = {
    {3,  2,  1 },
    {15, 14, 13},
    {8,  7,  9 },
    {6,  5,  4 },
    {11, 12, 10},
    {16, 17, 18},
    {19, 20, 21}
};
static const char *const class_only_keys[CHARACTER_CLASS_COUNT] = {"AmaOnly", "SorOnly", "NecOnly", "PalOnly",
                                                                   "BarOnly", "DruOnly", "AssOnly"};

static const char *Affixes__String(const char *key, const char *fallback) {
    const char *text = StringTable_Find(key);
    return text != NULL ? text : fallback;
}

// The skill's name and its class (-1: none) from its skills.txt row
static int Affixes__SkillName(const int row, char *name, const size_t size) {
    const SkillRow *skill = SkillData_ByRow(row);
    snprintf(name, size, "%s", skill != NULL ? skill->id : "a skill");
    for (int c = 0; c < CHARACTER_CLASS_COUNT && skill != NULL; c++) {
        const SkillClass *skills = Skills_ForClass((CharacterClass)c);
        const int         k      = Skills_Find(skills, skill->id);
        if (k >= 0) {
            snprintf(name, size, "%s", skills->skills[k].name);
            return c;
        }
    }
    return -1;
}

void Affixes_ModText(const ItemMod *mod, char *text, const size_t size) {
    if (mod->type >= MOD_TYPE_COUNT) {
        snprintf(text, size, "?");
        return;
    }
    const int cls = mod->param >= 0 && mod->param < CHARACTER_CLASS_COUNT ? mod->param : 0;
    switch (mod->type) {
    case MOD_CLASS_SKILLS:
        snprintf(text, size, "+%d %s", mod->value, Affixes__String(class_skill_keys[cls], "to Class Skills"));
        return;
    case MOD_TAB_SKILLS: {
        const int tab_class = mod->param / 3 >= 0 && mod->param / 3 < CHARACTER_CLASS_COUNT ? mod->param / 3 : 0;
        char      key[24];
        snprintf(key, sizeof(key), "StrSklTabItem%d", class_tab_keys[tab_class][mod->param % 3]);
        char line[96];
        snprintf(line, sizeof(line), Affixes__String(key, "+%d to Skills"), mod->value);
        snprintf(text, size, "%s %s", line, Affixes__String(class_only_keys[tab_class], ""));
        return;
    }
    case MOD_SKILL:
    case MOD_OSKILL: {
        char      name[64];
        const int owner = Affixes__SkillName(mod->param, name, sizeof(name));
        if (mod->type == MOD_SKILL && owner >= 0) {
            snprintf(text, size, "+%d to %s %s", mod->value, name, Affixes__String(class_only_keys[owner], ""));
        } else {
            snprintf(text, size, "+%d to %s", mod->value, name);
        }
        return;
    }
    case MOD_DEFENSE_PER_LEVEL:
    case MOD_MAX_DAMAGE_PER_LEVEL: {
        const Character *c = Character_Current();
        snprintf(text, size, mod_info[mod->type].text, mod->value * (c != NULL && c->level > 0 ? c->level : 1) / 8);
        return;
    }
    default:
        snprintf(text, size, mod_info[mod->type].text, mod->value);
        return;
    }
}

// What worn items add to a skill's level: All Skills, its class's, its tree's, the skill itself (only while you have a
// point in it; "oskill" gives it anyway)
int Affixes_SkillBonus(const Character *character, const int skill) {
    const SkillClass *skills = Skills_ForClass(character->class_id);
    if (skill < 0 || skill >= skills->count) {
        return 0;
    }
    const SkillRow *row     = SkillData_Get(skills->skills[skill].id);
    const int       tab     = ((int)character->class_id * 3) + skills->skills[skill].page - 1;
    const bool      learned = skill < CHARACTER_SKILLS && character->skill_levels[skill] > 0;
    int             bonus   = 0;
    int             given   = 0; // oskill
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item *item = &character->equipped[s];
        if (!Item_Works(item) || item->unidentified) {
            continue;
        }
        for (int m = 0; m < item->mod_count && m < ITEM_MODS_MAX; m++) {
            const ItemMod *mod = &item->mods[m];
            switch (mod->type) {
            case MOD_ALL_SKILLS:
                bonus += mod->value;
                break;
            case MOD_CLASS_SKILLS:
                bonus += mod->param == (int)character->class_id ? mod->value : 0;
                break;
            case MOD_TAB_SKILLS:
                bonus += mod->param == tab ? mod->value : 0;
                break;
            case MOD_SKILL:
                bonus += row != NULL && mod->param == row->row ? mod->value : 0;
                break;
            case MOD_OSKILL:
                given += row != NULL && mod->param == row->row ? mod->value : 0;
                break;
            default:
                break;
            }
        }
    }
    return (learned ? bonus : 0) + given;
}

const char *Affixes_InvFile(const Item *item) {
    Affixes__Load();
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return "";
    }
    if (item->quality == ITEM_SET && item->name1 > 0 && item->name1 <= set_item_count &&
        set_items[item->name1 - 1].inv_file[0] != '\0') {
        return set_items[item->name1 - 1].inv_file;
    }
    if (item->quality == ITEM_UNIQUE) {
        if (item->name1 > 0 && item->name1 <= unique_count && uniques[item->name1 - 1].inv_file[0] != '\0') {
            return uniques[item->name1 - 1].inv_file;
        }
        if (base->unique_inv_file[0] != '\0') {
            return base->unique_inv_file;
        }
    }
    return base->inv_file;
}

int Affixes_ItemTotal(const Item *item, const ItemModType type) {
    int total = 0;
    if (item->unidentified) {
        return 0; // not known yet: it does nothing
    }
    for (int m = 0; m < item->mod_count && m < ITEM_MODS_MAX; m++) {
        total += item->mods[m].type == type ? item->mods[m].value : 0;
    }
    return total;
}

// How much of one kind a property gives (fixed values: the smaller end of a range)
static int Affixes__PropValue(const Prop *prop, const ItemModType type) {
    int       pair  = -1;
    const int found = prop->code[0] != '\0' ? Affixes__Code(prop->code, &pair) : -1;
    if (found == (int)type) {
        return prop->min;
    }
    return pair == (int)type ? prop->max : 0;
}

// Set bonuses: for each set item worn, its own bonus for the other pieces worn, then the set's
static int Affixes__SetTotal(const Character *character, const ItemModType type) {
    if (set_item_count == 0) {
        return 0;
    }
    int total = 0;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item *item = &character->equipped[s];
        if (item->quality != ITEM_SET || item->name1 <= 0 || item->name1 > set_item_count || !Item_Works(item) ||
            item->unidentified) {
            continue;
        }
        const SetItem *piece = &set_items[item->name1 - 1];
        const int      worn  = Affixes_SetPiecesWorn(character, piece->set);
        for (int n = 0; n + 2 <= worn && n < 5; n++) {
            total += Affixes__PropValue(&piece->worn[n][0], type) + Affixes__PropValue(&piece->worn[n][1], type);
        }
    }
    // The sets' own (once per set: counted at its first piece)
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item *item = &character->equipped[s];
        if (item->quality != ITEM_SET || item->name1 <= 0 || item->name1 > set_item_count || !Item_Works(item) ||
            item->unidentified) {
            continue;
        }
        const char *name  = set_items[item->name1 - 1].set;
        bool        first = true;
        for (int e = 0; e < s && first; e++) {
            const Item *other = &character->equipped[e];
            first             = !(other->quality == ITEM_SET && other->name1 > 0 && other->name1 <= set_item_count &&
                      strcmp(set_items[other->name1 - 1].set, name) == 0);
        }
        if (!first) {
            continue;
        }
        const int worn = Affixes_SetPiecesWorn(character, name);
        for (int i = 0; i < item_set_count; i++) {
            const ItemSet *set = &item_sets[i];
            if (strcmp(set->name, name) != 0) {
                continue;
            }
            for (int n = 0; n + 2 <= worn && n < 4; n++) {
                total += Affixes__PropValue(&set->partial[n][0], type) + Affixes__PropValue(&set->partial[n][1], type);
            }
            for (int f = 0; f < set->full_count && worn >= set->pieces && set->pieces > 0; f++) {
                total += Affixes__PropValue(&set->full[f], type);
            }
        }
    }
    return total;
}

int Affixes_SetPiecesWorn(const Character *character, const char *set) {
    Affixes__Load();
    int worn = 0;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item *item = &character->equipped[s];
        worn += item->quality == ITEM_SET && item->name1 > 0 && item->name1 <= set_item_count && Item_Works(item) &&
                        !item->unidentified && strcmp(set_items[item->name1 - 1].set, set) == 0
                    ? 1
                    : 0;
    }
    return worn;
}

const char *Affixes_SetName(const Item *item) {
    Affixes__Load();
    return item->quality == ITEM_SET && item->name1 > 0 && item->name1 <= set_item_count
               ? set_items[item->name1 - 1].set
               : NULL;
}

int Affixes_Total(const Character *character, const ItemModType type) {
    int total = 0;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        if (Item_Works(&character->equipped[s])) { // (a broken item does nothing)
            total += Affixes_ItemTotal(&character->equipped[s], type);
        }
    }
    return total + Affixes__SetTotal(character, type);
}

// ---------------------------------------------------------------------------------------------------------------------
// Saving

// " <quality> <name1> <name2> <mods or ->" and, with sockets, " s<count>" and " g:<code>,<code>" (what's in them).
// Plain items without sockets: nothing.
void Affixes_Format(const Item *item, char *text, const size_t size) {
    text[0] = '\0';
    if (item->quality == ITEM_NORMAL && item->sockets == 0 && item->mod_count == 0 && item->max_durability == 0 &&
        !item->unidentified) {
        return;
    }
    size_t used = (size_t)snprintf(text, size, " %d %d %d ", item->quality, item->name1, item->name2);
    if (item->mod_count == 0) {
        used += (size_t)snprintf(text + used, size - used, "-");
    }
    for (int m = 0; m < item->mod_count && m < ITEM_MODS_MAX && used < size; m++) {
        used += (size_t)snprintf(text + used, size - used, "%s%s:%d", m > 0 ? "," : "",
                                 item->mods[m].type < MOD_TYPE_COUNT ? mod_info[item->mods[m].type].save : "?",
                                 item->mods[m].value);
        if (item->mods[m].param != 0 && used < size) {
            used += (size_t)snprintf(text + used, size - used, "@%d", item->mods[m].param); // which skill, tree, class
        }
    }
    if (item->sockets > 0 && used < size) {
        used += (size_t)snprintf(text + used, size - used, " s%d", item->sockets);
        for (int s = 0; s < item->socketed_count && s < ITEM_SOCKETS_MAX && used < size; s++) {
            used += (size_t)snprintf(text + used, size - used, "%s%s", s == 0 ? " g:" : ",", item->socketed[s]);
        }
    }
    if (item->max_durability > 0 && used < size) {
        used += (size_t)snprintf(text + used, size - used, " d%d/%d", item->durability, item->max_durability);
    }
    if (item->unidentified && used < size) {
        snprintf(text + used, size - used, " u");
    }
}

void Affixes_Parse(Item *item, const char *text) {
    int  quality = 0;
    int  name1   = 0;
    int  name2   = 0;
    char mods[512];
    mods[0]  = '\0';
    int read = 0;
    if (text == NULL || sscanf(text, "%d %d %d %511s%n", &quality, &name1, &name2, mods, &read) < 3 || quality < 0 ||
        quality >= ITEM_QUALITY_COUNT) {
        return;
    }
    // After the mods: sockets " s3 g:gcv,r01", durability " d12/24", unidentified " u"
    const char *rest = read > 0 ? text + read : "";
    char        word[128];
    int         used = 0;
    while (sscanf(rest, "%127s%n", word, &used) == 1) {
        rest  += used;
        int a  = 0;
        int b  = 0;
        if (word[0] == 's' && sscanf(word + 1, "%d", &a) == 1 && a > 0) {
            item->sockets = (uint8_t)(a > ITEM_SOCKETS_MAX ? ITEM_SOCKETS_MAX : a);
        } else if (strncmp(word, "g:", 2) == 0) {
            item->socketed_count = 0;
            for (const char *part = word + 2; *part != '\0' && item->socketed_count < ITEM_SOCKETS_MAX;) {
                const char *comma = strchr(part, ',');
                const int   len   = comma != NULL ? (int)(comma - part) : (int)strlen(part);
                snprintf(item->socketed[item->socketed_count++], ITEM_CODE_MAX, "%.*s", len < 7 ? len : 7, part);
                part += len + (comma != NULL ? 1 : 0);
            }
        } else if (word[0] == 'd' && sscanf(word + 1, "%d/%d", &a, &b) == 2 && b > 0) {
            item->max_durability = (int16_t)b;
            item->durability     = (int16_t)(a < 0 ? 0 : (a > b ? b : a));
        } else if (strcmp(word, "u") == 0) {
            item->unidentified = true;
        }
    }
    if (item->socketed_count > item->sockets) {
        item->socketed_count = item->sockets;
    }
    item->quality   = (uint8_t)quality;
    item->name1     = (int16_t)name1;
    item->name2     = (int16_t)name2;
    item->mod_count = 0;
    for (char *part = strtok(mods, ","); part != NULL && item->mod_count < ITEM_MODS_MAX; part = strtok(NULL, ",")) {
        char *colon = strrchr(part, ':');
        if (colon == NULL) {
            continue;
        }
        *colon = '\0';
        for (int t = 0; t < MOD_TYPE_COUNT; t++) {
            if (strcmp(mod_info[t].save, part) == 0) {
                const char *at                      = strchr(colon + 1, '@');
                item->mods[item->mod_count].type    = (uint8_t)t;
                item->mods[item->mod_count].param   = (int16_t)(at != NULL ? atoi(at + 1) : 0);
                item->mods[item->mod_count++].value = (int16_t)atoi(colon + 1);
                break;
            }
        }
    }
}

void Affixes_Release(void) {
    free(prefixes.rows);
    free(suffixes.rows);
    free(rare_prefixes);
    free(rare_suffixes);
    free(uniques);
    free(set_items);
    free(item_sets);
    set_items      = NULL;
    item_sets      = NULL;
    set_item_count = item_set_count = 0;
    memset(&prefixes, 0, sizeof(prefixes));
    memset(&suffixes, 0, sizeof(suffixes));
    rare_prefixes = rare_suffixes = NULL;
    uniques                       = NULL;
    rare_prefix_count = rare_suffix_count = unique_count = 0;
    loaded                                               = false;
}
