#ifndef ABYSS_CATALOG_SPRITES_H
#define ABYSS_CATALOG_SPRITES_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// The "Sprites" part of the Catalog: browse every DC6 image named in the MPQ list files, step through its frames
// and directions, play its animation and try it with different palettes.
typedef struct CatalogSprites CatalogSprites;

CatalogSprites *CatalogSprites_Create(void);
void            CatalogSprites_Destroy(CatalogSprites **sprites);
int             CatalogSprites_GetCount(const CatalogSprites *sprites);
const char     *CatalogSprites_GetName(const CatalogSprites *sprites, int index);

// Category bar (Items / Skills / Menus / UI / Fonts / Other). Update returns true when the category changed.
bool CatalogSprites_UpdateCategories(CatalogSprites *sprites, int x, int y, int width);
void CatalogSprites_DrawCategories(const CatalogSprites *sprites, int x, int y, int width);
int  CatalogSprites_GetCategoriesHeight(const CatalogSprites *sprites, int width); // one or two rows

// index is a position in the current category's list
void CatalogSprites_Select(CatalogSprites *sprites, int index);
void CatalogSprites_UpdateDetail(CatalogSprites *sprites);
void CatalogSprites_DrawDetail(CatalogSprites *sprites, const SDL_Rect *panel);

#endif // ABYSS_CATALOG_SPRITES_H
