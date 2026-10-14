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
    INPUT_MOUSE_MOVE,   // value: 0 left, 1 right, 2 up, 3 down (moving the mouse that way)
} InputKind;

typedef struct InputPress {
    InputKind kind;
    int       value;
    uint16_t  mods; // SDL_Keymod held at the time
    uint64_t  time; // ms
} InputPress;

#define INPUT_PAD_AXIS_THRESHOLD 0.5f  // a stick or trigger counts as pressed past half way
#define INPUT_PAD_DEAD_ZONE      0.15f // and not at all this close to the middle

void InputManager_InitializeSingleton(void);
void InputManager_DestroySingleton(void);
// Reads the mouse's speed (across, up/down) and the wheel's speed from the settings
void        InputManager_ApplyMouseSettings(void);
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
float    InputManager_GetAmount(InputKind kind,
                                int       value); // 0..1: how far a stick is pushed, how fast the mouse moves; 1 held
uint16_t InputManager_GetMods(void);
float    InputManager_GetPadAxis(SDL_GameControllerAxis axis); // -1..1, the strongest of the connected pads

#endif // ABYSS_INPUT_MANAGER_H
