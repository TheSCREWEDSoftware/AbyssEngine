#include "Hirelings.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "Character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct HireRow {
    char title[32];
    char kind[40];
    int  version;
    int  id;
    int  act;
    int  difficulty;
    int  level;
    char seller[24]; // monstats id
    char monster[24];
    char name_first[16];
    char name_last[16];
    int  gold;
    int  exp_per_level;
    int  hp, hp_per_level, defense, def_per_level, ar, ar_per_level;
    int  dmg_min, dmg_max, dmg_per_level, resist, resist_per_level;
    char weapon[8];
} HireRow;

static HireRow *rows      = NULL;
static int      row_count = -1;

// monstats.txt row numbers (hireling.txt's Class and Seller) to their ids
static void Hirelings__Ids(HireRow *list, const int count, const int *classes, const int *sellers) {
    DataTable *monstats = DataTable_Load(EXCEL_MONSTATS);
    if (monstats == NULL) {
        return;
    }
    const int c_id = DataTable_GetColumnIndex(monstats, "Id");
    for (int i = 0; i < count; i++) {
        snprintf(list[i].monster, sizeof(list[i].monster), "%s", DataTable_GetString(monstats, classes[i], c_id));
        snprintf(list[i].seller, sizeof(list[i].seller), "%s", DataTable_GetString(monstats, sellers[i], c_id));
        for (char *c = list[i].monster; *c != '\0'; c++) {
            *c = (char)((*c >= 'A' && *c <= 'Z') ? *c - 'A' + 'a' : *c);
        }
    }
    DataTable_Destroy(&monstats);
}

static void Hirelings__Load(void) {
    if (row_count >= 0) {
        return;
    }
    row_count       = 0;
    DataTable *data = DataTable_Load("/data/global/excel/hireling.txt");
    if (data == NULL) {
        return;
    }
    const int total = DataTable_GetRowCount(data);
    rows            = calloc(total > 0 ? (size_t)total : 1, sizeof(HireRow));
    int *classes    = calloc(total > 0 ? (size_t)total : 1, sizeof(int));
    int *sellers    = calloc(total > 0 ? (size_t)total : 1, sizeof(int));
    FAIL_IF_NULL(rows);
    FAIL_IF_NULL(classes);
    FAIL_IF_NULL(sellers);
    // The expansion's rows when there are any (version 100), else the original game's (0)
    bool expansion = false;
    for (int r = 0; r < total; r++) {
        expansion = expansion || DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "Version")) == 100;
    }
#define STR(n) DataTable_GetString(data, r, DataTable_GetColumnIndex(data, n))
#define INT(n) DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, n))
    for (int r = 0; r < total; r++) {
        if (STR("Hireling")[0] == '\0' || INT("Version") != (expansion ? 100 : 0)) {
            continue;
        }
        HireRow *h = &rows[row_count];
        snprintf(h->title, sizeof(h->title), "%s", STR("Hireling"));
        snprintf(h->kind, sizeof(h->kind), "%s", STR("SubType"));
        h->version         = INT("Version");
        h->id              = INT("Id");
        h->act             = INT("Act");
        h->difficulty      = INT("Difficulty");
        h->level           = INT("Level");
        classes[row_count] = INT("Class");
        sellers[row_count] = INT("Seller");
        snprintf(h->name_first, sizeof(h->name_first), "%s", STR("NameFirst"));
        snprintf(h->name_last, sizeof(h->name_last), "%s", STR("NameLast"));
        h->gold             = INT("Gold");
        h->exp_per_level    = INT("Exp/Lvl");
        h->hp               = INT("HP");
        h->hp_per_level     = INT("HP/Lvl");
        h->defense          = INT("Defense");
        h->def_per_level    = INT("Def/Lvl");
        h->ar               = INT("AR");
        h->ar_per_level     = INT("AR/Lvl");
        h->dmg_min          = INT("Dmg-Min");
        h->dmg_max          = INT("Dmg-Max");
        h->dmg_per_level    = INT("Dmg/Lvl");
        h->resist           = INT("Resist");
        h->resist_per_level = INT("Resist/Lvl");
        snprintf(h->weapon, sizeof(h->weapon), "%s", STR("WType1"));
        row_count++;
    }
#undef STR
#undef INT
    DataTable_Destroy(&data);
    Hirelings__Ids(rows, row_count, classes, sellers);
    free(classes);
    free(sellers);
}

bool Hirelings_Sells(const char *npc_id) {
    Hirelings__Load();
    for (int r = 0; r < row_count; r++) {
        if (strcasecmp(rows[r].seller, npc_id) == 0) {
            return true;
        }
    }
    return false;
}

