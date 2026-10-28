#include "PadNav.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "InputManager.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD_NAV_MAX      768    // places a frame
#define PAD_REPEAT_FIRST 320    // ms a direction is held before it repeats
#define PAD_REPEAT_NEXT  110    // then this often
#define PAD_STICK_PRESS  0.55f  // a stick past this counts as a direction pressed
#define PAD_POINTER_RATE 700.0f // pixels a second at full push (times Options › Controls › Controller's speed)
#define PAD_STEP         24     // pixels the pointer moves when there's nothing that way to jump to
#define PAD_FREE_AIM_MS  1500   // after the right stick moved the pointer, play doesn't aim for a while

typedef struct PadNavPlace {
    SDL_Rect   rect;
    PadNavKind kind;
    int        step_w;
    int        step_h;
    bool       more_above; // a list that scrolls further that way
    bool       more_below;
} PadNavPlace;

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_COUNT } PadDir;

static struct {
    PadNavPlace places[2][PAD_NAV_MAX]; // this frame's (being filled) and last frame's (navigated)
    int         counts[2];
    int         filling;
    bool        gameplay;      // set during the last frame by the game world
    bool        gameplay_next; // being set during this frame
    bool        suspended;
    bool        suspend_next;
    bool        captured;        // the menus have the controller this frame
    bool        gameplay_before; // (play had the controller last frame)
    int         snap;            // frames until the pointer is put onto something (0: not)
    bool        buttons[SDL_CONTROLLER_BUTTON_MAX];
    uint64_t    dir_since[DIR_COUNT]; // when each direction went down (0: up)
    uint64_t    dir_next[DIR_COUNT];  // its next repeat
    float       pointer_x;            // the pointer, moved by the sticks (fractions kept)
    float       pointer_y;
    uint64_t    last_tick;
    uint64_t    free_aim_until;
    int         tab_step;
    int         slider_step;
    SDL_Rect    slider_rect;
    bool        mouse_left; // what we're holding for the controller
    bool        mouse_right;
    // The keyboard on the screen (a box to type in has the focus)
    int    key_row;
    int    key_col;
    bool   caps;
    bool   typed_done; // Done on the keyboard: hidden while that box keeps the focus (A on a box: back)
    Label *label;
} nav = {.filling = 0};

static const char *const keyboard_rows[4] = {"abcdefghij", "klmnopqrst", "uvwxyz-_.'", "0123456789"};
#define KEY_ROWS 5 // and a last row: Space, Delete, Done

void PadNav_Register(const SDL_Rect *rect) { PadNav_RegisterKind(rect, PAD_NAV_TARGET, 0, 0); }

void PadNav_RegisterKind(const SDL_Rect *rect, const PadNavKind kind, const int step_w, const int step_h) {
    if (rect == NULL || rect->w <= 0 || rect->h <= 0 || nav.counts[nav.filling] >= PAD_NAV_MAX) {
        return;
    }
    PadNavPlace *place = &nav.places[nav.filling][nav.counts[nav.filling]++];
    place->rect        = *rect;
    place->kind        = kind;
    place->step_w      = step_w > 0 ? step_w : rect->w;
    place->step_h      = step_h > 0 ? step_h : rect->h;
}

void PadNav_RegisterList(const SDL_Rect *rect, const int row_h, const bool more_above, const bool more_below) {
    const int before = nav.counts[nav.filling];
    PadNav_RegisterKind(rect, PAD_NAV_LIST, 0, row_h);
    if (nav.counts[nav.filling] > before) {
        nav.places[nav.filling][before].more_above = more_above;
        nav.places[nav.filling][before].more_below = more_below;
    }
}

void PadNav_SetGameplay(const bool gameplay) { nav.gameplay_next = nav.gameplay_next || gameplay; }
void PadNav_Suspend(void) { nav.suspend_next = true; }
bool PadNav_Active(void) { return InputManager_PadInUse(); }
bool PadNav_FreeAim(void) { return SDL_GetTicks64() < nav.free_aim_until; }
int  PadNav_TakeTabStep(void) {
    const int step = nav.tab_step;
    nav.tab_step   = 0;
    return step;
}

