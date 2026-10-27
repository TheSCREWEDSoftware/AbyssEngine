#ifndef ABYSS_CUBE_H
#define ABYSS_CUBE_H

#include "Character.h"
#include <stdbool.h>
#include <stddef.h>

// The Horadric Cube: its 3 x 4 inside (the character's cube[]) and Transmute, with the game's recipes from
// cubemain.txt: quest items (the Horadric Staff, Khalim's Will), gems and runes up a grade, potions, rings and amulets,
// rerolling magic items, sockets, repairs, crafted items, exceptional and elite upgrades, the cow level's portal.
// Recipes with what the engine doesn't have (a unique by name, the Pandemonium portals) are left out.

#define CUBE_ITEM "box"

// The inside: like the stash, a grid
int  Cube_ItemAt(const Character *character, int cell_x, int cell_y);
int  Cube_Covered(const Character *character, const ItemBase *base, int x, int y); // -1 free, index, -2 can't
bool Cube_Place(Character *character, const Item *item, int x, int y);
bool Cube_Add(Character *character, const Item *item);
void Cube_Remove(Character *character, int index);
bool Cube_Carried(const Character *character); // a cube in the inventory

// Transmute: the recipe whose inputs are exactly what's inside, made. False (with why) when nothing matches.
bool Cube_Transmute(Character *character, char *message, size_t size);

// The recipe that opens the cow level was just made: the game takes you there (once per call)
bool Cube_TakePortal(void);

void Cube_Release(void);

#endif // ABYSS_CUBE_H