// The table's row for a kind at a level: the last one at or below it
static int Hirelings__RowFor(const int row, const int level) {
    int best = row;
    for (int r = 0; r < row_count; r++) {
        if (rows[r].id == rows[row].id && rows[r].level <= level && rows[r].level >= rows[best].level) {
            best = r;
        }
    }
    return best;
}

int Hirelings_Offers(const char *npc_id, const int difficulty, const int character_level, HireOffer *offers,
                     const int most) {
    Hirelings__Load();
    // The kinds they sell at this difficulty (one row each: the lowest level one)
    int kinds[32];
    int kind_count = 0;
    for (int r = 0; r < row_count && kind_count < 32; r++) {
        if (strcasecmp(rows[r].seller, npc_id) != 0 || rows[r].difficulty != difficulty + 1) {
            continue;
        }
        bool seen = false;
        for (int k = 0; k < kind_count && !seen; k++) {
            seen = rows[kinds[k]].id == rows[r].id;
        }
        if (!seen) {
            kinds[kind_count++] = r;
        }
    }
    int count = 0;
    for (int o = 0; o < most && kind_count > 0; o++) {
        HireOffer *offer = &offers[count++];
        offer->row       = kinds[rand() % kind_count];
        const HireRow *h = &rows[offer->row];
        // About your level, never below what the table starts at
        int level       = character_level - (rand() % 4);
        level           = level < h->level ? h->level : level;
        offer->level    = level;
        const int first = atoi(h->name_first + strcspn(h->name_first, "0123456789"));
        const int last  = atoi(h->name_last + strcspn(h->name_last, "0123456789"));
        offer->name     = first + (last > first ? rand() % (last - first + 1) : 0);
        offer->cost     = h->gold + (h->gold * level / 4);
        snprintf(offer->kind, sizeof(offer->kind), "%s", h->kind);
    }
    return count;
}

bool Hirelings_Stats(const int row, const int level, HireStats *stats) {
    Hirelings__Load();
    memset(stats, 0, sizeof(HireStats));
    if (row < 0 || row >= row_count) {
        return false;
    }
    const HireRow *h     = &rows[Hirelings__RowFor(row, level)];
    const int      over  = level > h->level ? level - h->level : 0;
    stats->hp            = h->hp + (h->hp_per_level * over);
    stats->defense       = h->defense + (h->def_per_level * over);
    stats->attack_rating = h->ar + (h->ar_per_level * over);
    stats->damage_min    = h->dmg_min + (h->dmg_per_level * over / 2);
    stats->damage_max    = h->dmg_max + (h->dmg_per_level * over / 2);
    stats->resist        = h->resist + (h->resist_per_level * over / 4);
    stats->ranged        = strcmp(h->weapon, "bow") == 0 || h->act == 3;
    if (strstr(h->kind, "Fire") != NULL) {
        stats->element = ELEMENT_FIRE;
    } else if (strstr(h->kind, "Ice") != NULL || strstr(h->kind, "Cold") != NULL) {
        stats->element = ELEMENT_COLD;
    } else if (strstr(h->kind, "Ltng") != NULL) {
        stats->element = ELEMENT_LIGHTNING;
    }
    return true;
}

const char *Hirelings_MonsterId(const int row) {
    Hirelings__Load();
    return row >= 0 && row < row_count ? rows[row].monster : NULL;
}

void Hirelings_Name(const int row, const int name, char *out, const int size) {
    Hirelings__Load();
    out[0] = '\0';
    if (row < 0 || row >= row_count) {
        return;
    }
    // The key's letters, then the number ("merc07", "merca205")
    char key[24];
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%.*s", (int)strcspn(rows[row].name_first, "0123456789"), rows[row].name_first);
    const int digits = (int)strlen(rows[row].name_first) - (int)strlen(prefix);
    snprintf(key, sizeof(key), "%s%0*d", prefix, digits, name);
    const char *found = StringTable_Find(key);
    snprintf(out, (size_t)size, "%s", found != NULL ? found : rows[row].title);
}

const char *Hirelings_Title(const int row) {
    Hirelings__Load();
    return row >= 0 && row < row_count ? rows[row].title : "Hireling";
}

int Hirelings_ReviveCost(const int level) {
    const int cost = level * level * 15;
    return cost < 50000 ? cost : 50000;
}

int Hirelings_Experience(const int row) {
    Hirelings__Load();
    return row >= 0 && row < row_count ? rows[row].exp_per_level : 100;
}

void Hirelings_Release(void) {
    free(rows);
    rows      = NULL;
    row_count = -1;
}
