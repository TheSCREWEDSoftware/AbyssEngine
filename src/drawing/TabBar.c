#include "TabBar.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#define TAB_BAR_PADDING 8 // minimum room around each name

struct TabBar {
    Label **labels;
    int     count;
    int     selected;
    bool    prev_mouse_left;
};

TabBar *TabBar_Create(const char *const *names, const int count, const int selected) {
    TabBar *tab_bar = malloc(sizeof(TabBar));
    FAIL_IF_NULL(tab_bar);
    memset(tab_bar, 0, sizeof(TabBar));

    tab_bar->labels = calloc((size_t)count, sizeof(Label *));
    FAIL_IF_NULL(tab_bar->labels);
    tab_bar->count    = count;
    tab_bar->selected = selected;

    for (int i = 0; i < count; i++) {
        tab_bar->labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetText(tab_bar->labels[i], names[i]);
        Label_SetAlignment(tab_bar->labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }

    return tab_bar;
}

void TabBar_Destroy(TabBar **tab_bar) {
    if (tab_bar == NULL || *tab_bar == NULL) {
        return;
    }

    for (int i = 0; i < (*tab_bar)->count; i++) {
        Label_Destroy(&(*tab_bar)->labels[i]);
    }
    free((*tab_bar)->labels);
    free(*tab_bar);
    *tab_bar = NULL;
}

int TabBar_GetSelected(const TabBar *tab_bar) { return tab_bar->selected; }

static int TabBar__TextWidth(const TabBar *tab_bar, const int tab) {
    int width = 0;
    Label_GetSize(tab_bar->labels[tab], &width, NULL);
    return width;
}

// Tabs [first, first + count) of one row: do they fit side by side?
static bool TabBar__RowFits(const TabBar *tab_bar, const int first, const int count, const int width) {
    int total = 0;
    for (int i = first; i < first + count; i++) {
        total += TabBar__TextWidth(tab_bar, i) + TAB_BAR_PADDING;
    }
    return total <= width;
}

// When the names don't fit in one row they wrap onto more rows, split as evenly as possible.
static int TabBar__RowCount(const TabBar *tab_bar, const int width) {
    for (int rows = 1; rows < tab_bar->count; rows++) {
        const int per_row = (tab_bar->count + rows - 1) / rows;
        bool      fits    = true;
        for (int first = 0; first < tab_bar->count && fits; first += per_row) {
            const int count = first + per_row <= tab_bar->count ? per_row : tab_bar->count - first;
            fits            = TabBar__RowFits(tab_bar, first, count, width);
        }
        if (fits) {
            return rows;
        }
    }
    return tab_bar->count > 0 ? tab_bar->count : 1;
}

int TabBar_GetHeight(const TabBar *tab_bar, const int width) {
    return TabBar__RowCount(tab_bar, width) * TAB_BAR_HEIGHT;
}

// Within a row, tabs share the width in proportion to their text, so longer names get more room.
static SDL_Rect TabBar__Rect(const TabBar *tab_bar, const int x, const int y, const int width, const int tab) {
    const int rows    = TabBar__RowCount(tab_bar, width);
    const int per_row = (tab_bar->count + rows - 1) / rows;
    const int row     = tab / per_row;
    const int first   = row * per_row;
    const int count   = first + per_row <= tab_bar->count ? per_row : tab_bar->count - first;

    int total = 0;
    for (int i = first; i < first + count; i++) {
        total += TabBar__TextWidth(tab_bar, i);
    }
    const int padding = (width - total) / count;
    int       left    = x;
    for (int i = first; i < tab; i++) {
        left += TabBar__TextWidth(tab_bar, i) + padding;
    }

    const int      right = (tab == first + count - 1) ? x + width : left + TabBar__TextWidth(tab_bar, tab) + padding;
    const SDL_Rect rect  = {left, y + (row * TAB_BAR_HEIGHT), right - left, TAB_BAR_HEIGHT};
    return rect;
}

bool TabBar_Update(TabBar *tab_bar, const int x, const int y, const int width) {
    int  mouse_x;
    int  mouse_y;
    bool mouse_left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool clicked       = !mouse_left && tab_bar->prev_mouse_left;
    tab_bar->prev_mouse_left = mouse_left;
    if (!clicked) {
        return false;
    }

    for (int i = 0; i < tab_bar->count; i++) {
        const SDL_Rect rect = TabBar__Rect(tab_bar, x, y, width, i);
        if (mouse_x >= rect.x && mouse_x < rect.x + rect.w && mouse_y >= rect.y && mouse_y < rect.y + rect.h &&
            i != tab_bar->selected) {
            tab_bar->selected = i;
            return true;
        }
    }
    return false;
}

void TabBar_Draw(const TabBar *tab_bar, const int x, const int y, const int width) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < tab_bar->count; i++) {
        const SDL_Rect rect     = TabBar__Rect(tab_bar, x, y, width, i);
        const bool     selected = i == tab_bar->selected;

        SDL_SetRenderDrawColor(sdl_renderer, selected ? 120 : 0, selected ? 95 : 0, selected ? 40 : 0,
                               selected ? 220 : 170);
        SDL_RenderFillRect(sdl_renderer, &rect);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
        SDL_RenderDrawRect(sdl_renderer, &rect);

        Label *label = tab_bar->labels[i];
        if (selected) {
            Label_SetColor(label, 0xFF, 0xE8, 0xA0, 0xFF);
        } else {
            Label_SetColor(label, 0xC7, 0xB3, 0x77, 0xFF);
        }
        Label_Draw(label, rect.x + (rect.w / 2), rect.y + (rect.h / 2));
    }
}
