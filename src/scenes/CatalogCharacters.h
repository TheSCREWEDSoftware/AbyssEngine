#ifndef ABYSS_CATALOG_CHARACTERS_H
#define ABYSS_CATALOG_CHARACTERS_H

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

void CatalogCharacters_Select(CatalogCharacters *characters, int index);
void CatalogCharacters_UpdateDetail(CatalogCharacters *characters);
void CatalogCharacters_DrawDetail(CatalogCharacters *characters, const SDL_Rect *panel);

#endif // ABYSS_CATALOG_CHARACTERS_H
