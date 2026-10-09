#ifndef ABYSS_INPUT_MANAGER_H
#define ABYSS_INPUT_MANAGER_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

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

#endif // ABYSS_INPUT_MANAGER_H
