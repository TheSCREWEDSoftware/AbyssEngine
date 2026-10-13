#include "Button.h"
#include "../audio/Sounds.h"

#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include "Sprite.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ButtonLayout {
    ButtonType  button_type;
    const char *resource_name;
    const char *palette_name;
    const char *font_path;
    uint8_t     segments_x;
    uint8_t     segments_y;
    int         base_frame;
    int         text_offset;
    uint32_t    label_color;
    uint32_t    disabled_color;
    bool        allow_frame_change;
} ButtonLayout;

#define BUTTON_COLOR_GREY            0x646464FF
#define BUTTON_COLOR_LIGHT_GREY_A75  0x808080C3
#define BUTTON_PRESSED_TEXT_OFFSET_X (-2)
#define BUTTON_PRESSED_TEXT_OFFSET_Y 2
#define BUTTON_TEXT_MARGIN           34 // room for the button's end caps

// Frame layout of the *ButtonBlank.dc6 files: all segments of the normal state, followed by all segments of the
// pressed state.
static const ButtonLayout button_layouts[] = {
    {.button_type        = BUTTON_TYPE_WIDE,
     .resource_name      = BUTTON_WIDE_BLANK,
     .palette_name       = PALETTE_UNITS,
     .font_path          = FONTS_FONTEXOCET10,
     .segments_x         = 2,
     .segments_y         = 1,
     .text_offset        = 1,
     .label_color        = BUTTON_COLOR_GREY,
     .disabled_color     = BUTTON_COLOR_LIGHT_GREY_A75,
     .allow_frame_change = true},
    {.button_type        = BUTTON_TYPE_MEDIUM,
     .resource_name      = BUTTON_MEDIUM_BLANK,
     .palette_name       = PALETTE_UNITS,
     .font_path          = FONTS_FONTEXOCET10,
     .segments_x         = 1,
     .segments_y         = 1,
     .text_offset        = 1,
     .label_color        = BUTTON_COLOR_GREY,
     .disabled_color     = BUTTON_COLOR_LIGHT_GREY_A75,
     .allow_frame_change = true},
    {.button_type        = BUTTON_TYPE_SHORT,
     .resource_name      = BUTTON_SHORT_BLANK,
     .palette_name       = PALETTE_UNITS,
     .font_path          = FONTS_FONTREDICULOUS,
     .segments_x         = 1,
     .segments_y         = 1,
     .text_offset        = -1,
     .label_color        = BUTTON_COLOR_GREY,
     .disabled_color     = BUTTON_COLOR_LIGHT_GREY_A75,
     .allow_frame_change = true},
    {.button_type        = BUTTON_TYPE_CANCEL,
     .resource_name      = BUTTON_CANCEL_BLANK,
     .palette_name       = PALETTE_UNITS,
     .font_path          = FONTS_FONTEXOCET10,
     .segments_x         = 1,
     .segments_y         = 1,
     .text_offset        = 1,
     .label_color        = BUTTON_COLOR_GREY,
     .disabled_color     = BUTTON_COLOR_LIGHT_GREY_A75,
     .allow_frame_change = true},
    {.button_type        = BUTTON_TYPE_TALL,
     .resource_name      = BUTTON_TALL_BLANK,
     .palette_name       = PALETTE_UNITS,
     .font_path          = FONTS_FONTEXOCET10,
     .segments_x         = 1,
     .segments_y         = 1,
     .text_offset        = 1,
     .label_color        = BUTTON_COLOR_GREY,
     .disabled_color     = BUTTON_COLOR_LIGHT_GREY_A75,
     .allow_frame_change = true},
};

#define BUTTON_LAYOUT_COUNT (sizeof(button_layouts) / sizeof(button_layouts[0]))

struct Button {
    const ButtonLayout *layout;
    Sprite             *sprite;
    Label              *label;
    ButtonClickHandler  on_click;
    void               *user_data;
    int                 x;
    int                 y;
    int                 width;
    int                 height;
    bool                enabled;
    bool                visible;
    bool                toggled; // drawn pressed in, e.g. the active tab of a group
    bool                hovered;
    bool                armed;           // left mouse button went down while over this button
    bool                prev_mouse_left; // left mouse button state on the previous update
};

static const ButtonLayout *Button__GetLayout(ButtonType button_type);
static void                Button__ApplyLabelColor(const Button *button, uint32_t rgba);

Button *Button_Create(const ButtonType button_type, const char *text) {
    Button *button = malloc(sizeof(Button));
    FAIL_IF_NULL(button);
    memset(button, 0, sizeof(Button));

    button->layout  = Button__GetLayout(button_type);
    button->enabled = true;
    button->visible = true;
    button->sprite  = Sprite_Create(button->layout->resource_name, button->layout->palette_name);

    for (int i = 0; i < button->layout->segments_x; i++) {
        int w;
        Sprite_GetFrameSize(button->sprite, button->layout->base_frame + i, &w, NULL);
        button->width += w;
    }

    for (int i = 0; i < button->layout->segments_y; i++) {
        int h;
        Sprite_GetFrameSize(button->sprite, button->layout->base_frame + (i * button->layout->segments_x), NULL, &h);
        button->height += h;
    }

    Button_SetText(button, text);
    return button;
}

