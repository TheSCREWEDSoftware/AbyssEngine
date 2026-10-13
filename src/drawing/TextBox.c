#include "TextBox.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_BOX_MAX_LENGTH 64
#define TEXT_BOX_PADDING_X  6

struct TextBox {
    Label *text_label;
    Label *placeholder_label;
    Label *caret_label;
    char   text[TEXT_BOX_MAX_LENGTH + 1];
    int    x;
    int    y;
    int    width;
    int    height;
    bool   focused;
    bool   prev_mouse_left;
};

TextBox *TextBox_Create(const char *font_path, const int x, const int y, const int width, const int height,
                        const char *placeholder) {
    TextBox *text_box = malloc(sizeof(TextBox));
    FAIL_IF_NULL(text_box);
    memset(text_box, 0, sizeof(TextBox));

    text_box->x      = x;
    text_box->y      = y;
    text_box->width  = width;
    text_box->height = height;

    text_box->text_label = Label_Create(font_path, PALETTE_STATIC);
    Label_SetAlignment(text_box->text_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);

    text_box->placeholder_label = Label_Create(font_path, PALETTE_STATIC);
    Label_SetText(text_box->placeholder_label, placeholder != NULL ? placeholder : "");
    Label_SetAlignment(text_box->placeholder_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);

    text_box->caret_label = Label_Create(font_path, PALETTE_STATIC);
    Label_SetText(text_box->caret_label, "_");
    Label_SetAlignment(text_box->caret_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);

    return text_box;
}

void TextBox_Destroy(TextBox **text_box) {
    if (text_box == NULL || *text_box == NULL) {
        return;
    }

    TextBox_Blur(*text_box);
    Label_Destroy(&(*text_box)->text_label);
    Label_Destroy(&(*text_box)->placeholder_label);
    Label_Destroy(&(*text_box)->caret_label);
    free(*text_box);
    *text_box = NULL;
}

void TextBox_Focus(TextBox *text_box) {
    if (text_box->focused) {
        return;
    }

    text_box->focused = true;
    InputManager_StartTextInput();
    InputManager_SetTextInput(text_box->text);
}

bool TextBox_IsFocused(const TextBox *text_box) { return text_box != NULL && text_box->focused; }

void TextBox_Blur(TextBox *text_box) {
    if (!text_box->focused) {
        return;
    }

    text_box->focused = false;
    InputManager_StopTextInput();
}

const char *TextBox_GetText(const TextBox *text_box) { return text_box->text; }

void TextBox_Clear(TextBox *text_box) {
    text_box->text[0] = '\0';
    Label_SetText(text_box->text_label, "");
    if (text_box->focused) {
        InputManager_SetTextInput("");
    }
}

bool TextBox_Update(TextBox *text_box) {
    int  mouse_x;
    int  mouse_y;
    bool mouse_left;

    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool was_mouse_left = text_box->prev_mouse_left;
    text_box->prev_mouse_left = mouse_left;

    if (mouse_left && !was_mouse_left) {
        const bool hovered = mouse_x >= text_box->x && mouse_x < text_box->x + text_box->width &&
                             mouse_y >= text_box->y && mouse_y < text_box->y + text_box->height;
        if (hovered) {
            TextBox_Focus(text_box);
        }
    }

    if (!text_box->focused) {
        return false;
    }

    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE) && text_box->text[0] != '\0') {
        TextBox_Clear(text_box);
        return true;
    }

    // Keep only characters the game fonts can draw.
    char        filtered[TEXT_BOX_MAX_LENGTH + 1];
    size_t      length = 0;
    const char *input  = InputManager_GetTextInput();
    for (const unsigned char *ch = (const unsigned char *)input; *ch != '\0' && length < TEXT_BOX_MAX_LENGTH; ch++) {
        if (*ch >= 0x20 && *ch < 0x7F) {
            filtered[length++] = (char)*ch;
        }
    }
    filtered[length] = '\0';

    if (strcmp(filtered, text_box->text) == 0) {
        return false;
    }

    snprintf(text_box->text, sizeof(text_box->text), "%s", filtered);
    Label_SetText(text_box->text_label, text_box->text);
    return true;
}

void TextBox_Draw(const TextBox *text_box) {
    const SDL_Rect rect = {text_box->x, text_box->y, text_box->width, text_box->height};

    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(sdl_renderer, &rect);
    if (text_box->focused) {
        SDL_SetRenderDrawColor(sdl_renderer, 199, 179, 119, 255);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
    }
    SDL_RenderDrawRect(sdl_renderer, &rect);

    const int text_x = text_box->x + TEXT_BOX_PADDING_X;
    const int text_y = text_box->y + (text_box->height / 2);
    int       text_w = 0;

    if (text_box->text[0] == '\0') {
        Label_SetColor(text_box->placeholder_label, 0x80, 0x80, 0x80, 0xFF);
        Label_Draw(text_box->placeholder_label, text_x, text_y);
    } else {
        Label_SetColor(text_box->text_label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(text_box->text_label, text_x, text_y);
        Label_GetSize(text_box->text_label, &text_w, NULL);
    }

    // Blinking caret
    if (text_box->focused && (SDL_GetTicks64() / 500) % 2 == 0) {
        Label_SetColor(text_box->caret_label, 0xC7, 0xB3, 0x77, 0xFF);
        Label_Draw(text_box->caret_label, text_x + text_w + 1, text_y);
    }
}
