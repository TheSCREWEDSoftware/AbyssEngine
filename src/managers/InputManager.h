#ifndef ABYSS_INPUT_MANAGER_H
#define ABYSS_INPUT_MANAGER_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// Anything that can be bound to an action (see game/Controls.h)
typedef enum InputKind {
    INPUT_NONE,
    INPUT_KEY,          // value: SDL scancode
    INPUT_MOUSE_BUTTON, // value: SDL_BUTTON_LEFT .. SDL_BUTTON_X2
    INPUT_MOUSE_WHEEL,  // value: +1 up, -1 down
    INPUT_PAD_BUTTON,   // value: SDL_GameControllerButton
    INPUT_PAD_AXIS,     // value: axis * 2 + 1 for the positive direction, + 0 for the negative
} InputKind;

typedef struct InputPress {
    InputKind kind;
    int       value;
    uint16_t  mods; // SDL_Keymod held at the time
    uint64_t  time; // ms
} InputPress;

#define INPUT_PAD_AXIS_THRESHOLD 0.5f // a stick or trigger counts as pressed past half way

void        InputManager_InitializeSingleton(void);
void        InputManager_DestroySingleton(void);
bool        InputManager_ProcessSdlEvent(SDL_Event *sdl_event);
void        InputManager_StartTextInput(void);
void        InputManager_StopTextInput(void);
bool        InputManager_IsTextInputActive(void);
void        InputManager_GetMousePosition(int *mouse_x, int *mouse_y);
void        InputManager_GetMouseButtons(bool *left, bool *middle, bool *right);
void        InputManager_ResetMouseButtons(void);
const char *InputManager_GetTextInput(void);                     // text typed since StartTextInput (backspace aware)
void        InputManager_SetTextInput(const char *text);         // replaces the text input buffer
bool        InputManager_ConsumeKeyPress(SDL_Scancode scancode); // true once per key down (repeats included)
int         InputManager_ConsumeMouseWheel(void);                // wheel steps since the last call (positive = up)

// For bindings: every press (key, mouse button, wheel step, gamepad button, stick past half way) in order, once each
bool     InputManager_PollPress(InputPress *press); // false when there are no more
void     InputManager_ClearPresses(void);
bool     InputManager_IsDown(InputKind kind, int value); // held now (wheel steps never are)
uint16_t InputManager_GetMods(void);
float    InputManager_GetPadAxis(SDL_GameControllerAxis axis); // -1..1, the strongest of the connected pads

#endif // ABYSS_INPUT_MANAGER_H
