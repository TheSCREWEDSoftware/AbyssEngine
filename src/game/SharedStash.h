#ifndef ABYSS_SHAREDSTASH_H
#define ABYSS_SHAREDSTASH_H

#include "Character.h"
#include <stdbool.h>

// The shared stash: a stash page every character on this computer can reach (one for softcore characters, another for
// hardcore ones), kept in save/shared.stash (shared_hc.stash), like the remastered game's shared tab. Expansion
// characters, Single Player only.
//
// It's shown through the character's own stash: SharedStash_Swap puts the shared page in place of the character's
// stash (and the character's out of the way); swapping again puts them back and saves the shared page if it changed.
// The stash screen swaps around each frame's input and drawing, so nothing else ever sees the shared items.

bool SharedStash_Allowed(const Character *character); // an expansion character
void SharedStash_Swap(Character *character);
void SharedStash_Release(void);

#endif // ABYSS_SHAREDSTASH_H
