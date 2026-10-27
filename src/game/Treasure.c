#include "Treasure.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "Affixes.h"
#include "Inventory.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_TREASURE "/data/global/excel/TreasureClassEx.txt"
#define TC_ITEMS       10
#define TC_DEPTH       12 // classes inside classes, this deep at most

typedef struct TreasureClass {
    char name[48];
    int picks; // > 0: this many picks, each maybe nothing; < 0: each item as many times as its chance, this many in all
    int no_drop;
    char items[TC_ITEMS][48];
    int  chances[TC_ITEMS];
    int  count;
} TreasureClass;

static TreasureClass *classes     = NULL;
static int            class_count = 0;
static bool           loaded      = false;

static void Treasure__Load(void) {
    if (loaded) {
        return;
    }
    loaded       = true;
    DataTable *t = DataTable_Load(EXCEL_TREASURE);
    if (t == NULL) {
        LOG_WARN("No treasure classes: nothing will drop");
        return;
    }
    const int rows = DataTable_GetRowCount(t);
    classes        = calloc((size_t)(rows > 0 ? rows : 1), sizeof(TreasureClass));
    FAIL_IF_NULL(classes);
    const int c_name   = DataTable_GetColumnIndex(t, "Treasure Class");
    const int c_picks  = DataTable_GetColumnIndex(t, "Picks");
    const int c_nodrop = DataTable_GetColumnIndex(t, "NoDrop");
    for (int row = 0; row < rows; row++) {
        const char *name = DataTable_GetString(t, row, c_name);
        if (name[0] == '\0') {
            continue;
        }
        TreasureClass *tc = &classes[class_count++];
        snprintf(tc->name, sizeof(tc->name), "%s", name);
        tc->picks   = DataTable_GetInt(t, row, c_picks);
        tc->no_drop = DataTable_GetInt(t, row, c_nodrop);
        for (int i = 0; i < TC_ITEMS; i++) {
            char column[16];
            snprintf(column, sizeof(column), "Item%d", i + 1);
            const char *item = DataTable_GetString(t, row, DataTable_GetColumnIndex(t, column));
            snprintf(column, sizeof(column), "Prob%d", i + 1);
            const int chance = DataTable_GetInt(t, row, DataTable_GetColumnIndex(t, column));
            if (item[0] != '\0' && chance > 0) {
                snprintf(tc->items[tc->count], sizeof(tc->items[0]), "%s", item);
                tc->chances[tc->count++] = chance;
            }
        }
    }
    DataTable_Destroy(&t);
    LOG_INFO("Treasure: %d classes", class_count);
}

static const TreasureClass *Treasure__Find(const char *name) {
    Treasure__Load();
    for (int c = 0; c < class_count; c++) {
        if (strcmp(classes[c].name, name) == 0) {
            return &classes[c];
        }
    }
    return NULL;
}

bool Treasure_Exists(const char *treasure_class) {
    return treasure_class != NULL && treasure_class[0] != '\0' && Treasure__Find(treasure_class) != NULL;
}

static int Treasure__Roll(const int low, const int high) {
    return high > low ? low + (rand() % (high - low + 1)) : low;
}

// The classes the game makes from the items: "weap12" = a spawnable weapon of item level 10 to 12
static bool Treasure__Auto(const char *name, Item *item) {
    static const char *kinds[] = {"weap", "armo", "mele", "bow"};
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        const size_t length = strlen(kinds[k]);
        if (strncmp(name, kinds[k], length) != 0 || !isdigit((unsigned char)name[length])) {
            continue;
        }
        const int top    = atoi(name + length);
        int       found  = 0;
        int       chosen = -1;
        for (int i = 0; i < Items_Count(); i++) { // one of them at random (reservoir: each as likely)
            const ItemBase *base = Items_Get(i);
            if (base->spawnable && base->level > top - 3 && base->level <= top && Items_IsA(base, kinds[k]) &&
                rand() % ++found == 0) {
                chosen = i;
            }
        }
        if (chosen < 0) {
            return false;
        }
        *item = Inventory_NewItem(Items_Get(chosen)->code);
        return true;
    }
    return false;
}

static void Treasure__Pick(const char *name, const int level, Item *items, int *count, const int depth) {
    if (*count >= TREASURE_MAX_DROPS || depth > TC_DEPTH || name[0] == '\0') {
        return;
    }
    const TreasureClass *tc = Treasure__Find(name);
    if (tc != NULL) {
        if (tc->picks < 0) {
            // Each item as many times as its chance, -picks in all (bosses' guaranteed drops)
            int left = -tc->picks;
            for (int i = 0; i < tc->count && left > 0; i++) {
                for (int n = 0; n < tc->chances[i] && left > 0; n++, left--) {
                    Treasure__Pick(tc->items[i], level, items, count, depth + 1);
                }
            }
            return;
        }
        int total = tc->no_drop;
        for (int i = 0; i < tc->count; i++) {
            total += tc->chances[i];
        }
        for (int p = 0; p < (tc->picks > 0 ? tc->picks : 1) && total > 0; p++) {
            int roll = rand() % total;
            if (roll < tc->no_drop) {
                continue; // nothing this time
            }
            roll -= tc->no_drop;
            for (int i = 0; i < tc->count; i++) {
                if (roll < tc->chances[i]) {
                    Treasure__Pick(tc->items[i], level, items, count, depth + 1);
                    break;
                }
                roll -= tc->chances[i];
            }
        }
        return;
    }
    Item item;
    if (Treasure__Auto(name, &item)) {
        Affixes_Roll(&item, level); // magic, rare or unique, maybe
        items[(*count)++] = item;
        return;
    }
    const ItemBase *base = Items_Find(name);
    if (base == NULL) {
        return; // not one we know
    }
    item = Inventory_NewItem(name);
    if (strcmp(name, "gld") == 0) {
        const int l   = level > 0 ? level : 1;
        item.quantity = Treasure__Roll(l, (l * 8) + 10); // gold: more from tougher monsters
    } else if (base->stackable && base->max_stack > 1) {
        item.quantity = Treasure__Roll((base->max_stack / 2) > 0 ? base->max_stack / 2 : 1, base->max_stack);
    } else {
        Affixes_Roll(&item, level); // rings and amulets, armor and weapons named in the class itself
    }
    items[(*count)++] = item;
}

int Treasure_Roll(const char *treasure_class, const int level, Item *items) {
    int count = 0;
    if (treasure_class != NULL) {
        Treasure__Pick(treasure_class, level, items, &count, 0);
    }
    return count;
}

void Treasure_Release(void) {
    free(classes);
    classes     = NULL;
    class_count = 0;
    loaded      = false;
}
