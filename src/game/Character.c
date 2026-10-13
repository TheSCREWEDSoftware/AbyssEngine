#include "Character.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#define EXCEL_EXPERIENCE  "/data/global/excel/experience.txt"
#define CHARACTER_MAGIC   "AbyssCharacter"
#define CHARACTER_VERSION 1
#define CHARACTER_EXT     ".abc"

static const struct {
    const char *name;
    const char *token;
    bool        expansion;
} classes[CHARACTER_CLASS_COUNT] = {
    {"Amazon",      "am", false},
    {"Sorceress",   "so", false},
    {"Necromancer", "ne", false},
    {"Paladin",     "pa", false},
    {"Barbarian",   "ba", false},
    {"Druid",       "dz", true },
    {"Assassin",    "ai", true },
};

static const char *stat_names[STAT_COUNT] = {"Strength", "Dexterity", "Vitality", "Energy"};

// What charstats.txt (and the class's starting weapon in weapons.txt) says about a class
typedef struct ClassInfo {
    int  stats[STAT_COUNT];
    int  stamina;
    int  hp_add;
    int  to_hit_factor;
    int  life_per_level; // in fourths
    int  stamina_per_level;
    int  mana_per_level;
    int  life_per_vitality;
    int  stamina_per_vitality;
    int  mana_per_energy;
    int  stat_per_level;
    int  mana_regen; // seconds
    int  run_drain;
    char weapon_name[32];
    int  weapon_min; // damage
    int  weapon_max;
    int  weapon_str_bonus; // % damage per 100 strength
    int  weapon_dex_bonus;
} ClassInfo;

static ClassInfo class_info[CHARACTER_CLASS_COUNT];
static uint32_t  experience_table[CHARACTER_MAX_LEVEL + 1]; // [level] = needed to be that level
static bool      tables_loaded = false;
static Character current;

// ---------------------------------------------------------------------------------------------------------------------
// Tables

static void Character__DefaultClass(ClassInfo *info) {
    // Used if the tables can't be read: roughly an Amazon
    memset(info, 0, sizeof(ClassInfo));
    const int stats[STAT_COUNT] = {20, 25, 20, 15};
    memcpy(info->stats, stats, sizeof(stats));
    info->stamina = 84, info->hp_add = 30, info->life_per_level = 8, info->stamina_per_level = 4;
    info->mana_per_level = 6, info->life_per_vitality = 12, info->stamina_per_vitality = 4, info->mana_per_energy = 6;
    info->stat_per_level = 5, info->mana_regen = 120, info->run_drain = 20;
    snprintf(info->weapon_name, sizeof(info->weapon_name), "Fists");
    info->weapon_min = 1, info->weapon_max = 2;
}

