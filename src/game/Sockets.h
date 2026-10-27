#ifndef ABYSS_SOCKETS_H
#define ABYSS_SOCKETS_H

#include "Character.h"
#include <stdbool.h>

// Sockets, the game's way: weapons, shields, helms and body armor can come with up to six (gemsockets in their
// table, fewer at low item levels). A gem or rune (gems.txt) put in one adds its magic for that kind of item (a
// weapon's, a helm's or armor's, a shield's); a jewel adds its own. The right runes in the right order filling a plain
// item make a runeword (Runes.txt), which adds the runeword's magic and names the item.

bool        Sockets_CanGoIn(const Item *item);                               // a gem, rune or jewel
bool        Sockets_Insert(Item *target, const Item *gem, const char **why); // false (why) when it can't
void        Sockets_Roll(Item *item, int item_level); // a dropped item: sockets, sometimes (plain and magic ones)
const char *Sockets_RunewordName(int row);            // NULL if no such runeword
void        Sockets_Release(void);

#endif // ABYSS_SOCKETS_H
