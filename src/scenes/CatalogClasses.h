#ifndef ABYSS_CATALOG_CLASSES_H
#define ABYSS_CATALOG_CLASSES_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// The "Classes" part of the Catalog: class stats (charstats.txt) and the skill trees (skills.txt + skilldesc.txt),
// drawn with the game's own skill panel and icons.
typedef struct CatalogClasses CatalogClasses;

CatalogClasses *CatalogClasses_Create(void);
void            CatalogClasses_Destroy(CatalogClasses **classes);
int             CatalogClasses_GetCount(const CatalogClasses *classes);
const char     *CatalogClasses_GetName(const CatalogClasses *classes, int index);

// Class detail panel (stats + animated figure). Returns true when "Skill Trees" was clicked.
void CatalogClasses_Select(CatalogClasses *classes, int index);
bool CatalogClasses_UpdateDetail(CatalogClasses *classes);
void CatalogClasses_DrawDetail(const CatalogClasses *classes, const SDL_Rect *panel);

// Full screen skill tree view for the selected class.
void CatalogClasses_OpenSkillTree(CatalogClasses *classes);
void CatalogClasses_CloseSkillTree(CatalogClasses *classes);
bool CatalogClasses_IsSkillTreeOpen(const CatalogClasses *classes);
void CatalogClasses_UpdateSkillTree(CatalogClasses *classes);
void CatalogClasses_DrawSkillTree(CatalogClasses *classes);

#endif // ABYSS_CATALOG_CLASSES_H
