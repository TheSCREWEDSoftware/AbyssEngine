#include "Sockets.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "Affixes.h"
#include "Items.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_GEMS  "/data/global/excel/gems.txt"
#define EXCEL_RUNES "/data/global/excel/Runes.txt"

typedef struct SocketProp {
    char code[16];
    char param[32];
    int  min;
    int  max;
} SocketProp;

// What a gem or rune does in each kind of item
enum { GEM_WEAPON, GEM_HELM, GEM_SHIELD, GEM_KINDS };

typedef struct Gem {
    char       code[ITEM_CODE_MAX];
    SocketProp props[GEM_KINDS][3];
    int        prop_count[GEM_KINDS];
} Gem;

typedef struct Runeword {
    char       name[48];
    char       itypes[6][8];
    int        itype_count;
    char       etypes[3][8];
    int        etype_count;
    char       runes[6][ITEM_CODE_MAX];
    int        rune_count;
    SocketProp props[7];
    int        prop_count;
} Runeword;

static Gem      *gems          = NULL;
static int       gem_count     = -1;
static Runeword *runewords     = NULL;
static int       runeword_rows = 0; // rows of Runes.txt (runewords[row]); rune_count 0: not one that works

static void Sockets__Load(void) {
    if (gem_count >= 0) {
        return;
    }
    gem_count       = 0;
    DataTable *data = DataTable_Load(EXCEL_GEMS);
    if (data != NULL) {
        const int rows = DataTable_GetRowCount(data);
        gems           = calloc(rows > 0 ? (size_t)rows : 1, sizeof(Gem));
        FAIL_IF_NULL(gems);
        static const char *kinds[GEM_KINDS] = {"weapon", "helm", "shield"};
        for (int r = 0; r < rows; r++) {
            const char *code = DataTable_GetString(data, r, DataTable_GetColumnIndex(data, "code"));
            if (code[0] == '\0') {
                continue;
            }
            Gem *gem = &gems[gem_count++];
            snprintf(gem->code, sizeof(gem->code), "%s", code);
            for (int k = 0; k < GEM_KINDS; k++) {
                for (int p = 1; p <= 3; p++) {
                    char column[32];
                    snprintf(column, sizeof(column), "%sMod%dCode", kinds[k], p);
                    const char *prop = DataTable_GetString(data, r, DataTable_GetColumnIndex(data, column));
                    if (prop[0] == '\0') {
                        continue;
                    }
                    SocketProp *out = &gem->props[k][gem->prop_count[k]++];
                    snprintf(out->code, sizeof(out->code), "%s", prop);
                    snprintf(column, sizeof(column), "%sMod%dParam", kinds[k], p);
                    snprintf(out->param, sizeof(out->param), "%s",
                             DataTable_GetString(data, r, DataTable_GetColumnIndex(data, column)));
                    snprintf(column, sizeof(column), "%sMod%dMin", kinds[k], p);
                    out->min = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, column));
                    snprintf(column, sizeof(column), "%sMod%dMax", kinds[k], p);
                    out->max = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, column));
                }
            }
        }
        DataTable_Destroy(&data);
    }
    data = DataTable_Load(EXCEL_RUNES);
    if (data == NULL) {
        return; // the original game: no runewords
    }
    runeword_rows = DataTable_GetRowCount(data);
    runewords     = calloc(runeword_rows > 0 ? (size_t)runeword_rows : 1, sizeof(Runeword));
    FAIL_IF_NULL(runewords);
#define STR(name) DataTable_GetString(data, r, DataTable_GetColumnIndex(data, name))
    for (int r = 0; r < runeword_rows; r++) {
        Runeword *word = &runewords[r];
        if (DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "complete")) != 1) {
            continue; // the ones the game never finished
        }
        const char *name = StringTable_Find(STR("Name"));
        snprintf(word->name, sizeof(word->name), "%s", name != NULL ? name : STR("Rune Name"));
        char column[32];
        for (int i = 1; i <= 6; i++) {
            snprintf(column, sizeof(column), "itype%d", i);
            if (STR(column)[0] != '\0') {
                snprintf(word->itypes[word->itype_count++], 8, "%.7s", STR(column));
            }
            snprintf(column, sizeof(column), "Rune%d", i);
            if (STR(column)[0] != '\0') {
                snprintf(word->runes[word->rune_count++], ITEM_CODE_MAX, "%.7s", STR(column));
            }
        }
        for (int i = 1; i <= 3; i++) {
            snprintf(column, sizeof(column), "etype%d", i);
            if (STR(column)[0] != '\0') {
                snprintf(word->etypes[word->etype_count++], 8, "%.7s", STR(column));
            }
        }
        for (int i = 1; i <= 7; i++) {
            snprintf(column, sizeof(column), "T1Code%d", i);
            if (STR(column)[0] == '\0') {
                continue;
            }
            SocketProp *prop = &word->props[word->prop_count++];
            snprintf(prop->code, sizeof(prop->code), "%s", STR(column));
            snprintf(column, sizeof(column), "T1Param%d", i);
            snprintf(prop->param, sizeof(prop->param), "%s", STR(column));
            snprintf(column, sizeof(column), "T1Min%d", i);
            prop->min = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, column));
            snprintf(column, sizeof(column), "T1Max%d", i);
            prop->max = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, column));
        }
    }