static void Character__LoadTables(void) {
    if (tables_loaded) {
        return;
    }
    tables_loaded = true;
    for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
        Character__DefaultClass(&class_info[c]);
    }

    DataTable *stats   = DataTable_Load(EXCEL_CHARSTATS);
    DataTable *weapons = DataTable_Load(EXCEL_WEAPONS);
    if (stats != NULL) {
#define COL(n) DataTable_GetColumnIndex(stats, n)
        const int c_class = COL("class");
        for (int row = 0; row < DataTable_GetRowCount(stats); row++) {
            int class_id = -1;
            for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
                if (strcmp(DataTable_GetString(stats, row, c_class), classes[c].name) == 0) {
                    class_id = c;
                }
            }
            if (class_id < 0) {
                continue;
            }
            ClassInfo *info             = &class_info[class_id];
            info->stats[STAT_STRENGTH]  = DataTable_GetInt(stats, row, COL("str"));
            info->stats[STAT_DEXTERITY] = DataTable_GetInt(stats, row, COL("dex"));
            info->stats[STAT_VITALITY]  = DataTable_GetInt(stats, row, COL("vit"));
            info->stats[STAT_ENERGY]    = DataTable_GetInt(stats, row, COL("int"));
            info->stamina               = DataTable_GetInt(stats, row, COL("stamina"));
            info->hp_add                = DataTable_GetInt(stats, row, COL("hpadd"));
            info->to_hit_factor         = DataTable_GetInt(stats, row, COL("ToHitFactor"));
            info->life_per_level        = DataTable_GetInt(stats, row, COL("LifePerLevel"));
            info->stamina_per_level     = DataTable_GetInt(stats, row, COL("StaminaPerLevel"));
            info->mana_per_level        = DataTable_GetInt(stats, row, COL("ManaPerLevel"));
            info->life_per_vitality     = DataTable_GetInt(stats, row, COL("LifePerVitality"));
            info->stamina_per_vitality  = DataTable_GetInt(stats, row, COL("StaminaPerVitality"));
            info->mana_per_energy       = DataTable_GetInt(stats, row, COL("ManaPerMagic"));
            info->stat_per_level        = DataTable_GetInt(stats, row, COL("StatPerLevel"));
            info->mana_regen            = DataTable_GetInt(stats, row, COL("ManaRegen"));
            info->run_drain             = DataTable_GetInt(stats, row, COL("RunDrain"));
            info->stat_per_level        = info->stat_per_level > 0 ? info->stat_per_level : 5;
            info->mana_regen            = info->mana_regen > 0 ? info->mana_regen : 120;

            // The first starting item that is a weapon
            for (int i = 1; i <= 10 && weapons != NULL; i++) {
                char column[8];
                snprintf(column, sizeof(column), "item%d", i);
                const char *code = DataTable_GetString(stats, row, COL(column));
                if (code[0] == '\0') {
                    continue;
                }
                const int w_code = DataTable_GetColumnIndex(weapons, "code");
                bool      found  = false;
                for (int w = 0; w < DataTable_GetRowCount(weapons) && !found; w++) {
                    if (strcmp(DataTable_GetString(weapons, w, w_code), code) != 0) {
                        continue;
                    }
                    found  = true;
                    int lo = DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "mindam"));
                    int hi = DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "maxdam"));
                    if (lo <= 0) { // two-handed (staves)
                        lo = DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "2handmindam"));
                        hi = DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "2handmaxdam"));
                    }
                    snprintf(info->weapon_name, sizeof(info->weapon_name), "%s",
                             DataTable_GetString(weapons, w, DataTable_GetColumnIndex(weapons, "name")));
                    info->weapon_min = lo > 0 ? lo : 1;
                    info->weapon_max = hi > info->weapon_min ? hi : info->weapon_min + 1;
                    info->weapon_str_bonus =
                        DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "StrBonus"));
                    info->weapon_dex_bonus =
                        DataTable_GetInt(weapons, w, DataTable_GetColumnIndex(weapons, "DexBonus"));
                }
                if (found) {
                    break;
                }
            }
        }
#undef COL
    }
    DataTable_Destroy(&stats);
    DataTable_Destroy(&weapons);

    // experience.txt: the row for level L holds what's needed to reach level L + 1
    for (int level = 0; level <= CHARACTER_MAX_LEVEL; level++) {
        experience_table[level] = level <= 1 ? 0 : (uint32_t)(500.0 * level * level * level / 8.0); // fallback
    }
    DataTable *table = DataTable_Load(EXCEL_EXPERIENCE);
    if (table != NULL) {
        const int c_level  = DataTable_GetColumnIndex(table, "Level");
        const int c_amazon = DataTable_GetColumnIndex(table, "Amazon");
        for (int row = 0; row < DataTable_GetRowCount(table); row++) {
            const char *level_text = DataTable_GetString(table, row, c_level);
            if (!isdigit((unsigned char)level_text[0])) {
                continue; // "MaxLvl"
            }
            const int level = atoi(level_text) + 1;
            if (level >= 1 && level <= CHARACTER_MAX_LEVEL) {
                experience_table[level] = (uint32_t)strtoul(DataTable_GetString(table, row, c_amazon), NULL, 10);
            }
        }
        DataTable_Destroy(&table);
    }
    experience_table[1] = 0;
}

void Character_Release(void) { tables_loaded = false; }

// ---------------------------------------------------------------------------------------------------------------------
// Classes and numbers

const char *Character_ClassName(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT ? classes[class_id].name : "?";
}

const char *Character_ClassToken(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT ? classes[class_id].token : "am";
}

bool Character_ClassIsExpansion(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT && classes[class_id].expansion;
}

const char *Character_StatName(const CharacterStat stat) { return stat < STAT_COUNT ? stat_names[stat] : "?"; }

static const ClassInfo *Character__Info(const Character *character) {
    Character__LoadTables();
    return &class_info[character->class_id < CHARACTER_CLASS_COUNT ? character->class_id : 0];
}