int PadNav_SliderStep(const SDL_Rect *rect) {
    return rect != NULL && nav.slider_step != 0 && rect->x == nav.slider_rect.x && rect->y == nav.slider_rect.y &&
                   rect->w == nav.slider_rect.w
               ? nav.slider_step
               : 0;
}

// A button's edge this frame (true once when it goes down)
static bool PadNav__Pressed(const SDL_GameControllerButton button) {
    return InputManager_RawPadButton(button) && !nav.buttons[button];
}

// The keyboard on the screen is up: a box to type in has the focus (and Done wasn't picked)
static bool PadNav__Typing(void) { return InputManager_IsTextInputActive() && !nav.typed_done; }

// The place the pointer is in (the smallest that has it), -1 none
static int PadNav__Under(const PadNavPlace *places, const int count, const int x, const int y) {
    int best = -1;
    for (int i = 0; i < count; i++) {
        const SDL_Rect *r = &places[i].rect;
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h &&
            (best < 0 || r->w * r->h < places[best].rect.w * places[best].rect.h)) {
            best = i;
        }
    }
    return best;
}

// The nearest place that way from (x, y): mostly that way, as little to the side as can be
static int PadNav__Next(const PadNavPlace *places, const int count, const int from, const int x, const int y,
                        const PadDir dir) {
    int   best  = -1;
    float score = 1e9f;
    for (int i = 0; i < count; i++) {
        if (i == from) {
            continue;
        }
        const SDL_Rect *r = &places[i].rect;
        // The nearest point of it, and how far that is along and across the way we're going
        const float cx     = (float)(x < r->x ? r->x : (x >= r->x + r->w ? r->x + r->w - 1 : x));
        const float cy     = (float)(y < r->y ? r->y : (y >= r->y + r->h ? r->y + r->h - 1 : y));
        const float mid_x  = (float)r->x + ((float)r->w / 2.0f);
        const float mid_y  = (float)r->y + ((float)r->h / 2.0f);
        float       along  = 0.0f; // (to its middle: it must be ahead)
        float       near   = 0.0f; // (to its nearest edge: how far it is)
        float       across = 0.0f;
        switch (dir) {
        case DIR_UP:
            along  = (float)y - mid_y;
            near   = (float)y - cy;
            across = fabsf(cx - (float)x);
            break;
        case DIR_DOWN:
            along  = mid_y - (float)y;
            near   = cy - (float)y;
            across = fabsf(cx - (float)x);
            break;
        case DIR_LEFT:
            along  = (float)x - mid_x;
            near   = (float)x - cx;
            across = fabsf(cy - (float)y);
            break;
        default:
            along  = mid_x - (float)x;
            near   = cx - (float)x;
            across = fabsf(cy - (float)y);
            break;
        }
        if (along < 4.0f || across > (fmaxf(near, 0.0f) * 3.0f) + 80.0f) {
            continue; // behind, or too far to the side
        }
        // Up and down: screens are laid out in rows, so the next row counts more than the column; left and right
        // keep to the row
        const bool  vertical = dir == DIR_UP || dir == DIR_DOWN;
        const float s        = fmaxf(near, 0.0f) + (along * 0.1f) + (across * (vertical ? 1.2f : 4.0f)) +
                        (across > 0.0f ? (vertical ? 10.0f : 60.0f) : 0.0f); // (in line counts most)
        if (s < score) {
            score = s;
            best  = i;
        }
    }
    return best;
}

// The directions pressed this frame (D-pad and left stick; a held one repeats)
static bool PadNav__Directions(bool pressed[DIR_COUNT], const bool use_stick) {
    const uint64_t now             = SDL_GetTicks64();
    const float    lx              = use_stick ? InputManager_RawPadAxis(SDL_CONTROLLER_AXIS_LEFTX) : 0.0f;
    const float    ly              = use_stick ? InputManager_RawPadAxis(SDL_CONTROLLER_AXIS_LEFTY) : 0.0f;
    const bool     down[DIR_COUNT] = {
        InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_DPAD_UP) || ly <= -PAD_STICK_PRESS,
        InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly >= PAD_STICK_PRESS,
        InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx <= -PAD_STICK_PRESS,
        InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >= PAD_STICK_PRESS,
    };
    bool any = false;
    for (int d = 0; d < DIR_COUNT; d++) {
        pressed[d] = false;
        if (!down[d]) {
            nav.dir_since[d] = 0;
            continue;
        }
        if (nav.dir_since[d] == 0) {
            nav.dir_since[d] = now;
            nav.dir_next[d]  = now + PAD_REPEAT_FIRST;
            pressed[d]       = true;
        } else if (now >= nav.dir_next[d]) {
            nav.dir_next[d] = now + PAD_REPEAT_NEXT;
            pressed[d]      = true;
        }
        any = any || pressed[d];
    }
    return any;
}

