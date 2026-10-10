#ifndef ABYSS_CATALOG_THEME_H
#define ABYSS_CATALOG_THEME_H

#include <SDL2/SDL.h>

// Background colours for the Catalog panels, switchable with the "Backdrop" button. Shared by all tabs.
typedef struct CatalogColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} CatalogColor;

void         CatalogTheme_Next(void);
const char  *CatalogTheme_GetName(void);
CatalogColor CatalogTheme_Panel(void);                       // detail panels, lists
CatalogColor CatalogTheme_Preview(void);                     // the box behind pictures and figures (a little lighter)
void         CatalogTheme_FillPanel(const SDL_Rect *rect);   // filled + gold border
void         CatalogTheme_FillPreview(const SDL_Rect *rect); // filled
void         CatalogTheme_FillChecker(const SDL_Rect *rect); // preview colour as a checkerboard (transparency)

#endif // ABYSS_CATALOG_THEME_H
