#include "Checkbox.h"
#include "../audio/Sounds.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "../managers/PadNav.h"
#include "Label.h"
#include <SDL2/SDL.h>
#include <stdlib.h>

#define BOX_SIZE 14

struct Checkbox {
    int    x;
    int    y;
    Label *label;
    bool   checked;
    bool   enabled;
    bool   prev_left;
};

Checkbox *Checkbox_Create(const int x, const int y, const char *text) {
    Checkbox *checkbox = calloc(1, sizeof(Checkbox));
    FAIL_IF_NULL(checkbox);
    checkbox->x       = x;
    checkbox->y       = y;
    checkbox->enabled = true;
    checkbox->label   = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(checkbox->label, text);
    Label_SetAlignment(checkbox->label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    return checkbox;
}

void Checkbox_Destroy(Checkbox **checkbox) {
    if (checkbox == NULL || *checkbox == NULL) {
        return;
    }
    Label_Destroy(&(*checkbox)->label);
    free(*checkbox);
    *checkbox = NULL;
}

void Checkbox_SetChecked(Checkbox *checkbox, const bool checked) { checkbox->checked = checked; }
bool Checkbox_IsChecked(const Checkbox *checkbox) { return checkbox->checked; }
void Checkbox_SetEnabled(Checkbox *checkbox, const bool enabled) { checkbox->enabled = enabled; }

bool Checkbox_Update(Checkbox *checkbox) {
    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked  = left && !checkbox->prev_left;
    checkbox->prev_left = left;
    int text_w;
    int text_h;
    Label_GetSize(checkbox->label, &text_w, &text_h);
    if (checkbox->enabled) {
        PadNav_Register(&(SDL_Rect){checkbox->x, checkbox->y, BOX_SIZE + 8 + text_w, BOX_SIZE});
    }
    const bool over = mouse_x >= checkbox->x && mouse_x < checkbox->x + BOX_SIZE + 8 + text_w &&
                      mouse_y >= checkbox->y && mouse_y < checkbox->y + BOX_SIZE;
    if (clicked && over && checkbox->enabled) {
        checkbox->checked = !checkbox->checked;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        return true;
    }
    return false;
}

void Checkbox_Draw(const Checkbox *checkbox) {
    const SDL_Rect box = {checkbox->x, checkbox->y, BOX_SIZE, BOX_SIZE};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 20, 18, 15, 230);
    SDL_RenderFillRect(sdl_renderer, &box);
    SDL_SetRenderDrawColor(sdl_renderer, 150, 125, 70, 255);
    SDL_RenderDrawRect(sdl_renderer, &box);
    if (checkbox->checked) {
        const SDL_Rect tick = {checkbox->x + 3, checkbox->y + 3, BOX_SIZE - 6, BOX_SIZE - 6};
        SDL_SetRenderDrawColor(sdl_renderer, 220, 190, 110, 255);
        SDL_RenderFillRect(sdl_renderer, &tick);
    }
    const uint8_t shade = checkbox->enabled ? 0xFF : 0x80;
    Label_SetColor(checkbox->label, shade, shade, shade, 0xFF);
    Label_Draw(checkbox->label, checkbox->x + BOX_SIZE + 8, checkbox->y + (BOX_SIZE / 2));
}