// The pointer moved by a stick (a curve: small pushes for fine moves)
static bool PadNav__Stick(const SDL_GameControllerAxis ax, const SDL_GameControllerAxis ay, const float seconds) {
    const float x = InputManager_RawPadAxis(ax);
    const float y = InputManager_RawPadAxis(ay);
    if (fabsf(x) < INPUT_PAD_DEAD_ZONE && fabsf(y) < INPUT_PAD_DEAD_ZONE) {
        return false;
    }
    const float speed  = PAD_POINTER_RATE * (float)AbyssConfiguration_GetPadPointerSpeed() / 100.0f * seconds;
    nav.pointer_x     += x * fabsf(x) * speed;
    nav.pointer_y     += y * fabsf(y) * speed;
    nav.pointer_x      = nav.pointer_x < 0.0f ? 0.0f : (nav.pointer_x > 799.0f ? 799.0f : nav.pointer_x);
    nav.pointer_y      = nav.pointer_y < 0.0f ? 0.0f : (nav.pointer_y > 599.0f ? 599.0f : nav.pointer_y);
    InputManager_SetPointer((int)nav.pointer_x, (int)nav.pointer_y);
    return true;
}

static void PadNav__Point(const int x, const int y) {
    nav.pointer_x = (float)x;
    nav.pointer_y = (float)y;
    InputManager_SetPointer(x, y);
}

// The keyboard on the screen: the D-pad moves on it, A types, X deletes, Y capitals, B or Start: done (Enter)
static void PadNav__Keyboard(const bool pressed[DIR_COUNT]) {
    if (pressed[DIR_UP]) {
        nav.key_row = (nav.key_row + KEY_ROWS - 1) % KEY_ROWS;
    }
    if (pressed[DIR_DOWN]) {
        nav.key_row = (nav.key_row + 1) % KEY_ROWS;
    }
    const int cols = nav.key_row < 4 ? 10 : 3;
    nav.key_col    = nav.key_col >= cols ? cols - 1 : nav.key_col;
    if (pressed[DIR_LEFT]) {
        nav.key_col = (nav.key_col + cols - 1) % cols;
    }
    if (pressed[DIR_RIGHT]) {
        nav.key_col = (nav.key_col + 1) % cols;
    }
    char text[512];
    snprintf(text, sizeof(text), "%s", InputManager_GetTextInput());
    const size_t len = strlen(text);
    if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_A)) {
        if (nav.key_row < 4) {
            char c = keyboard_rows[nav.key_row][nav.key_col];
            c      = nav.caps && c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
            if (len + 1 < sizeof(text)) {
                text[len]     = c;
                text[len + 1] = '\0';
                InputManager_SetTextInput(text);
            }
        } else if (nav.key_col == 0 && len + 1 < sizeof(text)) {
            text[len]     = ' ';
            text[len + 1] = '\0';
            InputManager_SetTextInput(text);
        } else if (nav.key_col == 1 && len > 0) {
            text[len - 1] = '\0';
            InputManager_SetTextInput(text);
        } else if (nav.key_col == 2) {
            InputManager_PressKey(SDL_SCANCODE_RETURN);
            nav.typed_done = true;
        }
    }
    if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_X) && len > 0) {
        text[len - 1] = '\0';
        InputManager_SetTextInput(text);
    }
    if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_Y)) {
        nav.caps = !nav.caps;
    }
    if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_B) || PadNav__Pressed(SDL_CONTROLLER_BUTTON_START)) {
        InputManager_PressKey(SDL_SCANCODE_RETURN);
        nav.typed_done = true;
    }
}

// Let go of what we were holding for the controller
static void PadNav__Release(void) {
    if (nav.mouse_left) {
        InputManager_SetMouseButton(SDL_BUTTON_LEFT, false);
        nav.mouse_left = false;
    }
    if (nav.mouse_right) {
        InputManager_SetMouseButton(SDL_BUTTON_RIGHT, false);
        nav.mouse_right = false;
    }
}