void Character_Create(Character *character, const char *name, const CharacterClass class_id, const bool expansion,
                      const bool hardcore) {
    memset(character, 0, sizeof(Character));
    snprintf(character->name, sizeof(character->name), "%s", name);
    character->class_id   = class_id < CHARACTER_CLASS_COUNT ? class_id : CHARACTER_AMAZON;
    character->expansion  = expansion || Character_ClassIsExpansion(class_id);
    character->hardcore   = hardcore;
    character->level      = 1;
    const ClassInfo *info = Character__Info(character);
    memcpy(character->stats, info->stats, sizeof(character->stats));
    character->life    = Character_MaxLife(character);
    character->mana    = Character_MaxMana(character);
    character->stamina = Character_MaxStamina(character);
}

// Starting value, plus so much (in fourths) per point above the class's start and per level after the first
static int Character__Grow(const int start, const int points, const int per_point, const int levels,
                           const int per_level) {
    return start + ((points * per_point) / 4) + ((levels * per_level) / 4);
}

int Character_MaxLife(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stats[STAT_VITALITY] + info->hp_add,
                           character->stats[STAT_VITALITY] - info->stats[STAT_VITALITY], info->life_per_vitality,
                           character->level - 1, info->life_per_level);
}

int Character_MaxMana(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stats[STAT_ENERGY], character->stats[STAT_ENERGY] - info->stats[STAT_ENERGY],
                           info->mana_per_energy, character->level - 1, info->mana_per_level);
}

int Character_MaxStamina(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stamina, character->stats[STAT_VITALITY] - info->stats[STAT_VITALITY],
                           info->stamina_per_vitality, character->level - 1, info->stamina_per_level);
}

int Character_Defense(const Character *character) { return character->stats[STAT_DEXTERITY] / 4; }

int Character_AttackRating(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    const int        ar   = (character->stats[STAT_DEXTERITY] * 5) - 35 + info->to_hit_factor;
    return ar > 1 ? ar : 1;
}

void Character_Damage(const Character *character, int *minimum, int *maximum) {
    const ClassInfo *info = Character__Info(character);
    // Strength (and for some weapons dexterity) adds a percentage to the weapon's damage
    const int str_bonus = info->weapon_str_bonus > 0 ? info->weapon_str_bonus : 100;
    const int bonus     = ((character->stats[STAT_STRENGTH] * str_bonus) / 100) +
                      ((character->stats[STAT_DEXTERITY] * info->weapon_dex_bonus) / 100);
    *minimum = info->weapon_min + ((info->weapon_min * bonus) / 100);
    *maximum = info->weapon_max + ((info->weapon_max * bonus) / 100);
    *minimum = *minimum > 0 ? *minimum : 1;
    *maximum = *maximum > *minimum ? *maximum : *minimum;
}

const char *Character_WeaponName(const Character *character) { return Character__Info(character)->weapon_name; }

int Character_ManaRegenSeconds(const Character *character) { return Character__Info(character)->mana_regen; }

int Character_RunDrain(const Character *character) { return Character__Info(character)->run_drain; }

uint32_t Character_ExperienceFor(const int level) {
    Character__LoadTables();
    if (level <= 1) {
        return 0;
    }
    return experience_table[level > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : level];
}

int Character_AddExperience(Character *character, const uint32_t experience) {
    const uint32_t cap = Character_ExperienceFor(CHARACTER_MAX_LEVEL);
    character->experience =
        (character->experience + experience > cap || character->experience + experience < character->experience)
            ? cap
            : character->experience + experience;
    int gained = 0;
    while (character->level < CHARACTER_MAX_LEVEL &&
           character->experience >= Character_ExperienceFor(character->level + 1)) {
        character->level++;
        character->stat_points  += Character__Info(character)->stat_per_level;
        character->skill_points += 1;
        gained++;
    }
    if (gained > 0) {
        character->life    = Character_MaxLife(character);
        character->mana    = Character_MaxMana(character);
        character->stamina = Character_MaxStamina(character);
    }
    return gained;
}

