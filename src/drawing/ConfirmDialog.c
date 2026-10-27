#include "ConfirmDialog.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Button.h"
#include "Label.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIALOG_W    440
#define DIALOG_X    ((800 - DIALOG_W) / 2)
#define TEXT_W      (DIALOG_W - 40)
#define MAX_LINES   4
#define LINE_H      20
#define BUTTON_W    96 // BUTTON_TYPE_CANCEL
#define BUTTON_GAP  16
#define MAX_ANSWERS 3

struct ConfirmDialog {
    bool    open;
    Label  *title_label;
    Label  *text_label;
    char    title[96];
    char    lines[MAX_LINES][128];
    int     line_count;
    Button *buttons[MAX_ANSWERS];
    int     answer_count;
    int     y; // top of the panel
    int     height;
    bool    escape_closes;
};

ConfirmDialog *ConfirmDialog_Create(void) {
    ConfirmDialog *dialog = calloc(1, sizeof(ConfirmDialog));
    FAIL_IF_NULL(dialog);
    dialog->title_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(dialog->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(dialog->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    dialog->text_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(dialog->text_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int b = 0; b < MAX_ANSWERS; b++) {
        dialog->buttons[b] = Button_Create(BUTTON_TYPE_CANCEL, "");
    }
    return dialog;
}

void ConfirmDialog_Destroy(ConfirmDialog **dialog) {
    if (dialog == NULL || *dialog == NULL) {
        return;
    }
    Label_Destroy(&(*dialog)->title_label);
    Label_Destroy(&(*dialog)->text_label);
    for (int b = 0; b < MAX_ANSWERS; b++) {
        Button_Destroy(&(*dialog)->buttons[b]);
    }
    free(*dialog);
    *dialog = NULL;
}

// Word wrap into the panel's width
static void ConfirmDialog__Wrap(ConfirmDialog *dialog, const char *text) {
    dialog->line_count = 0;
    char line[128]     = "";
    for (const char *word = text; *word != '\0' && dialog->line_count < MAX_LINES;) {
        while (*word == ' ') {
            word++;
        }
        const char *end = word;
        while (*end != '\0' && *end != ' ') {
            end++;
        }
        if (end == word) {
            break;
        }
        char attempt[256];
        snprintf(attempt, sizeof(attempt), "%s%s%.*s", line, line[0] != '\0' ? " " : "", (int)(end - word), word);
        if (line[0] != '\0' && Label_MeasureText(dialog->text_label, attempt) > TEXT_W) {
            snprintf(dialog->lines[dialog->line_count++], sizeof(dialog->lines[0]), "%s", line);
            snprintf(line, sizeof(line), "%.*s", (int)(end - word), word);
        } else {
            snprintf(line, sizeof(line), "%.127s", attempt);
        }
        word = end;
    }
    if (line[0] != '\0' && dialog->line_count < MAX_LINES) {
        snprintf(dialog->lines[dialog->line_count++], sizeof(dialog->lines[0]), "%s", line);
    }
}

void ConfirmDialog_Open(ConfirmDialog *dialog, const char *title, const char *text, const char *first,
                        const char *second, const char *third) {
    snprintf(dialog->title, sizeof(dialog->title), "%s", title != NULL ? title : "");
    ConfirmDialog__Wrap(dialog, text != NULL ? text : "");
    const char *answers[MAX_ANSWERS] = {first, second, third};
    dialog->answer_count             = 0;
    for (int b = 0; b < MAX_ANSWERS && answers[b] != NULL; b++) {
        Button_SetText(dialog->buttons[b], answers[b]);
        dialog->answer_count++;
    }
    dialog->height      = 70 + (dialog->line_count * LINE_H) + 50;
    dialog->y           = (600 - dialog->height) / 2;
    const int row_w     = (dialog->answer_count * BUTTON_W) + ((dialog->answer_count - 1) * BUTTON_GAP);
    const int row_x     = DIALOG_X + ((DIALOG_W - row_w) / 2);
    const int buttons_y = dialog->y + dialog->height - 48;
    for (int b = 0; b < dialog->answer_count; b++) {
        Button_SetPosition(dialog->buttons[b], row_x + (b * (BUTTON_W + BUTTON_GAP)), buttons_y);
    }
    dialog->open = true;
    InputManager_ClearPresses();
}

bool ConfirmDialog_IsOpen(const ConfirmDialog *dialog) { return dialog != NULL && dialog->open; }

void ConfirmDialog_EscapeCloses(ConfirmDialog *dialog, const bool closes) { dialog->escape_closes = closes; }

ConfirmAnswer ConfirmDialog_Update(ConfirmDialog *dialog) {
    if (!ConfirmDialog_IsOpen(dialog)) {
        return CONFIRM_NONE;
    }
    ConfirmAnswer answer = CONFIRM_NONE;
    for (int b = 0; b < dialog->answer_count; b++) {
        if (Button_Update(dialog->buttons[b]) && answer == CONFIRM_NONE) {
            answer = (ConfirmAnswer)b;
        }
    }
    if (answer == CONFIRM_NONE &&
        (InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN) || InputManager_ConsumeKeyPress(SDL_SCANCODE_KP_ENTER))) {
        answer = CONFIRM_FIRST;
    }
    if (answer == CONFIRM_NONE && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        if (dialog->escape_closes) {
            dialog->open = false;
            InputManager_ClearPresses();
            return CONFIRM_NONE;
        }
        answer = (ConfirmAnswer)(dialog->answer_count - 1);
    }
    InputManager_ClearPresses(); // nothing else reacts to keys while it's open
    if (answer != CONFIRM_NONE) {
        dialog->open = false;
    }
    return answer;
}

void ConfirmDialog_Draw(ConfirmDialog *dialog) {
    if (!ConfirmDialog_IsOpen(dialog)) {
        return;
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(sdl_renderer, NULL);
    const SDL_Rect panel = {DIALOG_X, dialog->y, DIALOG_W, dialog->height};
    SDL_SetRenderDrawColor(sdl_renderer, 12, 10, 8, 245);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);
    const SDL_Rect inner = {panel.x + 3, panel.y + 3, panel.w - 6, panel.h - 6};
    SDL_SetRenderDrawColor(sdl_renderer, 0x6A, 0x5A, 0x38, 255);
    SDL_RenderDrawRect(sdl_renderer, &inner);

    Label_SetText(dialog->title_label, dialog->title);
    Label_Draw(dialog->title_label, 400, dialog->y + 26);
    for (int l = 0; l < dialog->line_count; l++) {
        Label_SetText(dialog->text_label, dialog->lines[l]);
        Label_Draw(dialog->text_label, 400, dialog->y + 58 + (l * LINE_H));
    }
    for (int b = 0; b < dialog->answer_count; b++) {
        Button_Draw(dialog->buttons[b]);
    }
}
