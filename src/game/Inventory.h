#ifndef ABYSS_INVENTORY_H
#define ABYSS_INVENTORY_H

#include "Character.h"
#include "Items.h"

// A character's items: the inventory grid (10 x 4 cells), what's worn, and the belt. The rules are the game's: an
// item takes width x height cells, worn items must meet their requirements and suit the slot, a two-handed weapon
// leaves the other hand empty, and potions and scrolls go in the belt (a row of four without a belt, more with one).

Item Inventory_NewItem(const char *code); // a fresh one (armor gets its defense rolled)

// The class's starting items from charstats.txt: weapon and shield worn, potions in the belt, the rest in the grid
void Inventory_GiveStartingItems(Character *character);

// Pages: the grid has 1 to 255 of them (Options > Gameplay > Inventory pages; a multiplayer game's own setting).
// The grid functions below work on the page being shown; Inventory_Add looks through every page.
void Inventory_SetPages(int count); // how many there are now (at least those with items on, see Inventory_PagesUsed)
int  Inventory_Pages(void);
void Inventory_SetPage(int page); // the one being shown
int  Inventory_Page(void);
int  Inventory_PagesUsed(const Character *character); // 1 + the last page with something on it
bool Inventory_PlaceOnPage(Character *character, const Item *item, int x, int y, int page);

// The grid
int  Inventory_ItemAt(const Character *character, int cell_x, int cell_y); // index in inventory[], -1 if empty
bool Inventory_FreeAt(const Character *character, const ItemBase *base, int x, int y); // all its cells free
// What putting an item there would lift off: -1 none, an index (the one item it covers), -2 more than one / off
// the grid
int  Inventory_Covered(const Character *character, const ItemBase *base, int x, int y);
bool Inventory_Place(Character *character, const Item *item, int x, int y); // where it's free
bool Inventory_Add(Character *character, const Item *item);                 // anywhere free (left to right)
void Inventory_Remove(Character *character, int index);

// Wearing
bool            Inventory_CanEquip(const Character *character, const Item *item, EquipSlot slot, const char **why);
bool            Inventory_MeetsRequirements(const Character *character, const ItemBase *base);
const ItemBase *Inventory_Weapon(const Character *character); // NULL: bare hands

// The stash (in town): the same as the grid, 6 x 8 (the expansion's big one; classic characters' is 6 x 4), one page
int  Stash_Rows(const Character *character);
int  Stash_ItemAt(const Character *character, int cell_x, int cell_y);
int  Stash_Covered(const Character *character, const ItemBase *base, int x, int y);
bool Stash_Place(Character *character, const Item *item, int x, int y);
bool Stash_Add(Character *character, const Item *item);
void Stash_Remove(Character *character, int index);
int  Stash_GoldLimit(const Character *character);     // the game's: 50,000 for every ten levels (and the first ten)
int  Inventory_GoldLimit(const Character *character); // carried: 10,000 a level

// The belt
int  Inventory_BeltSize(const Character *character); // slots: 4 per row of the belt worn (4 without one)
bool Inventory_AddToBelt(Character *character, const Item *item);
void Inventory_SettleBelt(Character *character); // potions drop down into emptied slots of the row below

#endif // ABYSS_INVENTORY_H
