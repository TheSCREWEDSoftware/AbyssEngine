#include "Slider.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../managers/InputManager.h"
#include <SDL2/SDL.h>
#include <stdlib.h>

#define SLIDER_HEIGHT 16
#define TRACK_HEIGHT  6
#define KNOB_WIDTH    8

struct Slider {
    int  x;
    int  y;
    int  width;
    int  minimum;
    int  maximum;
    int  value;
    bool dragging;
    bool prev_left;
};

Slider *Slider_Create(const int x, const int y, const int width, const int minimum, const int maximum) {
    Slider *slider = calloc(1, sizeof(Slider));
    FAIL_IF_NULL(slider);
    slider->x       = x;
    slider->y       = y;
    slider->width   = width;
    slider->minimum = minimum;
    slider->maximum = maximum > minimum ? maximum : minimum + 1;
    slider->value   = minimum;
    return slider;
}

void Slider_Destroy(Slider **slider) {
    if (slider != NULL) {
        free(*slider);
        *slider = NULL;
    }
}

void Slider_SetValue(Slider *slider, const int value) {
    slider->value = value < slider->minimum ? slider->minimum : (value > slider->maximum ? slider->maximum : value);
}

int Slider_GetValue(const Slider *slider) { return slider->value; }

bool Slider_Update(Slider *slider) {
    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool over = mouse_x >= slider->x - KNOB_WIDTH && mouse_x <= slider->x + slider->width + KNOB_WIDTH &&
                      mouse_y >= slider->y && mouse_y < slider->y + SLIDER_HEIGHT;
    if (left && !slider->prev_left && over) {
        slider->dragging = true;
    }
    if (!left) {
        slider->dragging = false;
    }
    slider->prev_left = left;

    const int before = slider->value;
    if (slider->dragging) {
        const int range = slider->maximum - slider->minimum;
        const int along = mouse_x - slider->x;
        Slider_SetValue(slider, slider->minimum + (int)(((float)along / (float)slider->width * (float)range) + 0.5f));
    }
    return slider->value != before;
}

void Slider_Draw(const Slider *slider) {
    const int      range = slider->maximum - slider->minimum;
    const int      fill  = (int)((float)(slider->value - slider->minimum) / (float)range * (float)slider->width);
    const int      track = slider->y + ((SLIDER_HEIGHT - TRACK_HEIGHT) / 2);
    const SDL_Rect back  = {slider->x, track, slider->width, TRACK_HEIGHT};
    const SDL_Rect done  = {slider->x, track, fill, TRACK_HEIGHT};
    const SDL_Rect knob  = {slider->x + fill - (KNOB_WIDTH / 2), slider->y, KNOB_WIDTH, SLIDER_HEIGHT};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 40, 35, 30, 230);
    SDL_RenderFillRect(sdl_renderer, &back);
    SDL_SetRenderDrawColor(sdl_renderer, 150, 120, 60, 255);
    SDL_RenderFillRect(sdl_renderer, &done);
    SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
    SDL_RenderDrawRect(sdl_renderer, &back);
    SDL_SetRenderDrawColor(sdl_renderer, slider->dragging ? 255 : 220, slider->dragging ? 230 : 190, 120, 255);
    SDL_RenderFillRect(sdl_renderer, &knob);
}
