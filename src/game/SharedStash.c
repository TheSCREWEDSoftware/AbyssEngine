#include "SharedStash.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "Affixes.h"
#include "Inventory.h"
#include "Items.h"
#include <stdio.h>
#include <string.h>

typedef struct SharedPage {
    bool          loaded;
    InventoryItem items[STASH_MAX];
    int           count;
    InventoryItem saved[STASH_MAX]; // as last saved (to know when it changed)
    int           saved_count;
    int           gold; // the shared gold
    int           saved_gold;
} SharedPage;

static SharedPage pages[2]; // softcore, hardcore
static bool       swapped = false;

bool SharedStash_Allowed(const Character *character) { return character != NULL && character->expansion; }

static void SharedStash__Path(const int mode, char *path, const size_t size) {
    snprintf(path, size, "%s/save/%s", AbyssConfiguration_GetConfigFolder(),
             mode == 1 ? "shared_hc.stash" : "shared.stash");
}

// "item <x> <y> <code> <defense> <quantity><magic>" a line, like a character's stash
static void SharedStash__Load(const int mode) {
    SharedPage *page = &pages[mode];
    page->loaded     = true;
    page->count      = 0;
    char path[4200];
    SharedStash__Path(mode, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        char line[600];
        while (fgets(line, sizeof(line), file) != NULL && page->count < STASH_MAX) {
            line[strcspn(line, "\r\n")] = '\0';
            if (sscanf(line, "gold %d", &page->gold) == 1) {
                page->gold = page->gold > 0 ? page->gold : 0;
                continue;
            }
            Item item;
            memset(&item, 0, sizeof(item));
            char code[ITEM_CODE_MAX];
            int  x    = 0;
            int  y    = 0;
            int  used = 0;
            if (sscanf(line, "item %d %d %7s %d %d%n", &x, &y, code, &item.defense, &item.quantity, &used) != 5 ||
                Items_Find(code) == NULL) {
                continue;
            }
            snprintf(item.code, sizeof(item.code), "%s", code);
            Affixes_Parse(&item, line + used);
            InventoryItem *slot = &page->items[page->count++];
            slot->item          = item;
            slot->x             = x;
            slot->y             = y;
            slot->page          = Inventory_LinePage(line + used);
        }
        fclose(file);
    }
    memcpy(page->saved, page->items, sizeof(page->items));
    page->saved_count = page->count;
    page->saved_gold  = page->gold;
}

static void SharedStash__Save(const int mode) {
    SharedPage *page = &pages[mode];
    char        path[4200];
    SharedStash__Path(mode, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        LOG_WARN("Could not save the shared stash '%s'", path);
        return;
    }
    fprintf(file, "gold %d\n", page->gold);
    for (int i = 0; i < page->count; i++) {
        const InventoryItem *it = &page->items[i];
        char                 magic[400];
        Affixes_Format(&it->item, magic, sizeof(magic));
        fprintf(file, "item %d %d %s %d %d%s P%d\n", it->x, it->y, it->item.code, it->item.defense, it->item.quantity,
                magic, it->page);
    }
    fclose(file);
    memcpy(page->saved, page->items, sizeof(page->items));
    page->saved_count = page->count;
    page->saved_gold  = page->gold;
}

void SharedStash_Swap(Character *character) {
    const int   mode = character->hardcore ? 1 : 0;
    SharedPage *page = &pages[mode];
    if (!page->loaded) {
        SharedStash__Load(mode);
    }
    static InventoryItem held[STASH_MAX];
    const int            count = character->stash_count;
    memcpy(held, character->stash, sizeof(held));
    memcpy(character->stash, page->items, sizeof(held));
    character->stash_count = page->count;
    memcpy(page->items, held, sizeof(held));
    page->count           = count;
    const int gold        = character->stash_gold;
    character->stash_gold = page->gold;
    page->gold            = gold;
    swapped               = !swapped;
    if (!swapped && (page->count != page->saved_count || page->gold != page->saved_gold ||
                     memcmp(page->items, page->saved, sizeof(InventoryItem) * (size_t)page->count) != 0)) {
        SharedStash__Save(mode); // (back in its place, and changed)
    }
}

void SharedStash_Release(void) { memset(pages, 0, sizeof(pages)); }