#undef STR
    DataTable_Destroy(&data);
}

static const Gem *Sockets__Gem(const char *code) {
    Sockets__Load();
    for (int g = 0; g < gem_count; g++) {
        if (strcmp(gems[g].code, code) == 0) {
            return &gems[g];
        }
    }
    return NULL;
}

bool Sockets_CanGoIn(const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    return base != NULL && (Sockets__Gem(item->code) != NULL || Items_IsA(base, "jewl"));
}

// Which of a gem's three sets of magic an item gets; -1: it can't have sockets
static int Sockets__Kind(const ItemBase *base) {
    if (base->kind == ITEM_KIND_WEAPON) {
        return GEM_WEAPON;
    }
    if (Items_IsA(base, "shld")) {
        return GEM_SHIELD;
    }
    if (Items_IsA(base, "helm") || Items_IsA(base, "tors")) {
        return GEM_HELM;
    }
    return -1;
}

static bool Sockets__Fits(const ItemBase *base, const Runeword *word) {
    bool any = false;
    for (int i = 0; i < word->itype_count && !any; i++) {
        any = Items_IsA(base, word->itypes[i]);
    }
    for (int e = 0; e < word->etype_count && any; e++) {
        any = !Items_IsA(base, word->etypes[e]);
    }
    return any;
}

// A plain item, every socket filled with runes in a runeword's order: it becomes that runeword
static void Sockets__Runeword(Item *item, const ItemBase *base) {
    if (item->quality != ITEM_NORMAL || item->socketed_count != item->sockets) {
        return;
    }
    for (int r = 0; r < runeword_rows; r++) {
        const Runeword *word = &runewords[r];
        if (word->rune_count != item->socketed_count || word->rune_count == 0 || !Sockets__Fits(base, word)) {
            continue;
        }
        bool same = true;
        for (int s = 0; s < word->rune_count && same; s++) {
            same = strcmp(word->runes[s], item->socketed[s]) == 0;
        }
        if (!same) {
            continue;
        }
        item->quality = ITEM_RUNEWORD;
        item->name1   = (int16_t)(r + 1);
        for (int p = 0; p < word->prop_count; p++) {
            Affixes_AddPropertyParam(item, word->props[p].code, word->props[p].param, word->props[p].min,
                                     word->props[p].max);
        }
        return;
    }
}

bool Sockets_Insert(Item *target, const Item *gem, const char **why) {
    Sockets__Load();
    const ItemBase *base     = Items_Find(target->code);
    const ItemBase *gem_base = Items_Find(gem->code);
    if (base == NULL || gem_base == NULL || !Sockets_CanGoIn(gem)) {
        *why = "That doesn't go in sockets.";
        return false;
    }
    if (target->sockets == 0) {
        *why = "That item has no sockets.";
        return false;
    }
    if (target->socketed_count >= target->sockets) {
        *why = "Its sockets are full.";
        return false;
    }
    const int kind = Sockets__Kind(base);
    if (kind < 0) {
        *why = "That item can't have sockets.";
        return false;
    }
    snprintf(target->socketed[target->socketed_count++], ITEM_CODE_MAX, "%s", gem->code);
    const Gem *data = Sockets__Gem(gem->code);
    if (data != NULL) {
        for (int p = 0; p < data->prop_count[kind]; p++) {
            Affixes_AddPropertyParam(target, data->props[kind][p].code, data->props[kind][p].param,
                                     data->props[kind][p].min, data->props[kind][p].max);
        }
    } else {
        for (int m = 0; m < gem->mod_count; m++) { // a jewel: its own magic
            Affixes_AddMod(target, gem->mods[m].type, gem->mods[m].value);
        }
    }
    Sockets__Runeword(target, base);
    return true;
}

void Sockets_Roll(Item *item, const int item_level) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || base->max_sockets <= 0 || Sockets__Kind(base) < 0 || item->sockets > 0) {
        return;
    }
    // The game's caps by item level (itemtypes.txt MaxSock1/25/40 are about these for most things)
    int cap = item_level < 26 ? 3 : (item_level < 40 ? 4 : 6);
    cap     = cap < base->max_sockets ? cap : base->max_sockets;
    if (item->quality == ITEM_NORMAL && rand() % 8 == 0) {
        item->sockets = (uint8_t)(1 + (rand() % cap));
    } else if (item->quality == ITEM_MAGIC && rand() % 20 == 0) {
        item->sockets = (uint8_t)(1 + (rand() % (cap < 2 ? cap : 2)));
    }
}

const char *Sockets_RunewordName(const int row) {
    Sockets__Load();
    return row >= 0 && row < runeword_rows && runewords[row].name[0] != '\0' ? runewords[row].name : NULL;
}

void Sockets_Release(void) {
    free(gems);
    free(runewords);
    gems          = NULL;
    runewords     = NULL;
    gem_count     = -1;
    runeword_rows = 0;
}
