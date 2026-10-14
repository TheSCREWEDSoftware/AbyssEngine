#include "InputManager.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXT_INPUT_LENGTH 100
#define MAX_PRESSES           64
#define MAX_PADS              4

typedef struct InputManager {
    int     mouse_x;
    int     mouse_y;
    bool    key_pressed[SDL_NUM_SCANCODES];
    char    text_input[MAX_TEXT_INPUT_LENGTH + 1];
    uint8_t mouse_buttons;
    int     mouse_wheel_y;

    bool       key_down[SDL_NUM_SCANCODES];
    uint16_t   mods;
    InputPress presses[MAX_PRESSES]; // ring buffer
    int        press_head;
    int        press_count;

    SDL_GameController *pads[MAX_PADS];
    bool                pad_buttons[SDL_CONTROLLER_BUTTON_MAX];
    float               pad_axes[MAX_PADS][SDL_CONTROLLER_AXIS_MAX];
    uint64_t            move_time[4];  // mouse movement by direction (left, right, up, down): when it last moved
    float               move_speed[4]; // and how far (pixels in one event)
    uint64_t            move_press[4]; // the last press it made (one every MOVE_PRESS_MS while it keeps moving)
} InputManager;

InputManager *input_manager;

#define MOVE_MIN      2     // pixels in one motion event to count as moving that way
#define MOVE_PRESS    6     // and to count as a press (a flick)
#define MOVE_PRESS_MS 200   // at most one press each way this often
#define MOVE_HOLD_MS  60    // held while it moved that way this recently
#define MOVE_FULL     12.0f // pixels in one event for the full amount

static void InputManager__Press(const InputKind kind, const int value) {
    InputPress *press = &input_manager->presses[(input_manager->press_head + input_manager->press_count) % MAX_PRESSES];
    if (input_manager->press_count == MAX_PRESSES) {
        input_manager->press_head = (input_manager->press_head + 1) % MAX_PRESSES; // nobody's reading: drop the oldest
    } else {
        input_manager->press_count++;
    }
    press->kind  = kind;
    press->value = value;
    press->mods  = input_manager->mods;
    press->time  = SDL_GetTicks64();
}

static uint16_t InputManager__Mods(const uint16_t sdl_mods) {
    return (uint16_t)(sdl_mods & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT));
}

static float InputManager__AxisValue(const int pad, const SDL_GameControllerAxis axis) {
    return input_manager->pad_axes[pad][axis];
}

void InputManager_InitializeSingleton(void) {
    input_manager = malloc(sizeof(InputManager));
    memset(input_manager, 0, sizeof(InputManager));
    // SDL starts with text input on (on Windows and macOS). It's only wanted while a text box has the focus: left on,
    // it opens on-screen keyboards / IME windows, and "is the player typing?" checks would always say yes.
    SDL_StopTextInput();
}

void InputManager_DestroySingleton(void) {
    assert(input_manager != NULL);

    free(input_manager);
}

