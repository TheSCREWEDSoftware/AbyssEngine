#include "MonsterStats.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_MONPRESET    "/data/global/excel/MonPreset.txt"
#define EXCEL_SUPERUNIQUES "/data/global/excel/SuperUniques.txt"
#define EXCEL_MONLVL       "/data/global/excel/MonLvl.txt"

// Case-insensitive compare (strcasecmp isn't available on MSVC)
static int MonsterStats__Compare(const char *a, const char *b) {
    for (; *a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static DataTable *monstats     = NULL;
static DataTable *monpreset    = NULL;
static DataTable *superuniques = NULL;
static DataTable *monlvl       = NULL; // base life / damage per monster level
static bool       loaded       = false;

static int c_id;
static int c_velocity;
static int c_npc;
static int c_min_hp;
static int c_max_hp;
static int c_damage_min;
static int c_damage_max;
static int c_level;
static int c_defense;
static int c_attack_rating;
static int c_experience;

static void MonsterStats__Load(void) {
    if (loaded) {
        return;
    }
    loaded       = true;
    monstats     = DataTable_Load(EXCEL_MONSTATS);
    monpreset    = DataTable_Load(EXCEL_MONPRESET);
    superuniques = DataTable_Load(EXCEL_SUPERUNIQUES);
    monlvl       = DataTable_Load(EXCEL_MONLVL);
    if (monstats != NULL) {
        c_id            = DataTable_GetColumnIndex(monstats, "Id");
        c_velocity      = DataTable_GetColumnIndex(monstats, "Velocity");
        c_npc           = DataTable_GetColumnIndex(monstats, "npc");
        c_min_hp        = DataTable_GetColumnIndex(monstats, "minHP");
        c_max_hp        = DataTable_GetColumnIndex(monstats, "maxHP");
        c_damage_min    = DataTable_GetColumnIndex(monstats, "A1MinD");
        c_damage_max    = DataTable_GetColumnIndex(monstats, "A1MaxD");
        c_level         = DataTable_GetColumnIndex(monstats, "Level");
        c_defense       = DataTable_GetColumnIndex(monstats, "AC");
        c_attack_rating = DataTable_GetColumnIndex(monstats, "A1TH");
        c_experience    = DataTable_GetColumnIndex(monstats, "Exp");
    }
}

bool MonsterStats_Get(const char *monstats_id, MonsterStats *stats) {
    MonsterStats__Load();
    memset(stats, 0, sizeof(MonsterStats));
    if (monstats == NULL || monstats_id == NULL) {
        return false;
    }
    for (int row = 0; row < DataTable_GetRowCount(monstats); row++) {
        if (MonsterStats__Compare(DataTable_GetString(monstats, row, c_id), monstats_id) != 0) {
            continue;
        }
        // monstats gives life and damage as percentages of the base values for the monster's level (MonLvl.txt)
        int base_hp      = 0;
        int base_damage  = 0;
        int base_defense = 0;
        int base_to_hit  = 0;
        int base_exp     = 0;
        if (monlvl != NULL) {
            const int level = DataTable_GetInt(monstats, row, c_level);
            const int c_lvl = DataTable_GetColumnIndex(monlvl, "Level");
            const int c_hp  = DataTable_GetColumnIndex(monlvl, "HP");
            const int c_dm  = DataTable_GetColumnIndex(monlvl, "DM");
            for (int l = 0; l < DataTable_GetRowCount(monlvl); l++) {
                if (DataTable_GetInt(monlvl, l, c_lvl) == level) {
                    base_hp      = DataTable_GetInt(monlvl, l, c_hp);
                    base_damage  = DataTable_GetInt(monlvl, l, c_dm);
                    base_defense = DataTable_GetInt(monlvl, l, DataTable_GetColumnIndex(monlvl, "AC"));
                    base_to_hit  = DataTable_GetInt(monlvl, l, DataTable_GetColumnIndex(monlvl, "TH"));
                    base_exp     = DataTable_GetInt(monlvl, l, DataTable_GetColumnIndex(monlvl, "XP"));
                    break;
                }
            }
        }
        stats->level         = DataTable_GetInt(monstats, row, c_level);
        stats->hp_min        = base_hp * DataTable_GetInt(monstats, row, c_min_hp) / 100;
        stats->hp_max        = base_hp * DataTable_GetInt(monstats, row, c_max_hp) / 100;
        stats->damage_min    = base_damage * DataTable_GetInt(monstats, row, c_damage_min) / 100;
        stats->damage_max    = base_damage * DataTable_GetInt(monstats, row, c_damage_max) / 100;
        stats->defense       = base_defense * DataTable_GetInt(monstats, row, c_defense) / 100;
        stats->attack_rating = base_to_hit * DataTable_GetInt(monstats, row, c_attack_rating) / 100;
        stats->experience    = base_exp * DataTable_GetInt(monstats, row, c_experience) / 100;
        stats->velocity      = DataTable_GetInt(monstats, row, c_velocity);
        stats->npc           = DataTable_GetInt(monstats, row, c_npc) != 0;
        return true;
    }
    return false;
}

const char *MonsterStats_PresetMonster(const int act, const int index) {
    MonsterStats__Load();
    if (monpreset == NULL) {
        return NULL;
    }
    const int c_act   = DataTable_GetColumnIndex(monpreset, "Act");
    const int c_place = DataTable_GetColumnIndex(monpreset, "Place");
    int       seen    = 0;
    for (int row = 0; row < DataTable_GetRowCount(monpreset); row++) {
        if (DataTable_GetInt(monpreset, row, c_act) != act + 1) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        const char *place = DataTable_GetString(monpreset, row, c_place);
        if (place[0] == '\0' || strncmp(place, "place_", 6) == 0 || strncmp(place, "trap-", 5) == 0) {
            return NULL; // random spawn markers, not a specific monster
        }
        // Super uniques (Bishibosh, Griswold, ...) are listed by name: use their base monster
        if (superuniques != NULL) {
            const int c_name  = DataTable_GetColumnIndex(superuniques, "Superunique");
            const int c_class = DataTable_GetColumnIndex(superuniques, "Class");
            for (int s = 0; s < DataTable_GetRowCount(superuniques); s++) {
                if (MonsterStats__Compare(DataTable_GetString(superuniques, s, c_name), place) == 0) {
                    return DataTable_GetString(superuniques, s, c_class);
                }
            }
        }
        return place;
    }
    return NULL;
}

void MonsterStats_Release(void) {
    DataTable_Destroy(&monstats);
    DataTable_Destroy(&monpreset);
    DataTable_Destroy(&superuniques);
    DataTable_Destroy(&monlvl);
    loaded = false;
}
