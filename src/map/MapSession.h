#ifndef ABYSS_MAP_SESSION_H
#define ABYSS_MAP_SESSION_H

#include "../scenes/CatalogCharacters.h"
#include "Map.h"
#include <stdbool.h>

// State shared by the Map Editor and Testing Grounds scenes, which replace each other: the map being edited
// (kept while you play-test it) and the Bestiary (slow to build, so built once).

CatalogCharacters *MapSession_GetBestiary(void);

// The editor parks its map here when it hands over to Testing Grounds, and takes it back afterwards.
void       MapSession_ParkMap(Map *map, bool playtest);
Map       *MapSession_TakeMap(void);
const Map *MapSession_PeekMap(void);
bool       MapSession_IsPlaytest(void); // Testing Grounds was opened from the editor

// Frees everything (back at the main menu).
void MapSession_Release(void);

#endif // ABYSS_MAP_SESSION_H