void PadNav_Update(void) {
    const uint64_t now     = SDL_GetTicks64();
    const float    seconds = nav.last_tick > 0 ? (float)(now - nav.last_tick) / 1000.0f : 0.0f;
    nav.last_tick          = now;
    // Last frame's places are the ones to move between; this frame's start empty
    nav.filling               = 1 - nav.filling;
    nav.counts[nav.filling]   = 0;
    const PadNavPlace *places = nav.places[1 - nav.filling];
    const int          count  = nav.counts[1 - nav.filling];
    nav.gameplay              = nav.gameplay_next;
    nav.gameplay_next         = false;
    nav.suspended             = nav.suspend_next;
    nav.suspend_next          = false;
    nav.tab_step              = 0;
    nav.slider_step           = 0;

    if (!InputManager_PadConnected() || nav.suspended) {
        PadNav__Release();
        nav.captured = false;
        InputManager_SetPadCapture(false);
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) { // (what's held now isn't pressed when it comes back)
            nav.buttons[b] = InputManager_RawPadButton((SDL_GameControllerButton)b);
        }
        return;
    }
    int mouse_x;
    int mouse_y;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    if (!InputManager_PadInUse()) {
        nav.pointer_x = (float)mouse_x; // (the mouse moved it: carry on from there)
        nav.pointer_y = (float)mouse_y;
    }

    // In play: the bindings have the controller; the right stick aims, Start is the menu
    if (!InputManager_IsTextInputActive()) {
        nav.typed_done = false; // (the next box typed in gets the keyboard again)
    }
    if (nav.gameplay && !PadNav__Typing()) {
        PadNav__Release();
        nav.captured        = false;
        nav.gameplay_before = true;
        InputManager_SetPadCapture(false);
        if (PadNav__Stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, seconds)) {
            nav.free_aim_until = now + PAD_FREE_AIM_MS;
        }
        if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_START)) {
            InputManager_PressKey(SDL_SCANCODE_ESCAPE);
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            nav.buttons[b] = InputManager_RawPadButton((SDL_GameControllerButton)b);
        }
        return;
    }

    // A menu or a panel has it
    if (!nav.captured || nav.gameplay_before) {
        nav.snap = 2; // (just came from play, or into the menus: the pointer goes onto something soon)
    }
    nav.gameplay_before = false;
    nav.captured        = true;
    InputManager_SetPadCapture(true);
    const bool console = !AbyssConfiguration_GetPadCursor();
    // Console style: the pointer onto the panel's grid (the biggest: the inventory, the stash) or what's nearest, once
    // the panel has said where it is, unless the pointer's on something already
    if (nav.snap > 0 && --nav.snap == 0 && console && InputManager_PadInUse() && count > 0 &&
        PadNav__Under(places, count, (int)nav.pointer_x, (int)nav.pointer_y) < 0) {
        int   pick = -1;
        float best = 1e9f;
        for (int i = 0; i < count; i++) {
            const SDL_Rect *r  = &places[i].rect;
            const float     dx = (float)(r->x + (r->w / 2)) - nav.pointer_x;
            const float     dy = (float)(r->y + (r->h / 2)) - nav.pointer_y;
            const float score  = places[i].kind == PAD_NAV_GRID ? -(float)(r->w * r->h) : sqrtf((dx * dx) + (dy * dy));
            if (pick < 0 || (places[i].kind == PAD_NAV_GRID) > (places[pick].kind == PAD_NAV_GRID) ||
                ((places[i].kind == PAD_NAV_GRID) == (places[pick].kind == PAD_NAV_GRID) && score < best)) {
                pick = i;
                best = score;
            }
        }
        const SDL_Rect *r = &places[pick].rect;
        PadNav__Point(places[pick].kind == PAD_NAV_GRID ? r->x + (places[pick].step_w / 2) : r->x + (r->w / 2),
                      places[pick].kind == PAD_NAV_GRID ? r->y + (places[pick].step_h / 2) : r->y + (r->h / 2));
    }
    bool pressed[DIR_COUNT];
    PadNav__Directions(pressed, console || PadNav__Typing());
    if (PadNav__Typing()) {
        PadNav__Release();
        PadNav__Keyboard(pressed);
    } else {
        if (PadNav__Stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, seconds)) {
            nav.free_aim_until = now + PAD_FREE_AIM_MS;
        }
        if (!console) {
            PadNav__Stick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, seconds);
        }
        for (int d = 0; d < DIR_COUNT; d++) {
            if (!pressed[d]) {
                continue;
            }
            const int          x             = (int)nav.pointer_x;
            const int          y             = (int)nav.pointer_y;
            const int          under         = PadNav__Under(places, count, x, y);
            const PadNavPlace *in            = under >= 0 ? &places[under] : NULL;
            static const int   dx[DIR_COUNT] = {0, 0, -1, 1};
            static const int   dy[DIR_COUNT] = {-1, 1, 0, 0};
            // Inside a grid, a list or on a slider: a cell, a row, a step of its value
            if (in != NULL && in->kind == PAD_NAV_SLIDER && (d == DIR_LEFT || d == DIR_RIGHT)) {
                nav.slider_rect = in->rect; // (the slider steps its value: Slider_Update asks)
                nav.slider_step = dx[d];
                continue;
            }
            if (in != NULL && (in->kind == PAD_NAV_GRID || in->kind == PAD_NAV_LIST)) {
                const int nx = x + (dx[d] * in->step_w);
                const int ny = y + (dy[d] * in->step_h);
                if (nx >= in->rect.x && nx < in->rect.x + in->rect.w && ny >= in->rect.y &&
                    ny < in->rect.y + in->rect.h) {
                    PadNav__Point(nx, ny);
                    continue;
                }
                if (in->kind == PAD_NAV_LIST && ((dy[d] < 0 && in->more_above) || (dy[d] > 0 && in->more_below))) {
                    InputManager_AddWheel(-dy[d]); // (at its top or bottom: it scrolls, while there's more)
                    continue;
                }
            }
            const int next = PadNav__Next(places, count, under, x, y, (PadDir)d);
            if (next >= 0) {
                const SDL_Rect *r = &places[next].rect;
                if (places[next].kind == PAD_NAV_GRID || places[next].kind == PAD_NAV_LIST) {
                    // Into a grid or list: the cell or row nearest where we come from
                    int cx = x < r->x ? r->x : (x >= r->x + r->w ? r->x + r->w - 1 : x);
                    int cy = y < r->y ? r->y : (y >= r->y + r->h ? r->y + r->h - 1 : y);
                    cx = r->x + ((cx - r->x) / places[next].step_w * places[next].step_w) + (places[next].step_w / 2);
                    cy = r->y + ((cy - r->y) / places[next].step_h * places[next].step_h) + (places[next].step_h / 2);
                    PadNav__Point(cx, cy);
                } else {
                    PadNav__Point(r->x + (r->w / 2), r->y + (r->h / 2));
                }
            } else if (count == 0) {
                PadNav__Point(x + (dx[d] * PAD_STEP), y + (dy[d] * PAD_STEP)); // nothing to go to: a step
            }
        }
        // A clicks (held: drags), X is the right button (on a slider, console style, A does nothing: it would jump)
        const int on = PadNav__Under(places, count, (int)nav.pointer_x, (int)nav.pointer_y);
        if (on >= 0 && places[on].kind == PAD_NAV_TEXT && PadNav__Pressed(SDL_CONTROLLER_BUTTON_A)) {
            nav.typed_done = false; // (A on a box to type in: the keyboard)
        }
        const bool a = InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_A) &&
                       !(console && on >= 0 && places[on].kind == PAD_NAV_SLIDER);
        if (a != nav.mouse_left) {
            InputManager_SetMouseButton(SDL_BUTTON_LEFT, a);
            nav.mouse_left = a;
        }
        const bool x = InputManager_RawPadButton(SDL_CONTROLLER_BUTTON_X);
        if (x != nav.mouse_right) {
            InputManager_SetMouseButton(SDL_BUTTON_RIGHT, x);
            nav.mouse_right = x;
        }
        if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_B) || PadNav__Pressed(SDL_CONTROLLER_BUTTON_START)) {
            InputManager_PressKey(SDL_SCANCODE_ESCAPE);
        }
        if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
            nav.tab_step = -1;
        }
        if (PadNav__Pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
            nav.tab_step = 1;
        }
    }
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        nav.buttons[b] = InputManager_RawPadButton((SDL_GameControllerButton)b);
    }
}