bool Character_SpendStatPoint(Character *character, const CharacterStat stat) {
    if (character->stat_points <= 0 || stat >= STAT_COUNT) {
        return false;
    }
    // Keep the same share of life/mana/stamina filled
    const int old_life    = Character_MaxLife(character);
    const int old_mana    = Character_MaxMana(character);
    const int old_stamina = Character_MaxStamina(character);
    character->stats[stat]++;
    character->stat_points--;
    character->life    += Character_MaxLife(character) - old_life;
    character->mana    += Character_MaxMana(character) - old_mana;
    character->stamina += Character_MaxStamina(character) - old_stamina;
    return true;
}

int Character_HitChance(const int attack_rating, const int attacker_level, const int defense,
                        const int defender_level) {
    const double ar     = attack_rating > 1 ? attack_rating : 1;
    const double def    = defense > 0 ? defense : 0;
    const double alvl   = attacker_level > 1 ? attacker_level : 1;
    const double dlvl   = defender_level > 1 ? defender_level : 1;
    const int    chance = (int)(200.0 * (ar / (ar + def)) * (alvl / (alvl + dlvl)));
    return chance < 5 ? 5 : (chance > 95 ? 95 : chance);
}

bool Character_IsValidName(const char *name, const char **reason) {
    const size_t length = strlen(name);
    const char  *why    = NULL;
    int          marks  = 0;
    if (length < 2) {
        why = "Your name must be at least 2 letters long.";
    } else if (length > CHARACTER_NAME_MAX - 1) {
        why = "Your name can be at most 15 letters long.";
    } else {
        for (size_t i = 0; i < length && why == NULL; i++) {
            const char c = name[i];
            if (c == '-' || c == '_') {
                marks++;
                if (i == 0 || i == length - 1) {
                    why = "Your name can't start or end with '-' or '_'.";
                }
            } else if (!isalpha((unsigned char)c)) {
                why = "Your name can only contain letters, and one '-' or '_'.";
            }
        }
        if (why == NULL && marks > 1) {
            why = "Your name can only contain one '-' or '_'.";
        }
    }
    if (reason != NULL) {
        *reason = why;
    }
    return why == NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Saving

static const char *Character__Folder(void) {
    static char folder[4096];
    snprintf(folder, sizeof(folder), "%s/save", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
    _mkdir(folder);
#else
    mkdir(folder, 0755);
#endif
    return folder;
}

static void Character__Path(const char *name, char *path, const size_t size) {
    snprintf(path, size, "%s/%s%s", Character__Folder(), name, CHARACTER_EXT);
}

bool Character_Save(Character *character) {
    char path[4200];
    Character__Path(character->name, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        LOG_WARN("Could not save '%s': %s", path, strerror(errno));
        return false;
    }
    character->last_played = (uint64_t)time(NULL);
    fprintf(file, "%s %d\n", CHARACTER_MAGIC, CHARACTER_VERSION);
    fprintf(file, "name %s\n", character->name);
    fprintf(file, "class %s\n", Character_ClassName(character->class_id));
    fprintf(file, "expansion %d\n", character->expansion ? 1 : 0);
    fprintf(file, "hardcore %d\n", character->hardcore ? 1 : 0);
    fprintf(file, "dead %d\n", character->dead ? 1 : 0);
    fprintf(file, "level %d\n", character->level);
    fprintf(file, "experience %lu\n", (unsigned long)character->experience);
    for (int s = 0; s < STAT_COUNT; s++) {
        fprintf(file, "%s %d\n", stat_names[s], character->stats[s]);
    }
    fprintf(file, "statpoints %d\n", character->stat_points);
    fprintf(file, "skillpoints %d\n", character->skill_points);
    fprintf(file, "life %d\n", character->life);
    fprintf(file, "mana %d\n", character->mana);
    fprintf(file, "stamina %d\n", character->stamina);
    fprintf(file, "act %d\n", character->act);
    fprintf(file, "lastplayed %llu\n", (unsigned long long)character->last_played);
    fprintf(file, "end\n");
    const bool ok = ferror(file) == 0;
    fclose(file);
    return ok;
}

static bool Character__Load(const char *path, Character *character) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    memset(character, 0, sizeof(Character));
    char line[256];
    int  version = 0;
    if (fgets(line, sizeof(line), file) == NULL || sscanf(line, CHARACTER_MAGIC " %d", &version) != 1 ||
        version > CHARACTER_VERSION) {
        fclose(file);
        return false;
    }
    character->level = 1;
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char key[32];
        char value[128] = "";
        if (sscanf(line, "%31s %127[^\n]", key, value) < 1 || strcmp(key, "end") == 0) {
            continue;
        }
        const int number = atoi(value);
        if (strcmp(key, "name") == 0) {
            snprintf(character->name, sizeof(character->name), "%.*s", (int)sizeof(character->name) - 1, value);
        } else if (strcmp(key, "class") == 0) {
            for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
                if (strcmp(value, classes[c].name) == 0) {
                    character->class_id = (CharacterClass)c;
                }
            }
        } else if (strcmp(key, "expansion") == 0) {
            character->expansion = number != 0;
        } else if (strcmp(key, "hardcore") == 0) {
            character->hardcore = number != 0;
        } else if (strcmp(key, "dead") == 0) {
            character->dead = number != 0;
        } else if (strcmp(key, "level") == 0) {
            character->level = number < 1 ? 1 : (number > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : number);
        } else if (strcmp(key, "experience") == 0) {
            character->experience = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "statpoints") == 0) {
            character->stat_points = number;
        } else if (strcmp(key, "skillpoints") == 0) {
            character->skill_points = number;
        } else if (strcmp(key, "life") == 0) {
            character->life = number;
        } else if (strcmp(key, "mana") == 0) {
            character->mana = number;
        } else if (strcmp(key, "stamina") == 0) {
            character->stamina = number;
        } else if (strcmp(key, "act") == 0) {
            character->act = number < 0 ? 0 : (number > 4 ? 4 : number);
        } else if (strcmp(key, "lastplayed") == 0) {
            character->last_played = strtoull(value, NULL, 10);
        } else {
            for (int s = 0; s < STAT_COUNT; s++) {
                if (strcmp(key, stat_names[s]) == 0) {
                    character->stats[s] = number;
                }
            }
        }
    }
    fclose(file);
    return character->name[0] != '\0';
}

