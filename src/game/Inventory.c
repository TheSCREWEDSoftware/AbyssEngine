#include "Inventory.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Item Inventory_NewItem(const char *code) {
    Item item;
    memset(&item, 0, sizeof(item));
    const ItemBase *base = Items_Find(code);
    if (base == NULL) {
        return item;
    }
    snprintf(item.code, sizeof(item.code), "%s", code);
    if (base->max_defense > 0) {
        const int range = base->max_defense - base->min_defense;
        item.defense    = base->min_defense + (range > 0 ? rand() % (range + 1) : 0);
    }
    item.quantity = base->stackable && base->max_stack > 0 ? base->max_stack : 1;
    return item;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pages

static int page_count   = 1;
static int current_page = 0;

void Inventory_SetPages(const int count) {
    page_count   = count < 1 ? 1 : (count > INVENTORY_PAGES_MAX ? INVENTORY_PAGES_MAX : count);
    current_page = current_page < page_count ? current_page : page_count - 1;
}

int  Inventory_Pages(void) { return page_count; }
void Inventory_SetPage(const int page) { current_page = page < 0 ? 0 : (page >= page_count ? page_count - 1 : page); }
int  Inventory_Page(void) { return current_page; }

int Inventory_PagesUsed(const Character *character) {
    int used = 1;
    for (int i = 0; i < character->inventory_count; i++) {
        used = character->inventory[i].page + 1 > used ? character->inventory[i].page + 1 : used;
    }
    return used;
}

bool Inventory_PlaceOnPage(Character *character, const Item *item, const int x, const int y, const int page) {
    const int shown = current_page;
    current_page    = page < 0 ? 0 : (page >= INVENTORY_PAGES_MAX ? INVENTORY_PAGES_MAX - 1 : page);
    const bool done = Inventory_Place(character, item, x, y);
    current_page    = shown;
    return done;
}

// ---------------------------------------------------------------------------------------------------------------------
// The grid

int Inventory_ItemAt(const Character *character, const int cell_x, const int cell_y) {
    for (int i = 0; i < character->inventory_count; i++) {
        const InventoryItem *it = &character->inventory[i];
        if (it->page != current_page) {
            continue;
        }
        const ItemBase *base = Items_Find(it->item.code);
        const int       w    = base != NULL ? base->width : 1;
        const int       h    = base != NULL ? base->height : 1;
        if (cell_x >= it->x && cell_x < it->x + w && cell_y >= it->y && cell_y < it->y + h) {
            return i;
        }
    }
    return -1;
}

int Inventory_Covered(const Character *character, const ItemBase *base, const int x, const int y) {
    if (base == NULL || x < 0 || y < 0 || x + base->width > INVENTORY_W || y + base->height > INVENTORY_H) {
        return -2;
    }
    int found = -1;
    for (int cy = y; cy < y + base->height; cy++) {
        for (int cx = x; cx < x + base->width; cx++) {
            const int at = Inventory_ItemAt(character, cx, cy);
            if (at >= 0 && found >= 0 && at != found) {
                return -2;
            }
            found = at >= 0 ? at : found;
        }
    }
    return found;
}

bool Inventory_FreeAt(const Character *character, const ItemBase *base, const int x, const int y) {
    return Inventory_Covered(character, base, x, y) == -1;
}

bool Inventory_Place(Character *character, const Item *item, const int x, const int y) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || character->inventory_count >= INVENTORY_MAX || !Inventory_FreeAt(character, base, x, y)) {
        return false;
    }
    InventoryItem *slot = &character->inventory[character->inventory_count++];
    slot->item          = *item;
    slot->x             = x;
    slot->y             = y;
    slot->page          = current_page;
    return true;
}

