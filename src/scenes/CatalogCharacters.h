#ifndef ABYSS_CATALOG_CHARACTERS_H
#define ABYSS_CATALOG_CHARACTERS_H

#include "../drawing/Figure.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// The "Characters" part of the Catalog: players, NPCs, enemies, bosses and objects drawn from their DCC/COF
// animations, with every direction and animation mode.
typedef struct CatalogCharacters CatalogCharacters;

CatalogCharacters *CatalogCharacters_Create(void);
void               CatalogCharacters_Destroy(CatalogCharacters **characters);

// Entries of the current category (Players / NPCs / Enemies / Bosses / Others)
int         CatalogCharacters_GetCount(const CatalogCharacters *characters);
const char *CatalogCharacters_GetName(const CatalogCharacters *characters, int index);

// Category bar. Update returns true when the category changed (the list must be refreshed).
bool CatalogCharacters_UpdateCategories(CatalogCharacters *characters, int x, int y, int width);
void CatalogCharacters_DrawCategories(const CatalogCharacters *characters, int x, int y, int width);

// For placing characters elsewhere (the map editor): how to build the figure and which animations to use.
typedef struct CatalogCharacterInfo {
    const char       *name;
    const char       *id; // unique per base: monstats Id, object token or player token
    const FigureSpec *spec;
    const char       *idle_mode; // "tn" (town) when there is one, otherwise the first animation
    const char       *walk_mode; // "tw" / "wl", NULL if it can't walk
} CatalogCharacterInfo;

bool CatalogCharacters_GetInfo(const CatalogCharacters *characters, int index, CatalogCharacterInfo *info);
bool CatalogCharacters_Find(const CatalogCharacters *characters, const char *base, const char *id,
                            CatalogCharacterInfo *info);

void CatalogCharacters_Select(CatalogCharacters *characters, int index);
void CatalogCharacters_UpdateDetail(CatalogCharacters *characters);
void CatalogCharacters_DrawDetail(CatalogCharacters *characters, const SDL_Rect *panel);

#endif // ABYSS_CATALOG_CHARACTERS_H