bool Character_Exists(const char *name) {
    Character *list  = NULL;
    const int  count = Character_ListSaved(&list);
    bool       found = false;
    for (int i = 0; i < count && !found; i++) {
        // Names are unique regardless of case (they're file names on Windows)
        const char *a = list[i].name;
        const char *b = name;
        for (; *a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
        }
        found = *a == '\0' && *b == '\0';
    }
    free(list);
    return found;
}

bool Character_Delete(const char *name) {
    char path[4200];
    Character__Path(name, path, sizeof(path));
    return remove(path) == 0;
}

static void Character__Add(Character **list, int *count, int *capacity, const char *folder, const char *file_name) {
    const size_t len = strlen(file_name);
    if (len <= strlen(CHARACTER_EXT) || strcmp(file_name + len - strlen(CHARACTER_EXT), CHARACTER_EXT) != 0) {
        return;
    }
    char path[4400];
    snprintf(path, sizeof(path), "%s/%s", folder, file_name);
    Character character;
    if (!Character__Load(path, &character)) {
        return;
    }
    if (*count == *capacity) {
        *capacity        = *capacity > 0 ? *capacity * 2 : 8;
        Character *grown = realloc(*list, sizeof(Character) * (size_t)*capacity);
        FAIL_IF_NULL(grown);
        *list = grown;
    }
    (*list)[(*count)++] = character;
}

static int Character__CompareRecent(const void *a, const void *b) {
    const Character *x = a;
    const Character *y = b;
    return x->last_played < y->last_played ? 1 : (x->last_played > y->last_played ? -1 : strcmp(x->name, y->name));
}

int Character_ListSaved(Character **characters) {
    *characters          = NULL;
    int         count    = 0;
    int         capacity = 0;
    const char *folder   = Character__Folder();
#ifdef _WIN32
    char pattern[4200];
    snprintf(pattern, sizeof(pattern), "%s\\*%s", folder, CHARACTER_EXT);
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            Character__Add(characters, &count, &capacity, folder, found.cFileName);
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(folder);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            Character__Add(characters, &count, &capacity, folder, entry->d_name);
        }
        closedir(dir);
    }
#endif
    if (count > 1) {
        qsort(*characters, (size_t)count, sizeof(Character), Character__CompareRecent);
    }
    return count;
}

Character *Character_Current(void) { return &current; }

void Character_SetCurrent(const Character *character) { current = *character; }