bool Inventory_Add(Character *character, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return false;
    }
    // Page by page, column by column from the left, top to bottom, like the game fills it
    for (int page = 0; page < page_count; page++) {
        for (int x = 0; x + base->width <= INVENTORY_W; x++) {
            for (int y = 0; y + base->height <= INVENTORY_H; y++) {
                if (Inventory_PlaceOnPage(character, item, x, y, page)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Inventory_Remove(Character *character, const int index) {
    if (index < 0 || index >= character->inventory_count) {
        return;
    }
    for (int i = index; i + 1 < character->inventory_count; i++) {
        character->inventory[i] = character->inventory[i + 1];
    }
    character->inventory_count--;
}

// ---------------------------------------------------------------------------------------------------------------------
// Wearing

bool Inventory_MeetsRequirements(const Character *character, const ItemBase *base) {
    return base != NULL && Character_Stat(character, STAT_STRENGTH) >= base->required_strength &&
           Character_Stat(character, STAT_DEXTERITY) >= base->required_dexterity &&
           character->level >= base->required_level;
}

static bool Inventory__IsTwoHanded(const Item *item) {
    const ItemBase *base = item->code[0] != '\0' ? Items_Find(item->code) : NULL;
    return base != NULL && base->kind == ITEM_KIND_WEAPON && base->two_handed;
}

bool Inventory_CanEquip(const Character *character, const Item *item, const EquipSlot slot, const char **why) {
    const ItemBase *base   = Items_Find(item->code);
    const char     *reason = NULL;
    if (base == NULL || slot >= EQUIP_COUNT || !base->slots[slot]) {
        reason = "That doesn't go there.";
    } else if (!Inventory_MeetsRequirements(character, base)) {
        reason = "You can't use that yet.";
    } else if (slot == EQUIP_RIGHT_ARM || slot == EQUIP_LEFT_ARM) {
        const EquipSlot other      = slot == EQUIP_RIGHT_ARM ? EQUIP_LEFT_ARM : EQUIP_RIGHT_ARM;
        const Item     *other_item = &character->equipped[other];
        const ItemBase *other_base = other_item->code[0] != '\0' ? Items_Find(other_item->code) : NULL;
        if (Inventory__IsTwoHanded(item) && other_base != NULL) {
            reason = "A two-handed weapon needs both hands free.";
        } else if (Inventory__IsTwoHanded(other_item)) {
            reason = "Your other hand holds a two-handed weapon.";
        } else if (other_base != NULL && other_base->kind == base->kind && base->kind != ITEM_KIND_WEAPON) {
            reason = "You can only hold one shield.";
        } else if (other_base != NULL && other_base->kind == ITEM_KIND_WEAPON && base->kind == ITEM_KIND_WEAPON) {
            // Two weapons: Barbarians, and Assassins with a claw in each hand
            const bool claws = strncmp(base->type, "h2h", 3) == 0 && strncmp(other_base->type, "h2h", 3) == 0;
            if (!(character->class_id == CHARACTER_BARBARIAN || (character->class_id == CHARACTER_ASSASSIN && claws))) {
                reason = "Only Barbarians can hold a weapon in each hand.";
            }
        }
    }
    if (why != NULL) {
        *why = reason;
    }
    return reason == NULL;
}

const ItemBase *Inventory_Weapon(const Character *character) {
    for (int s = EQUIP_RIGHT_ARM; s <= EQUIP_LEFT_ARM; s++) {
        const Item *item = &character->equipped[s];
        if (item->code[0] != '\0') {
            const ItemBase *base = Items_Find(item->code);
            if (base != NULL && base->kind == ITEM_KIND_WEAPON) {
                return base;
            }
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// The belt

int Inventory_BeltSize(const Character *character) {
    const Item     *belt = &character->equipped[EQUIP_BELT];
    const ItemBase *base = belt->code[0] != '\0' ? Items_Find(belt->code) : NULL;
    const int       rows = base != NULL && base->belt_rows > 0 ? base->belt_rows : 1;
    return (rows > 4 ? 4 : rows) * 4;
}

void Inventory_SettleBelt(Character *character) {
    const int size = Inventory_BeltSize(character);
    for (int pass = 0; pass < 4; pass++) {
        for (int s = 0; s + 4 < size; s++) {
            if (character->belt[s].code[0] == '\0' && character->belt[s + 4].code[0] != '\0') {
                character->belt[s] = character->belt[s + 4];
                memset(&character->belt[s + 4], 0, sizeof(Item));
            }
        }
    }
}

bool Inventory_AddToBelt(Character *character, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || !base->beltable) {
        return false;
    }
    const int size = Inventory_BeltSize(character);
    for (int s = 0; s < size; s++) {
        if (character->belt[s].code[0] == '\0') {
            character->belt[s] = *item;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Starting items

void Inventory_GiveStartingItems(Character *character) {
    character->inventory_count = 0;
    memset(character->equipped, 0, sizeof(character->equipped));
    memset(character->belt, 0, sizeof(character->belt));
    DataTable *stats = DataTable_Load(EXCEL_CHARSTATS);
    if (stats == NULL) {
        return;
    }
    const int c_class = DataTable_GetColumnIndex(stats, "class");
    for (int row = 0; row < DataTable_GetRowCount(stats); row++) {
        if (strcmp(DataTable_GetString(stats, row, c_class), Character_ClassName(character->class_id)) != 0) {
            continue;
        }
        for (int i = 1; i <= 10; i++) {
            char column[16];
            snprintf(column, sizeof(column), "item%d", i);
            const char *code = DataTable_GetString(stats, row, DataTable_GetColumnIndex(stats, column));
            snprintf(column, sizeof(column), "item%dloc", i);
            const char *loc = DataTable_GetString(stats, row, DataTable_GetColumnIndex(stats, column));
            snprintf(column, sizeof(column), "item%dcount", i);
            const int count = DataTable_GetInt(stats, row, DataTable_GetColumnIndex(stats, column));
            if (Items_Find(code) == NULL) {
                continue; // "0": nothing
            }
            for (int n = 0; n < (count > 0 ? count : 1); n++) {
                Item item = Inventory_NewItem(code);
                if (loc[0] != '\0') {
                    const EquipSlot slot = strcmp(loc, "larm") == 0 ? EQUIP_LEFT_ARM : EQUIP_RIGHT_ARM;
                    if (character->equipped[slot].code[0] == '\0') {
                        character->equipped[slot] = item;
                        continue;
                    }
                }
                const ItemBase *base = Items_Find(code);
                if (!(Items_IsPotion(base) && Inventory_AddToBelt(character, &item))) {
                    Inventory_Add(character, &item);
                }
            }
        }
        break;
    }
    DataTable_Destroy(&stats);
}
