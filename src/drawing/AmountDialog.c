#include "AmountDialog.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Button.h"
#include "Label.h"
#include "TextBox.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIALOG_W   340
#define DIALOG_H   170
#define DIALOG_X   ((800 - DIALOG_W) / 2)
#define DIALOG_Y   ((600 - DIALOG_H) / 2)
#define BOX_W      160
#define BOX_H      26
#define BUTTON_W   96 // BUTTON_TYPE_CANCEL
#define BUTTON_GAP 24

struct AmountDialog {
    bool     open;
    Label   *title_label;
    Label   *text_label;
    char     title[64];
    char     text[96];
    TextBox *box;
    Button  *ok;
    Button  *cancel;
    int      maximum;
    int      value;
    int      tag;
};

AmountDialog *AmountDialog_Create(void) {
    AmountDialog *dialog = calloc(1, sizeof(AmountDialog));
    FAIL_IF_NULL(dialog);
    dialog->title_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(dialog->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(dialog->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    dialog->text_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(dialog->text_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    dialog->box     = TextBox_Create(FONTS_FONT16, 400 - (BOX_W / 2), DIALOG_Y + 76, BOX_W, BOX_H, "0");
    dialog->ok      = Button_Create(BUTTON_TYPE_CANCEL, "OK");
    dialog->cancel  = Button_Create(BUTTON_TYPE_CANCEL, "Cancel");
    const int row_x = 400 - BUTTON_W - (BUTTON_GAP / 2);
    Button_SetPosition(dialog->ok, row_x, DIALOG_Y + DIALOG_H - 46);
    Button_SetPosition(dialog->cancel, row_x + BUTTON_W + BUTTON_GAP, DIALOG_Y + DIALOG_H - 46);
    return dialog;
}

void AmountDialog_Destroy(AmountDialog **dialog) {
    if (dialog == NULL || *dialog == NULL) {
        return;
    }
    Label_Destroy(&(*dialog)->title_label);
    Label_Destroy(&(*dialog)->text_label);
    TextBox_Destroy(&(*dialog)->box);
    Button_Destroy(&(*dialog)->ok);
    Button_Destroy(&(*dialog)->cancel);
    free(*dialog);
    *dialog = NULL;
}

static void AmountDialog__Set(AmountDialog *dialog, int value) {
    value         = value < 0 ? 0 : (value > dialog->maximum ? dialog->maximum : value);
    dialog->value = value;
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    TextBox_SetText(dialog->box, text);
}

void AmountDialog_Open(AmountDialog *dialog, const char *title, const char *text, const int maximum, const int start,
                       const int tag) {
    snprintf(dialog->title, sizeof(dialog->title), "%s", title != NULL ? title : "");
    snprintf(dialog->text, sizeof(dialog->text), "%s", text != NULL ? text : "");
    dialog->maximum = maximum > 0 ? maximum : 0;
    dialog->tag     = tag;
    AmountDialog__Set(dialog, start);
    TextBox_Focus(dialog->box);
    dialog->open = true;
    InputManager_ClearPresses();
    InputManager_ConsumeMouseWheel();
}

bool AmountDialog_IsOpen(const AmountDialog *dialog) { return dialog != NULL && dialog->open; }

int AmountDialog_Tag(const AmountDialog *dialog) { return dialog->tag; }

int AmountDialog_Value(const AmountDialog *dialog) { return dialog->value; }

AmountAnswer AmountDialog_Update(AmountDialog *dialog) {
    if (!AmountDialog_IsOpen(dialog)) {
        return AMOUNT_NONE;
    }
    AmountAnswer answer = AMOUNT_NONE;
    TextBox_Focus(dialog->box);
    TextBox_Update(dialog->box);
    // Only digits, at most the maximum
    const char *typed = TextBox_GetText(dialog->box);
    long        value = 0;
    bool        clean = true;
    for (const char *c = typed; *c != '\0'; c++) {
        if (*c >= '0' && *c <= '9') {
            value = value < 100000000 ? (value * 10) + (*c - '0') : value;
        } else {
            clean = false;
        }
    }
    if (!clean || value > dialog->maximum) {
        AmountDialog__Set(dialog, value > dialog->maximum ? dialog->maximum : (int)value);
    } else {
        dialog->value = (int)value;
    }
    const int wheel  = InputManager_ConsumeMouseWheel();
    int       step   = wheel;
    step            += InputManager_ConsumeKeyPress(SDL_SCANCODE_UP) ? 1 : 0;
    step            -= InputManager_ConsumeKeyPress(SDL_SCANCODE_DOWN) ? 1 : 0;
    step            += InputManager_ConsumeKeyPress(SDL_SCANCODE_PAGEUP) ? 1000 : 0;
    step            -= InputManager_ConsumeKeyPress(SDL_SCANCODE_PAGEDOWN) ? 1000 : 0;
    if (step != 0) {
        AmountDialog__Set(dialog, dialog->value + step);
    }
    if (Button_Update(dialog->ok)) {
        answer = AMOUNT_OK;
    }
    if (Button_Update(dialog->cancel) && answer == AMOUNT_NONE) {
        answer = AMOUNT_CANCEL;
    }
    if (answer == AMOUNT_NONE &&
        (InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN) || InputManager_ConsumeKeyPress(SDL_SCANCODE_KP_ENTER))) {
        answer = AMOUNT_OK;
    }
    if (answer == AMOUNT_NONE && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        answer = AMOUNT_CANCEL;
    }
    InputManager_ClearPresses(); // nothing else reacts to keys while it's open
    if (answer != AMOUNT_NONE) {
        dialog->open = false;
        TextBox_Blur(dialog->box);
    }
    return answer;
}

void AmountDialog_Draw(AmountDialog *dialog) {
    if (!AmountDialog_IsOpen(dialog)) {
        return;
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 120);
    SDL_RenderFillRect(sdl_renderer, NULL);
    const SDL_Rect panel = {DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H};
    SDL_SetRenderDrawColor(sdl_renderer, 12, 10, 8, 245);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);
    const SDL_Rect inner = {panel.x + 3, panel.y + 3, panel.w - 6, panel.h - 6};
    SDL_SetRenderDrawColor(sdl_renderer, 0x6A, 0x5A, 0x38, 255);
    SDL_RenderDrawRect(sdl_renderer, &inner);
    Label_SetText(dialog->title_label, dialog->title);
    Label_Draw(dialog->title_label, 400, DIALOG_Y + 24);
    Label_SetText(dialog->text_label, dialog->text);
    Label_Draw(dialog->text_label, 400, DIALOG_Y + 54);
    TextBox_Draw(dialog->box);
    Button_Draw(dialog->ok);
    Button_Draw(dialog->cancel);
}
