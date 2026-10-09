#ifndef ABYSS_BUTTON_H
#define ABYSS_BUTTON_H

#include <stdbool.h>

typedef struct Button Button;

typedef enum ButtonToolTip {
    BUTTON_TOOL_TIP_NONE,
    BUTTON_TOOL_TIP_CLOSE,
    BUTTON_TOOL_TIP_OK,
    BUTTON_TOOL_TIP_BUY,
    BUTTON_TOOL_TIP_SELL,
    BUTTON_TOOL_TIP_REPAIR,
    BUTTON_TOOL_TIP_REPAIR_ALL,
    BUTTON_TOOL_TIP_LEFT_ARROW,
    BUTTON_TOOL_TIP_RIGHT_ARROW,
    BUTTON_TOOL_TIP_QUERY,
    BUTTON_TOOL_TIP_SQUELCH_CHAT
} ButtonToolTip;

typedef enum ButtonType {
    BUTTON_TYPE_WIDE,   // 272x35, main menu
    BUTTON_TYPE_MEDIUM, // 128x35
    BUTTON_TYPE_SHORT,  // 135x25, main menu credits/cinematics
    BUTTON_TYPE_CANCEL, // 96x32
} ButtonType;

typedef void (*ButtonClickHandler)(void *user_data);

Button *Button_Create(ButtonType button_type, const char *text);
void    Button_Destroy(Button **button);
void    Button_SetPosition(Button *button, int x, int y);
void    Button_GetSize(const Button *button, int *width, int *height);
void    Button_SetEnabled(Button *button, bool enabled);
void    Button_SetVisible(Button *button, bool visible);
void    Button_SetClickHandler(Button *button, ButtonClickHandler handler, void *user_data);

// Processes mouse input. Returns true (and calls the click handler) when the button was clicked this frame.
bool Button_Update(Button *button);
void Button_Draw(const Button *button);

#endif // ABYSS_BUTTON_H
