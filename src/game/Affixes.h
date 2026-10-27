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
// Magic Find for the rolls that follow (the killer's: Roam's drops set it, then put it back to 0)
void Affixes_SetMagicFind(int percent);

// A chosen quality (gambling): false when the item can't be that (no unique of its kind at that level, ...)
bool Affixes_RollQuality(Item *item, int item_level, ItemQuality quality);
// A magic item with these affixes (MagicPrefix.txt / MagicSuffix.txt rows, from 0; -1: none): the cube's recipes
bool Affixes_SetMagic(Item *item, int prefix, int suffix);

// The name shown: magic "Jagged Hand Axe of the Fox"; rare "Grim Bite" with the base's name as line2; unique "The
// Gnasher" with the base's name; normal the base's name (line2 "")
void Affixes_Name(const Item *item, char *line1, size_t size1, char *line2, size_t size2);
void Affixes_QualityColor(const Item *item, uint8_t *r, uint8_t *g, uint8_t *b); // white, blue, yellow, gold
// One property as the game words it: "+5 to Strength", "+20% Enhanced Defense"
void Affixes_ModText(const ItemMod *mod, char *text, size_t size);
// What worn items add to one of the class's skills (its index in Skills_ForClass): +skills of all kinds
// (a skill with no point in it that a worn item has charges of: the charged level)
int Affixes_SkillBonus(const Character *character, int skill);
// Skills worn items give that aren't the class's own (charges with some left, another class's "oskill"), by
// skills.txt row; the count
int Affixes_ItemSkills(const Character *character, int *rows, int max);
// Such a skill's level (charged: it comes only from charges, so using it takes one)
int Affixes_ItemSkillLevel(const Character *character, int row, bool *charged);
// Uses up a charge of the skill (the item with the best level); false if it's not from charges
bool Affixes_UseCharge(Character *character, int row);
// Fills an item's charges (repairs do); the charges added
int Affixes_Recharge(Item *item);
// The inventory picture (uniques have their own)
const char *Affixes_InvFile(const Item *item);
// The sum of one property over the items worn
int Affixes_Total(const Character *character, ItemModType type);
int Affixes_ItemTotal(const Item *item, ItemModType type); // on one item

// In save files: " <quality> <name1> <name2> <mods>" ("str:5,ac%:20", or "-") after an item's other fields
// Adds a Properties.txt property (one we know) to the item's magic: rolled between min and max, or a pair (fire
// damage: min to the minimum, max to the maximum). Gems in sockets and runewords use it.
void Affixes_AddProperty(Item *item, const char *code, int min, int max);
void Affixes_AddPropertyParam(Item *item, const char *code, const char *param, int min, int max); // (a skill, ...)
void Affixes_AddMod(Item *item, int type, int value);
bool Affixes_MakeRare(
    Item *item,
    int   item_level); // a plain item made rare (Charsi's imbue) // (adds up with the same kind already on it)

// Sets: how many pieces of a set are worn, and the set an item belongs to (NULL: not a set item)
int         Affixes_SetPiecesWorn(const Character *character, const char *set);
const char *Affixes_SetName(const Item *item);

void Affixes_Format(const Item *item, char *text, size_t size);
void Affixes_Parse(Item *item, const char *text);

void Affixes_Release(void);

#endif // ABYSS_AFFIXES_H
