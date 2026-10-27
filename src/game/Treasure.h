#ifndef ABYSS_TREASURE_H
#define ABYSS_TREASURE_H

#include "Character.h"

// What drops: the game's treasure classes (TreasureClassEx.txt). A monster (monstats.txt TreasureClass1) or a chest
// names one; it picks from its list, maybe nothing ("NoDrop"), and what it picks can be another class, an item, gold,
// or one of the classes the game makes up from the items themselves: "weap12" is a weapon of item level 10 to 12,
// "armo", "mele" (melee weapons) and "bow" the same. Weapons, armor and jewelry may come out magic, rare or unique
// (Affixes.h); no set items yet.

#define TREASURE_MAX_DROPS 6 // the most one roll gives (the game's limit)

// Rolls a class: fills items (up to TREASURE_MAX_DROPS) and returns how many. level: the monster's (gold's amount).
int  Treasure_Roll(const char *treasure_class, int level, Item *items);
bool Treasure_Exists(const char *treasure_class);
void Treasure_Release(void);

#endif // ABYSS_TREASURE_H
