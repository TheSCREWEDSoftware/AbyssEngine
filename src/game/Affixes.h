#ifndef ABYSS_AFFIXES_H
#define ABYSS_AFFIXES_H

#include "Character.h"
#include <stddef.h>

// Magic, rare and unique items, the game's way: affixes from MagicPrefix.txt / MagicSuffix.txt (the prefix and suffix
// a magic item has, and the several a rare one has), rare names from RarePrefix.txt / RareSuffix.txt, and the named
// items of UniqueItems.txt. Each affix or unique gives properties (Properties.txt codes: "str", "ac%", "dmg-max",
// ...); only affixes whose properties the engine knows are used (Items.h ItemModType), so what an item says is what it
// does.

// A drop's quality and magic: called for each weapon, armor and jewelry dropped, with its item level (the monster's)
void Affixes_Roll(Item *item, int item_level);

// The name shown: magic "Jagged Hand Axe of the Fox"; rare "Grim Bite" with the base's name as line2; unique "The
// Gnasher" with the base's name; normal the base's name (line2 "")
void Affixes_Name(const Item *item, char *line1, size_t size1, char *line2, size_t size2);
void Affixes_QualityColor(const Item *item, uint8_t *r, uint8_t *g, uint8_t *b); // white, blue, yellow, gold
// One property as the game words it: "+5 to Strength", "+20% Enhanced Defense"
void Affixes_ModText(const ItemMod *mod, char *text, size_t size);
// The inventory picture (uniques have their own)
const char *Affixes_InvFile(const Item *item);
// The sum of one property over the items worn
int Affixes_Total(const Character *character, ItemModType type);
int Affixes_ItemTotal(const Item *item, ItemModType type); // on one item

// In save files: " <quality> <name1> <name2> <mods>" ("str:5,ac%:20", or "-") after an item's other fields
void Affixes_Format(const Item *item, char *text, size_t size);
void Affixes_Parse(Item *item, const char *text);

void Affixes_Release(void);

#endif // ABYSS_AFFIXES_H
