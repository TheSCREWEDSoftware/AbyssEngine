#include "Dropdown.h"
#include "../audio/Sounds.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

#define DROPDOWN_HEIGHT   20
#define DROPDOWN_ROW      18
#define DROPDOWN_MAX      16
#define DROPDOWN_TEXT_MAX 48

struct Dropdown {
    int    x;
    int    y;
    int    width;
    char   choices[DROPDOWN_MAX][DROPDOWN_TEXT_MAX];
    int    count;
    int    selected;
    int    hovered; // in the open list, -1 = none
    bool   open;
    bool   prev_left;
    Label *label;
};

Dropdown *Dropdown_Create(const int x, const int y, const int width, const char *const *choices, const int count) {
    Dropdown *dropdown = calloc(1, sizeof(Dropdown));
    FAIL_IF_NULL(dropdown);
    dropdown->x       = x;
    dropdown->y       = y;
    dropdown->width   = width;
    dropdown->hovered = -1;
    dropdown->label   = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(dropdown->label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Dropdown_SetChoices(dropdown, choices, count);
    return dropdown;
}

void Dropdown_Destroy(Dropdown **dropdown) {
    if (dropdown == NULL || *dropdown == NULL) {
        return;
    }
    Label_Destroy(&(*dropdown)->label);
    free(*dropdown);
    *dropdown = NULL;
}

void Dropdown_SetChoices(Dropdown *dropdown, const char *const *choices, const int count) {
    dropdown->count = count < DROPDOWN_MAX ? count : DROPDOWN_MAX;
    for (int i = 0; i < dropdown->count; i++) {
        snprintf(dropdown->choices[i], DROPDOWN_TEXT_MAX, "%s", choices[i]);
    }
    if (dropdown->selected >= dropdown->count) {
        dropdown->selected = 0;
    }
}

void Dropdown_SetSelected(Dropdown *dropdown, const int index) {
    if (index >= 0 && index < dropdown->count) {
        dropdown->selected = index;
    }
}

int  Dropdown_GetSelected(const Dropdown *dropdown) { return dropdown->selected; }
bool Dropdown_IsOpen(const Dropdown *dropdown) { return dropdown->open; }

bool Dropdown_Update(Dropdown *dropdown) {
    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked  = left && !dropdown->prev_left;
    dropdown->prev_left = left;

    const bool over_box = mouse_x >= dropdown->x && mouse_x < dropdown->x + dropdown->width && mouse_y >= dropdown->y &&
                          mouse_y < dropdown->y + DROPDOWN_HEIGHT;
    dropdown->hovered = -1;
    if (dropdown->open) {
        const int list_y = dropdown->y + DROPDOWN_HEIGHT;
        if (mouse_x >= dropdown->x && mouse_x < dropdown->x + dropdown->width && mouse_y >= list_y &&
            mouse_y < list_y + (dropdown->count * DROPDOWN_ROW)) {
            dropdown->hovered = (mouse_y - list_y) / DROPDOWN_ROW;
        }
        if (clicked || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
            dropdown->open = false;
            if (clicked && dropdown->hovered >= 0 && dropdown->hovered != dropdown->selected) {
                dropdown->selected = dropdown->hovered;
                Sounds_Play("cursor_button_click", SOUND_KIND_UI);
                return true;
            }
        }
        return false;
    }
    if (clicked && over_box && dropdown->count > 0) {
        dropdown->open = true;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    return false;
}

static void Dropdown__Box(const int x, const int y, const int w, const int h, const bool highlighted) {
    const SDL_Rect box = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, highlighted ? 120 : 20, highlighted ? 95 : 18, highlighted ? 40 : 15, 235);
    SDL_RenderFillRect(sdl_renderer, &box);
}

void Dropdown_Draw(const Dropdown *dropdown) {
    Dropdown__Box(dropdown->x, dropdown->y, dropdown->width, DROPDOWN_HEIGHT, false);
    const SDL_Rect frame = {dropdown->x, dropdown->y, dropdown->width, DROPDOWN_HEIGHT};
    SDL_SetRenderDrawColor(sdl_renderer, 150, 125, 70, 255);
    SDL_RenderDrawRect(sdl_renderer, &frame);
    // The arrow
    const int ax = dropdown->x + dropdown->width - 14;
    const int ay = dropdown->y + 8;
    for (int i = 0; i < 5; i++) {
        SDL_RenderDrawLine(sdl_renderer, ax + i, ay + i, ax + 8 - i, ay + i);
    }
    if (dropdown->count > 0) {
        Label_SetText(dropdown->label, dropdown->choices[dropdown->selected]);
        Label_SetColor(dropdown->label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(dropdown->label, dropdown->x + 6, dropdown->y + (DROPDOWN_HEIGHT / 2));
    }
}

void Dropdown_DrawList(const Dropdown *dropdown) {
    if (!dropdown->open) {
        return;
    }
    const int list_y = dropdown->y + DROPDOWN_HEIGHT;
    for (int i = 0; i < dropdown->count; i++) {
        Dropdown__Box(dropdown->x, list_y + (i * DROPDOWN_ROW), dropdown->width, DROPDOWN_ROW, i == dropdown->hovered);
        Label_SetText(dropdown->label, dropdown->choices[i]);
        const bool chosen = i == dropdown->selected;
        Label_SetColor(dropdown->label, chosen ? 0xFF : 0xE0, chosen ? 0xE8 : 0xE0, chosen ? 0x90 : 0xE0, 0xFF);
        Label_Draw(dropdown->label, dropdown->x + 6, list_y + (i * DROPDOWN_ROW) + (DROPDOWN_ROW / 2));
    }
    const SDL_Rect frame = {dropdown->x, list_y, dropdown->width, dropdown->count * DROPDOWN_ROW};
    SDL_SetRenderDrawColor(sdl_renderer, 150, 125, 70, 255);
    SDL_RenderDrawRect(sdl_renderer, &frame);
}