void Button_SetText(Button *button, const char *text) {
    // The button fonts only contain upper case glyphs.
    char *upper_text = strdup(text != NULL ? text : "");
    FAIL_IF_NULL(upper_text);
    for (char *ch = upper_text; *ch != '\0'; ch++) {
        *ch = (char)toupper((unsigned char)*ch);
    }

    if (button->label != NULL) {
        Label_Destroy(&button->label);
    }
    button->label = Label_Create(button->layout->font_path, PALETTE_UNITS);
    Label_SetText(button->label, upper_text);

    // Text too wide for the button: fall back to smaller fonts
    static const char *fallback_fonts[] = {FONTS_FONTREDICULOUS, FONTS_FONTEXOCET8};
    for (size_t i = 0; i < sizeof(fallback_fonts) / sizeof(fallback_fonts[0]); i++) {
        int text_w = 0;
        Label_GetSize(button->label, &text_w, NULL);
        if (text_w <= button->width - BUTTON_TEXT_MARGIN) {
            break;
        }
        Label_Destroy(&button->label);
        button->label = Label_Create(fallback_fonts[i], PALETTE_UNITS);
        Label_SetText(button->label, upper_text);
    }

    Label_SetAlignment(button->label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    free(upper_text);
}

void Button_Destroy(Button **button) {
    if (button == NULL || *button == NULL) {
        return;
    }

    Label_Destroy(&(*button)->label);
    Sprite_Destroy(&(*button)->sprite);

    free(*button);
    *button = NULL;
}

void Button_SetPosition(Button *button, const int x, const int y) {
    button->x = x;
    button->y = y;
}

void Button_GetSize(const Button *button, int *width, int *height) {
    if (width != NULL) {
        *width = button->width;
    }

    if (height != NULL) {
        *height = button->height;
    }
}

void Button_SetEnabled(Button *button, const bool enabled) {
    button->enabled = enabled;
    if (!enabled) {
        button->armed = false;
    }
}

void Button_SetVisible(Button *button, const bool visible) {
    button->visible = visible;
    if (!visible) {
        button->armed = false;
    }
}

void Button_SetToggled(Button *button, const bool toggled) { button->toggled = toggled; }

bool Button_IsToggled(const Button *button) { return button->toggled; }

void Button_SetClickHandler(Button *button, const ButtonClickHandler handler, void *user_data) {
    button->on_click  = handler;
    button->user_data = user_data;
}

bool Button_Update(Button *button) {
    int  mouse_x;
    int  mouse_y;
    bool mouse_left;

    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool was_mouse_left = button->prev_mouse_left;
    button->prev_mouse_left   = mouse_left;

    if (!button->visible || !button->enabled) {
        button->hovered = false;
        button->armed   = false;
        return false;
    }

    button->hovered = mouse_x >= button->x && mouse_x < button->x + button->width && mouse_y >= button->y &&
                      mouse_y < button->y + button->height;

    // Press started on this button
    if (mouse_left && !was_mouse_left && button->hovered) {
        button->armed = true;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        return false;
    }

    // Released: it's a click only if the press started here and the mouse is still over the button
    if (!mouse_left && was_mouse_left) {
        const bool clicked = button->armed && button->hovered;
        button->armed      = false;

        if (clicked) {
            if (button->on_click != NULL) {
                button->on_click(button->user_data);
            }
            return true;
        }
    }

    return false;
}

void Button_Draw(const Button *button) {
    if (!button->visible) {
        return;
    }

    const ButtonLayout *layout     = button->layout;
    const int           segments   = layout->segments_x * layout->segments_y;
    const bool          is_pressed = (button->armed && button->hovered) || button->toggled;

    int frame = layout->base_frame;
    if (is_pressed && layout->allow_frame_change) {
        frame += segments;
    }

    Sprite_DrawMulti(button->sprite, (uint8_t)frame, button->x, button->y, layout->segments_x, layout->segments_y);

    int text_x = button->x + (button->width / 2);
    int text_y = button->y + (button->height / 2) + layout->text_offset;
    if (is_pressed) {
        text_x += BUTTON_PRESSED_TEXT_OFFSET_X;
        text_y += BUTTON_PRESSED_TEXT_OFFSET_Y;
    }

    Button__ApplyLabelColor(button, button->enabled ? layout->label_color : layout->disabled_color);
    Label_Draw(button->label, text_x, text_y);
}

static const ButtonLayout *Button__GetLayout(const ButtonType button_type) {
    for (size_t i = 0; i < BUTTON_LAYOUT_COUNT; i++) {
        if (button_layouts[i].button_type == button_type) {
            return &button_layouts[i];
        }
    }

    LOG_FATAL("Could not locate button type %d.", (int)button_type); // exits
}

static void Button__ApplyLabelColor(const Button *button, const uint32_t rgba) {
    Label_SetColor(button->label, (uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba);
}
