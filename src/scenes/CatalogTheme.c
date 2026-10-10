#include "CatalogTheme.h"
#include "../common/Globals.h"

typedef struct CatalogBackdrop {
    const char  *name;
    CatalogColor panel;
    CatalogColor preview;
} CatalogBackdrop;

static const CatalogBackdrop backdrops[] = {
    {"Charcoal", {24, 24, 26, 245}, {48, 48, 52, 255}   },
    {"Grey",     {50, 50, 54, 250}, {84, 84, 90, 255}   },
    {"Stone",    {42, 37, 30, 250}, {74, 66, 54, 255}   },
    {"Slate",    {30, 36, 46, 250}, {56, 66, 82, 255}   },
    {"Light",    {66, 66, 70, 250}, {150, 150, 156, 255}}, // light picture box, readable text panel
    {"Dark",     {0, 0, 0, 170},    {32, 30, 34, 255}   }, // the original see-through look
};

#define BACKDROP_COUNT (int)(sizeof(backdrops) / sizeof(backdrops[0]))

static int current_backdrop = 0;

void CatalogTheme_Next(void) { current_backdrop = (current_backdrop + 1) % BACKDROP_COUNT; }

const char *CatalogTheme_GetName(void) { return backdrops[current_backdrop].name; }

CatalogColor CatalogTheme_Panel(void) { return backdrops[current_backdrop].panel; }

CatalogColor CatalogTheme_Preview(void) { return backdrops[current_backdrop].preview; }

void CatalogTheme_FillPanel(const SDL_Rect *rect) {
    const CatalogColor c = CatalogTheme_Panel();
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(sdl_renderer, rect);
    SDL_SetRenderDrawColor(sdl_renderer, 120, 95, 40, 255);
    SDL_RenderDrawRect(sdl_renderer, rect);
}

void CatalogTheme_FillPreview(const SDL_Rect *rect) {
    const CatalogColor c = CatalogTheme_Preview();
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(sdl_renderer, rect);
}

void CatalogTheme_FillChecker(const SDL_Rect *rect) {
    const CatalogColor c = CatalogTheme_Preview();
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    for (int y = 0; y < rect->h; y += 16) {
        for (int x = 0; x < rect->w; x += 16) {
            const int      shade = ((x / 16) + (y / 16)) % 2 == 0 ? -10 : 8;
            const SDL_Rect cell  = {rect->x + x, rect->y + y, (x + 16 <= rect->w) ? 16 : rect->w - x,
                                   (y + 16 <= rect->h) ? 16 : rect->h - y};
            SDL_SetRenderDrawColor(sdl_renderer, (uint8_t)SDL_clamp(c.r + shade, 0, 255),
                                   (uint8_t)SDL_clamp(c.g + shade, 0, 255), (uint8_t)SDL_clamp(c.b + shade, 0, 255),
                                   255);
            SDL_RenderFillRect(sdl_renderer, &cell);
        }
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
}
