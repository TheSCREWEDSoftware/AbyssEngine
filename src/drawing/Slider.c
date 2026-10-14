#include "Slider.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include "TextBox.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLIDER_HEIGHT 16
#define TRACK_HEIGHT  6
#define KNOB_WIDTH    8
#define BOX_GAP       14 // between the track and the value box
#define BOX_WIDTH     64
#define BOX_HEIGHT    22

struct Slider {
    int      x;
    int      y;
    int      width;
    int      minimum;
    int      maximum;
    int      value;
    bool     dragging;
    bool     prev_left;
    int      divisor;  // shown as value / divisor (100: 125 shows as 1.25)
    int      decimals; // places shown after the point
    char     suffix[8];
    char     end_names[2][24]; // shown at the ends instead of the minimum and maximum
    bool     typable;
    TextBox *box;
    Label   *label;
};

static void Slider__Format(const Slider *slider, const int value, char *text, const size_t size) {
    snprintf(text, size, "%.*f", slider->decimals, (double)value / (double)slider->divisor);
}

static void Slider__SyncBox(Slider *slider) {
    char text[32];
    Slider__Format(slider, slider->value, text, sizeof(text));
    if (strcmp(text, TextBox_GetText(slider->box)) != 0) {
        TextBox_SetText(slider->box, text);
    }
}

Slider *Slider_Create(const int x, const int y, const int width, const int minimum, const int maximum) {
    Slider *slider = calloc(1, sizeof(Slider));
    FAIL_IF_NULL(slider);
    slider->x       = x;
    slider->y       = y;
    slider->width   = width;
    slider->minimum = minimum;
    slider->maximum = maximum > minimum ? maximum : minimum + 1;
    slider->value   = minimum;
    slider->divisor = 1;
    slider->typable = true;
    slider->box     = TextBox_Create(FONTS_FONT16, x + width + BOX_GAP, y + (SLIDER_HEIGHT / 2) - (BOX_HEIGHT / 2),
                                     BOX_WIDTH, BOX_HEIGHT, "");
    slider->label   = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Slider__SyncBox(slider);
    return slider;
}

void Slider_Destroy(Slider **slider) {
    if (slider != NULL && *slider != NULL) {
        TextBox_Destroy(&(*slider)->box);
        Label_Destroy(&(*slider)->label);
        free(*slider);
        *slider = NULL;
    }
}

void Slider_SetFormat(Slider *slider, const int divisor, const int decimals, const char *suffix) {
    slider->divisor  = divisor > 0 ? divisor : 1;
    slider->decimals = decimals;
    snprintf(slider->suffix, sizeof(slider->suffix), "%s", suffix != NULL ? suffix : "");
    Slider__SyncBox(slider);
}

void Slider_SetEndNames(Slider *slider, const char *minimum, const char *maximum) {
    snprintf(slider->end_names[0], sizeof(slider->end_names[0]), "%s", minimum != NULL ? minimum : "");
    snprintf(slider->end_names[1], sizeof(slider->end_names[1]), "%s", maximum != NULL ? maximum : "");
}

void Slider_SetTypable(Slider *slider, const bool typable) { slider->typable = typable; }

bool Slider_IsTyping(const Slider *slider) { return slider->typable && TextBox_IsFocused(slider->box); }

void Slider_SetValue(Slider *slider, const int value) {
    slider->value = value < slider->minimum ? slider->minimum : (value > slider->maximum ? slider->maximum : value);
    if (!TextBox_IsFocused(slider->box)) {
        Slider__SyncBox(slider);
    }
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
    if (slider->typable) {
        // Typed: used as soon as it reads as a number (the box keeps what's typed until it loses the focus)
        if (TextBox_Update(slider->box)) {
            const char  *text  = TextBox_GetText(slider->box);
            char        *end   = NULL;
            const double typed = strtod(text, &end);
            if (end != text) {
                const int value = (int)lround(typed * (double)slider->divisor);
                slider->value =
                    value < slider->minimum ? slider->minimum : (value > slider->maximum ? slider->maximum : value);
            }
        }
        if (!TextBox_IsFocused(slider->box)) {
            Slider__SyncBox(slider); // tidy what was typed ("1.5" to "1.50", out of range to the end)
        }
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

    // The ends: the minimum and the maximum (or their names), under the track
    char text[32];
    Label_SetColor(slider->label, 0x90, 0x90, 0x90, 0xFF);
    for (int end = 0; end < 2; end++) {
        if (slider->end_names[end][0] != '\0') {
            snprintf(text, sizeof(text), "%s", slider->end_names[end]);
        } else {
            Slider__Format(slider, end == 0 ? slider->minimum : slider->maximum, text, sizeof(text));
        }
        Label_SetText(slider->label, text);
        Label_SetAlignment(slider->label, end == 0 ? LABEL_ALIGN_BEGIN : LABEL_ALIGN_END, LABEL_ALIGN_CENTER);
        Label_Draw(slider->label, end == 0 ? slider->x : slider->x + slider->width, slider->y + SLIDER_HEIGHT + 6);
    }

    // The value, typable
    if (slider->typable) {
        TextBox_Draw(slider->box);
        if (slider->suffix[0] != '\0') {
            Label_SetText(slider->label, slider->suffix);
            Label_SetColor(slider->label, 0xFF, 0xFF, 0xFF, 0xFF);
            Label_SetAlignment(slider->label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
            Label_Draw(slider->label, slider->x + slider->width + BOX_GAP + BOX_WIDTH + 5,
                       slider->y + (SLIDER_HEIGHT / 2));
        }
    }
}