bool InputManager_ProcessSdlEvent(SDL_Event *sdl_event) {
    switch (sdl_event->type) {
    case SDL_MOUSEMOTION: {
        input_manager->mouse_x = sdl_event->motion.x;
        input_manager->mouse_y = sdl_event->motion.y;
        // Mouse movement as an input (Mouse Move Left / Right / Up / Down)
        const uint64_t now      = SDL_GetTicks64();
        const int      moved[4] = {-sdl_event->motion.xrel, sdl_event->motion.xrel, -sdl_event->motion.yrel,
                                   sdl_event->motion.yrel};
        for (int d = 0; d < 4; d++) {
            if (moved[d] >= MOVE_MIN) {
                input_manager->move_time[d]  = now;
                input_manager->move_speed[d] = (float)moved[d];
                if (moved[d] >= MOVE_PRESS && now - input_manager->move_press[d] >= MOVE_PRESS_MS) {
                    input_manager->move_press[d] = now;
                    InputManager__Press(INPUT_MOUSE_MOVE, d);
                }
            }
        }
        return true;
    }
    case SDL_MOUSEBUTTONDOWN:
        input_manager->mouse_x        = sdl_event->button.x;
        input_manager->mouse_y        = sdl_event->button.y;
        input_manager->mouse_buttons |= SDL_BUTTON(sdl_event->button.button);
        InputManager__Press(INPUT_MOUSE_BUTTON, sdl_event->button.button);
        return true;
    case SDL_MOUSEBUTTONUP:
        input_manager->mouse_x        = sdl_event->button.x;
        input_manager->mouse_y        = sdl_event->button.y;
        input_manager->mouse_buttons &= ~SDL_BUTTON(sdl_event->button.button);
        return true;
    case SDL_MOUSEWHEEL: {
        const int steps =
            (sdl_event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -sdl_event->wheel.y : sdl_event->wheel.y;
        input_manager->mouse_wheel_y += steps;
        for (int s = 0; s < abs(steps) && s < 4; s++) {
            InputManager__Press(INPUT_MOUSE_WHEEL, steps > 0 ? 1 : -1);
        }
        return true;
    }
    case SDL_CONTROLLERDEVICEADDED:
        for (int p = 0; p < MAX_PADS; p++) {
            if (input_manager->pads[p] == NULL) {
                input_manager->pads[p] = SDL_GameControllerOpen(sdl_event->cdevice.which);
                break;
            }
        }
        return true;
    case SDL_CONTROLLERDEVICEREMOVED:
        for (int p = 0; p < MAX_PADS; p++) {
            SDL_GameController *pad = input_manager->pads[p];
            if (pad != NULL && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == sdl_event->cdevice.which) {
                SDL_GameControllerClose(pad);
                input_manager->pads[p] = NULL;
                memset(input_manager->pad_axes[p], 0, sizeof(input_manager->pad_axes[p]));
            }
        }
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
        if (sdl_event->cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
            input_manager->pad_buttons[sdl_event->cbutton.button] = true;
            InputManager__Press(INPUT_PAD_BUTTON, sdl_event->cbutton.button);
        }
        return true;
    case SDL_CONTROLLERBUTTONUP:
        if (sdl_event->cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
            input_manager->pad_buttons[sdl_event->cbutton.button] = false;
        }
        return true;
    case SDL_CONTROLLERAXISMOTION: {
        int pad = 0;
        for (int p = 0; p < MAX_PADS; p++) {
            SDL_GameController *c = input_manager->pads[p];
            if (c != NULL && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == sdl_event->caxis.which) {
                pad = p;
            }
        }
        const int axis = sdl_event->caxis.axis;
        if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) {
            return true;
        }
        const float before                 = InputManager__AxisValue(pad, (SDL_GameControllerAxis)axis);
        const float after                  = (float)sdl_event->caxis.value / 32767.0f;
        input_manager->pad_axes[pad][axis] = after;
        if (before < INPUT_PAD_AXIS_THRESHOLD && after >= INPUT_PAD_AXIS_THRESHOLD) {
            InputManager__Press(INPUT_PAD_AXIS, (axis * 2) + 1);
        }
        if (before > -INPUT_PAD_AXIS_THRESHOLD && after <= -INPUT_PAD_AXIS_THRESHOLD) {
            InputManager__Press(INPUT_PAD_AXIS, axis * 2);
        }
        return true;
    }
    case SDL_KEYDOWN:
        if (sdl_event->key.keysym.sym == SDLK_RETURN &&
            (sdl_event->key.keysym.mod == KMOD_LALT || sdl_event->key.keysym.mod == KMOD_RALT)) {
            const bool is_fullscreen = AbyssConfiguration_GetFullScreen();
            SDL_SetWindowFullscreen(sdl_window, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            AbyssConfiguration_SetFullScreen(!is_fullscreen);
        }
        // if in text input, handle backspace
        if (strlen(input_manager->text_input) > 0 && sdl_event->key.keysym.sym == SDLK_BACKSPACE) {
            input_manager->text_input[strlen(input_manager->text_input) - 1] = '\0';
        }
        input_manager->key_pressed[sdl_event->key.keysym.scancode] = true;
        input_manager->mods                                        = InputManager__Mods(sdl_event->key.keysym.mod);
        if (!sdl_event->key.repeat && sdl_event->key.keysym.scancode < SDL_NUM_SCANCODES) {
            input_manager->key_down[sdl_event->key.keysym.scancode] = true;
            InputManager__Press(INPUT_KEY, sdl_event->key.keysym.scancode);
        }

        return true;
    case SDL_KEYUP:
        input_manager->key_pressed[sdl_event->key.keysym.scancode] = false;
        input_manager->mods                                        = InputManager__Mods(sdl_event->key.keysym.mod);
        if (sdl_event->key.keysym.scancode < SDL_NUM_SCANCODES) {
            input_manager->key_down[sdl_event->key.keysym.scancode] = false;
        }
        return true;
    case SDL_TEXTINPUT:
        if (strlen(input_manager->text_input) + strlen(sdl_event->text.text) < MAX_TEXT_INPUT_LENGTH) {
            strcat(input_manager->text_input, sdl_event->text.text);
        }
        return true;
    default:
        return false;
    }
}

void InputManager_StartTextInput(void) {
    memset(input_manager->text_input, 0, sizeof(input_manager->text_input));
    SDL_StartTextInput();
}

void InputManager_StopTextInput(void) {
    memset(input_manager->text_input, 0, sizeof(input_manager->text_input));
    SDL_StopTextInput();
}

bool InputManager_IsTextInputActive(void) { return SDL_IsTextInputActive(); }

void InputManager_GetMousePosition(int *mouse_x, int *mouse_y) {
    if (mouse_x != NULL) {
        *mouse_x = input_manager->mouse_x;
    }

    if (mouse_y != NULL) {
        *mouse_y = input_manager->mouse_y;
    }
}

void InputManager_GetMouseButtons(bool *left, bool *middle, bool *right) {
    if (left != NULL) {
        *left = (input_manager->mouse_buttons & SDL_BUTTON_LMASK) != 0;
    }

    if (middle != NULL) {
        *middle = (input_manager->mouse_buttons & SDL_BUTTON_MMASK) != 0;
    }

    if (right != NULL) {
        *right = (input_manager->mouse_buttons & SDL_BUTTON_RMASK) != 0;
    }
}

void InputManager_ResetMouseButtons(void) { input_manager->mouse_buttons = 0; }

int InputManager_ConsumeMouseWheel(void) {
    const int result             = input_manager->mouse_wheel_y;
    input_manager->mouse_wheel_y = 0;
    return result;
}

const char *InputManager_GetTextInput(void) { return input_manager->text_input; }

void InputManager_SetTextInput(const char *text) {
    memset(input_manager->text_input, 0, sizeof(input_manager->text_input));
    if (text != NULL) {
        strncat(input_manager->text_input, text, MAX_TEXT_INPUT_LENGTH - 1);
    }
}

bool InputManager_PollPress(InputPress *press) {
    if (input_manager->press_count == 0) {
        return false;
    }
    *press                    = input_manager->presses[input_manager->press_head];
    input_manager->press_head = (input_manager->press_head + 1) % MAX_PRESSES;
    input_manager->press_count--;
    return true;
}

void InputManager_ClearPresses(void) {
    input_manager->press_head  = 0;
    input_manager->press_count = 0;
}

uint16_t InputManager_GetMods(void) { return input_manager->mods; }

float InputManager_GetPadAxis(const SDL_GameControllerAxis axis) {
    float strongest = 0.0f;
    for (int p = 0; p < MAX_PADS; p++) {
        const float v = input_manager->pad_axes[p][axis];
        strongest     = fabsf(v) > fabsf(strongest) ? v : strongest;
    }
    return strongest;
}

bool InputManager_IsDown(const InputKind kind, const int value) {
    switch (kind) {
    case INPUT_KEY:
        return value >= 0 && value < SDL_NUM_SCANCODES && input_manager->key_down[value];
    case INPUT_MOUSE_BUTTON:
        return value > 0 && value <= 8 && (input_manager->mouse_buttons & SDL_BUTTON(value)) != 0;
    case INPUT_PAD_BUTTON:
        return value >= 0 && value < SDL_CONTROLLER_BUTTON_MAX && input_manager->pad_buttons[value];
    case INPUT_PAD_AXIS: {
        const float v = InputManager_GetPadAxis((SDL_GameControllerAxis)(value / 2));
        return (value % 2 == 1) ? v >= INPUT_PAD_AXIS_THRESHOLD : v <= -INPUT_PAD_AXIS_THRESHOLD;
    }
    case INPUT_MOUSE_MOVE:
        return value >= 0 && value < 4 && SDL_GetTicks64() - input_manager->move_time[value] <= MOVE_HOLD_MS;
    default:
        return false;
    }
}

float InputManager_GetAmount(const InputKind kind, const int value) {
    switch (kind) {
    case INPUT_PAD_AXIS: {
        // A stick or trigger, as far as it's pushed that way (a little dead zone in the middle)
        const float v      = InputManager_GetPadAxis((SDL_GameControllerAxis)(value / 2));
        const float amount = (value % 2 == 1) ? v : -v;
        return amount <= INPUT_PAD_DEAD_ZONE ? 0.0f : (amount - INPUT_PAD_DEAD_ZONE) / (1.0f - INPUT_PAD_DEAD_ZONE);
    }
    case INPUT_MOUSE_MOVE:
        if (!InputManager_IsDown(kind, value)) {
            return 0.0f;
        }
        return input_manager->move_speed[value] >= MOVE_FULL ? 1.0f : input_manager->move_speed[value] / MOVE_FULL;
    default:
        return InputManager_IsDown(kind, value) ? 1.0f : 0.0f;
    }
}

bool InputManager_ConsumeKeyPress(const SDL_Scancode scancode) {
    const bool pressed                   = input_manager->key_pressed[scancode];
    input_manager->key_pressed[scancode] = false;
    return pressed;
}
