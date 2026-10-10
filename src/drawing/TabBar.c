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
    Label **small_labels; // used when the names don't fit the bar's width
    int     count;
    int     selected;
    bool    prev_mouse_left;
};

TabBar *TabBar_Create(const char *const *names, const int count, const int selected) {
    TabBar *tab_bar = malloc(sizeof(TabBar));
    FAIL_IF_NULL(tab_bar);
    memset(tab_bar, 0, sizeof(TabBar));

    tab_bar->labels       = calloc((size_t)count, sizeof(Label *));
    tab_bar->small_labels = calloc((size_t)count, sizeof(Label *));
    FAIL_IF_NULL(tab_bar->labels);
    FAIL_IF_NULL(tab_bar->small_labels);
    tab_bar->count    = count;
    tab_bar->selected = selected;

    for (int i = 0; i < count; i++) {
        tab_bar->labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetText(tab_bar->labels[i], names[i]);
        Label_SetAlignment(tab_bar->labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        tab_bar->small_labels[i] = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        Label_SetText(tab_bar->small_labels[i], names[i]);
        Label_SetAlignment(tab_bar->small_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }

    return tab_bar;
}

void TabBar_Destroy(TabBar **tab_bar) {
    if (tab_bar == NULL || *tab_bar == NULL) {
        return;
    }

    for (int i = 0; i < (*tab_bar)->count; i++) {
        Label_Destroy(&(*tab_bar)->labels[i]);
        Label_Destroy(&(*tab_bar)->small_labels[i]);
    }
    free((*tab_bar)->labels);
    free((*tab_bar)->small_labels);
    free(*tab_bar);
    *tab_bar = NULL;
}

int TabBar_GetSelected(const TabBar *tab_bar) { return tab_bar->selected; }

// The labels to use for a bar this wide: the normal font, or the small one when the names don't fit
static Label *const *TabBar__Labels(const TabBar *tab_bar, const int width) {
    int total = 0;
    for (int i = 0; i < tab_bar->count; i++) {
        int text_w = 0;
        Label_GetSize(tab_bar->labels[i], &text_w, NULL);
        total += text_w;
    }
    return total + (tab_bar->count * TAB_BAR_PADDING) > width ? tab_bar->small_labels : tab_bar->labels;
}

// Tabs share the bar width in proportion to their text, so longer names get more room.
static SDL_Rect TabBar__Rect(const TabBar *tab_bar, const int x, const int y, const int width, const int tab) {
    Label *const *labels = TabBar__Labels(tab_bar, width);
    int           total  = 0;
    int           left   = x;
    for (int i = 0; i < tab_bar->count; i++) {
        int text_w = 0;
        Label_GetSize(labels[i], &text_w, NULL);
        total += text_w;
    }

    const int padding = (width - total) / tab_bar->count;
    for (int i = 0; i < tab; i++) {
        int text_w = 0;
        Label_GetSize(labels[i], &text_w, NULL);
        left += text_w + padding;
    }

    int text_w = 0;
    Label_GetSize(labels[tab], &text_w, NULL);
    const int      right = (tab == tab_bar->count - 1) ? x + width : left + text_w + padding;
    const SDL_Rect rect  = {left, y, right - left, TAB_BAR_HEIGHT};
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

        Label *label = TabBar__Labels(tab_bar, width)[i];
        if (selected) {
            Label_SetColor(label, 0xFF, 0xE8, 0xA0, 0xFF);
        } else {
            Label_SetColor(label, 0xC7, 0xB3, 0x77, 0xFF);
        }
        Label_Draw(label, rect.x + (rect.w / 2), rect.y + (rect.h / 2));
    }
}