void PadNav_Draw(void) {
    if (!nav.captured || !InputManager_PadInUse()) {
        return;
    }
    if (nav.label == NULL) {
        nav.label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    if (PadNav__Typing()) {
        // The keyboard, low in the middle
        const SDL_Rect panel = {170, 196, 460, 210};
        SDL_SetRenderDrawColor(sdl_renderer, 10, 8, 6, 235);
        SDL_RenderFillRect(sdl_renderer, &panel);
        SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
        SDL_RenderDrawRect(sdl_renderer, &panel);
        // What's typed so far (with a caret), and the capitals switch
        char typed[160];
        snprintf(typed, sizeof(typed), "%.60s_", InputManager_GetTextInput());
        Label_SetText(nav.label, typed);
        Label_SetColor(nav.label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(nav.label, panel.x + 12, panel.y + 10);
        Label_SetText(nav.label, nav.caps ? "Capitals on (Y)" : "Capitals off (Y)");
        Label_SetColor(nav.label, 0xC7, 0xB3, 0x77, 0xFF);
        int caps_w = 0;
        Label_GetSize(nav.label, &caps_w, NULL);
        Label_Draw(nav.label, panel.x + panel.w - 12 - caps_w, panel.y + 10);
        for (int r = 0; r < KEY_ROWS; r++) {
            const int cols = r < 4 ? 10 : 3;
            for (int c = 0; c < cols; c++) {
                const int      w   = r < 4 ? 42 : 144;
                const SDL_Rect key = {panel.x + 10 + (c * (w + 2)), panel.y + 40 + (r * 32), w, 28};
                const bool     on  = r == nav.key_row && c == nav.key_col;
                SDL_SetRenderDrawColor(sdl_renderer, on ? 120 : 30, on ? 95 : 26, on ? 40 : 18, 255);
                SDL_RenderFillRect(sdl_renderer, &key);
                char text[16];
                if (r < 4) {
                    const char ch = keyboard_rows[r][c];
                    snprintf(text, sizeof(text), "%c", nav.caps && ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
                } else {
                    static const char *const names[3] = {"Space", "Delete (X)", "Done (B)"};
                    snprintf(text, sizeof(text), "%s", names[c]);
                }
                Label_SetText(nav.label, text);
                Label_SetColor(nav.label, 0xFF, 0xFF, 0xFF, 0xFF);
                int tw = 0;
                Label_GetSize(nav.label, &tw, NULL);
                Label_Draw(nav.label, key.x + ((key.w - tw) / 2), key.y + 6);
            }
        }
        return;
    }
    if (AbyssConfiguration_GetPadCursor()) {
        return; // (cursor style: the pointer is enough)
    }
    // Console style: the gold frame round what's picked (the cell, in a grid)
    int mouse_x;
    int mouse_y;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    const PadNavPlace *places = nav.places[1 - nav.filling];
    const int          under  = PadNav__Under(places, nav.counts[1 - nav.filling], mouse_x, mouse_y);
    if (under < 0) {
        return;
    }
    SDL_Rect r = places[under].rect;
    if (places[under].kind == PAD_NAV_GRID || places[under].kind == PAD_NAV_LIST) {
        const int sw = places[under].step_w;
        const int sh = places[under].step_h;
        r.x          = places[under].rect.x + ((mouse_x - r.x) / sw * sw);
        r.y          = places[under].rect.y + ((mouse_y - places[under].rect.y) / sh * sh);
        r.w          = places[under].kind == PAD_NAV_LIST ? places[under].rect.w : sw;
        r.h          = sh;
        if (places[under].kind == PAD_NAV_LIST) {
            r.x = places[under].rect.x;
        }
    }
    SDL_SetRenderDrawColor(sdl_renderer, 0xE8, 0xC8, 0x60, 255);
    for (int t = 0; t < 2; t++) {
        const SDL_Rect frame = {r.x - 2 - t, r.y - 2 - t, r.w + 4 + (t * 2), r.h + 4 + (t * 2)};
        SDL_RenderDrawRect(sdl_renderer, &frame);
    }
}
