#include "Controls.h"
#include "../common/Logging.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define DOUBLE_TAP_MS 300 // a second press this soon after the first is a double tap

typedef struct ActionInfo {
    const char  *id;
    const char  *name;
    ControlGroup group;
    unsigned     contexts;
    bool         works;
    const char  *defaults[3]; // the game's default keys (ours for the actions the game doesn't have)
    const char  *help;
} ActionInfo;

#define G CONTROL_CONTEXT_GAME
#define D CONTROL_CONTEXT_DEBUG
#define A CONTROL_CONTEXT_ALL

static const ActionInfo actions[ACTION_COUNT] = {
    [ACTION_MOVE_ATTACK] = {"move_attack",
                            "Move / attack / use",                  GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Mouse Left"},
                            "Classic: walk where you click, attack what you click, open what you click"                                              },
    [ACTION_MOVE_UP]     = {"move_up",
                            "Move up",                              GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Up", "Pad Left Stick Up"},
                            "Modern and Combined movement: walk towards the top of the screen"                                                       },
    [ACTION_MOVE_DOWN]   = {"move_down",
                            "Move down",                            GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Down", "Pad Left Stick Down"},
                            "Modern and Combined movement"                                                                                           },
    [ACTION_MOVE_LEFT]   = {"move_left",
                            "Move left",                            GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Left", "Pad Left Stick Left"},
                            "Modern and Combined movement"                                                                                           },
    [ACTION_MOVE_RIGHT]  = {"move_right",
                            "Move right",                           GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Right", "Pad Left Stick Right"},
                            "Modern and Combined movement"                                                                                           },
    [ACTION_RUN_TOGGLE] =
        {"run_toggle",     "Run / walk",         GROUP_MOVEMENT, A, true,  {"R"},          "Switch between running and walking"   },
    [ACTION_RUN_HOLD]    = {"run_hold",
                            "Run (hold)",                           GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Left Ctrl"},
                            "Run (or walk, when running is on) while held"                                                                           },
    [ACTION_STAND_STILL] = {"stand_still",
                            "Stand still",                          GROUP_MOVEMENT,
                            A,                                                         true,
                            {"Left Shift", "Right Shift"},
                            "Attack without walking; clicks don't move you"                                                                          },
    [ACTION_ATTACK]      = {"attack",
                            "Attack",                               GROUP_COMBAT,
                            A,                                                         true,
                            {"E", "Pad Button 1 (Left)"},
                            "Modern and Combined movement: attack what's under the mouse, or the nearest monster"                                    },
    [ACTION_RIGHT_SKILL] = {"right_skill",
                            "Right skill / drop monster",           GROUP_COMBAT,
                            A,                                                         true,
                            {"Mouse Right"},
                            "Free Roam: drop the selected monster (the game: use the right skill)"                                                   },
    [ACTION_SWAP_WEAPONS] =
        {"swap_weapons",   "Swap weapons",       GROUP_COMBAT,   G, false, {"W"},          "Expansion: swap weapon sets"          },
    [ACTION_SKILL_1]        = {"skill_1",        "Skill 1",            GROUP_COMBAT,   G, false, {"F1"},         NULL                                   },
    [ACTION_SKILL_2]        = {"skill_2",        "Skill 2",            GROUP_COMBAT,   G, false, {"F2"},         NULL                                   },
    [ACTION_SKILL_3]        = {"skill_3",        "Skill 3",            GROUP_COMBAT,   G, false, {"F3"},         NULL                                   },
    [ACTION_SKILL_4]        = {"skill_4",        "Skill 4",            GROUP_COMBAT,   G, false, {"F4"},         NULL                                   },
    [ACTION_SKILL_5]        = {"skill_5",        "Skill 5",            GROUP_COMBAT,   G, false, {"F5"},         NULL                                   },
    [ACTION_SKILL_6]        = {"skill_6",        "Skill 6",            GROUP_COMBAT,   G, false, {"F6"},         NULL                                   },
    [ACTION_SKILL_7]        = {"skill_7",        "Skill 7",            GROUP_COMBAT,   G, false, {"F7"},         NULL                                   },
    [ACTION_SKILL_8]        = {"skill_8",        "Skill 8",            GROUP_COMBAT,   G, false, {"F8"},         NULL                                   },
    [ACTION_SKILL_PREVIOUS] = {"skill_previous",
                            "Previous skill",                       GROUP_COMBAT,
                            G,                                                         false,
                            {NULL},
                            "The game: mouse wheel up (here the wheel zooms)"                                                                        },
    [ACTION_SKILL_NEXT]     = {"skill_next",
                            "Next skill",                           GROUP_COMBAT,
                            G,                                                         false,
                            {NULL},
                            "The game: mouse wheel down (here the wheel zooms)"                                                                      },
    [ACTION_SKILL_TREE]     = {"skill_tree",     "Skill tree",         GROUP_COMBAT,   G, false, {"T"},          NULL                                   },
    [ACTION_SKILL_BAR]      = {"skill_bar",      "Skill speed bar",    GROUP_COMBAT,   G, false, {"S"},          NULL                                   },
    [ACTION_BELT_1]         = {"belt_1",         "Use belt 1",         GROUP_BELT,     G, false, {"1"},          NULL                                   },
    [ACTION_BELT_2]         = {"belt_2",         "Use belt 2",         GROUP_BELT,     G, false, {"2"},          NULL                                   },
    [ACTION_BELT_3]         = {"belt_3",         "Use belt 3",         GROUP_BELT,     G, false, {"3"},          NULL                                   },
    [ACTION_BELT_4]         = {"belt_4",         "Use belt 4",         GROUP_BELT,     G, false, {"4"},          NULL                                   },
    [ACTION_SHOW_BELT]      = {"show_belt",      "Show belt",          GROUP_BELT,     G, false, {"`"},          NULL                                   },
    [ACTION_CHARACTER]      = {"character",      "Character screen",   GROUP_SCREENS,  G, true,  {"A", "C"},     NULL                                   },
    [ACTION_INVENTORY]      = {"inventory",      "Inventory",          GROUP_SCREENS,  G, false, {"B", "I"},     NULL                                   },
    [ACTION_PARTY]          = {"party",          "Party screen",       GROUP_SCREENS,  G, false, {"P"},          NULL                                   },
    [ACTION_HIRELING]       = {"hireling",       "Hireling screen",    GROUP_SCREENS,  G, false, {"O"},          NULL                                   },
    [ACTION_QUESTS]         = {"quests",         "Quest log",          GROUP_SCREENS,  G, false, {"Q"},          NULL                                   },
    [ACTION_MESSAGE_LOG]    = {"message_log",    "Message log",        GROUP_SCREENS,  G, false, {"M"},          NULL                                   },
    [ACTION_HELP]           = {"help",           "Help",               GROUP_SCREENS,  G, false, {"H"},          NULL                                   },
    [ACTION_CLEAR_SCREEN] =
        {"clear_screen",   "Clear screen",       GROUP_SCREENS,  A, true,  {"Space"},      "Close the open screens and panels"    },
    [ACTION_SHOW_ITEMS]     = {"show_items",
                            "Show items",                           GROUP_SCREENS,
                            G,                                                         false,
                            {"Left Alt"},
                            "Show the names of items on the ground while held"                                                                       },
    [ACTION_SHOW_PORTRAITS] = {"show_portraits", "Show portraits",     GROUP_SCREENS,  G, false, {"Z"},          NULL                                   },
    [ACTION_WAYPOINT]       = {"waypoint",
                            "Waypoint / places",                    GROUP_SCREENS,
                            A,                                                         true,
                            {"V"},
                            "Travel by waypoint (in town or next to one); Free Roam: the Places list"                                                },
    [ACTION_TOWN] = {"town",           "Back to town",       GROUP_SCREENS,  G, true,  {"Home"},       "Single Player: back to the act's town"},
    [ACTION_AUTOMAP]        = {"automap",        "Automap",            GROUP_AUTOMAP,  A, true,  {"Tab"},        NULL                                   },
    [ACTION_AUTOMAP_CENTER] = {"automap_center", "Center automap",     GROUP_AUTOMAP,  G, false, {"F9"},         NULL                                   },
    [ACTION_AUTOMAP_FADE]   = {"automap_fade",   "Fade automap",       GROUP_AUTOMAP,  G, false, {"F10"},        NULL                                   },
    [ACTION_AUTOMAP_PARTY]  = {"automap_party",  "Party on automap",   GROUP_AUTOMAP,  G, false, {"F11"},        NULL                                   },
    [ACTION_AUTOMAP_NAMES]  = {"automap_names",  "Names on automap",   GROUP_AUTOMAP,  G, false, {"F12"},        NULL                                   },
    [ACTION_ZOOM_IN]        = {"zoom_in",        "Zoom in",            GROUP_VIEW,     A, true,  {"Wheel Up"},   NULL                                   },
    [ACTION_ZOOM_OUT]       = {"zoom_out",       "Zoom out",           GROUP_VIEW,     A, true,  {"Wheel Down"}, NULL                                   },
    [ACTION_LIGHT]          = {"light",          "Day / dusk / night", GROUP_VIEW,     A, true,  {"L"},          "Cycle the light (debug)"              },
    [ACTION_SCREENSHOT]     = {"screenshot",
                            "Screenshot",                           GROUP_VIEW,
                            A,                                                         true,
                            {"PrintScreen"},
                            "Saves a picture in the screenshots folder next to abyss.ini"                                                            },
    [ACTION_CHAT]           = {"chat",           "Chat",               GROUP_CHAT,     G, false, {"Return"},     NULL                                   },
    [ACTION_CLEAR_MESSAGES] = {"clear_messages", "Clear messages",     GROUP_CHAT,     G, false, {"N"},          NULL                                   },
    [ACTION_COMBAT]         = {"combat",         "Combat on / off",    GROUP_DEBUG,    D, true,  {"C"},          NULL                                   },
    [ACTION_COLLISION]      = {"collision",      "Collision view",     GROUP_DEBUG,    D, true,  {"B"},          NULL                                   },
    [ACTION_PREVIEW]        = {"preview",        "Placement preview",  GROUP_DEBUG,    D, true,  {"P"},          NULL                                   },
    [ACTION_NEW_WORLD] = {"new_world",      "New world",          GROUP_DEBUG,    D, true,  {"N"},          "Free Roam: a new random world seed"   },
};

#undef G
#undef D
#undef A

static const char *group_names[GROUP_COUNT] = {
    "Movement", "Combat and skills", "Belt", "Screens", "Automap", "View", "Chat and messages", "Free Roam (debug)",
};

static ControlBindings bindings[CONTROL_PROFILES][ACTION_COUNT];
static bool            own_profile[CONTROL_PROFILES]; // [0] is always true
static bool            loaded          = false;
static int             current_profile = 0;
static unsigned        current_context = CONTROL_CONTEXT_ALL;
static bool            pressed[ACTION_COUNT];
static uint64_t        last_press_time[INPUT_PAD_AXIS + 1][SDL_NUM_SCANCODES];

// ---------------------------------------------------------------------------------------------------------------------
// Actions

const char  *Controls_ActionName(const ControlAction action) { return actions[action].name; }
const char  *Controls_ActionId(const ControlAction action) { return actions[action].id; }
const char  *Controls_ActionHelp(const ControlAction action) { return actions[action].help; }
ControlGroup Controls_ActionGroup(const ControlAction action) { return actions[action].group; }
unsigned     Controls_ActionContexts(const ControlAction action) { return actions[action].contexts; }
bool         Controls_ActionWorks(const ControlAction action) { return actions[action].works; }
const char  *Controls_GroupName(const ControlGroup group) { return group_names[group]; }

// ---------------------------------------------------------------------------------------------------------------------
// Inputs as text

static const char *mouse_move_names[4] = {"Mouse Move Left", "Mouse Move Right", "Mouse Move Up", "Mouse Move Down"};
static const char *mouse_names[]       = {"", "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse 4", "Mouse 5"};

// Gamepad inputs carry Windows' button numbers (what the system's game controller settings show, the same for any
// pad) with where the button is: the four on the right are a second D-pad, "Pad Button 4 (Up)" is the top one.
// SDL numbers them by position too, so this holds for every pad.
static const char *Controls__AxisName(const int value) {
    static const char *names[] = {
        "Pad Left Stick Left",         "Pad Left Stick Right",           "Pad Left Stick Up",
        "Pad Left Stick Down",         "Pad Right Stick Left",           "Pad Right Stick Right",
        "Pad Right Stick Up",          "Pad Right Stick Down",           "Pad Button 7 (Left Trigger) -",
        "Pad Button 7 (Left Trigger)", "Pad Button 8 (Right Trigger) -", "Pad Button 8 (Right Trigger)"};
    return value >= 0 && value < (int)(sizeof(names) / sizeof(names[0])) ? names[value] : "Pad Axis";
}

static const char *Controls__PadButtonName(const int value) {
    static const char *names[] = {"Pad Button 2 (Down)",
                                  "Pad Button 3 (Right)",
                                  "Pad Button 1 (Left)",
                                  "Pad Button 4 (Up)",
                                  "Pad Button 9 (Select)",
                                  "Pad Button 13 (Home)",
                                  "Pad Button 10 (Start)",
                                  "Pad Button 11 (Left Stick Press)",
                                  "Pad Button 12 (Right Stick Press)",
                                  "Pad Button 5 (Left Bumper)",
                                  "Pad Button 6 (Right Bumper)",
                                  "Pad D-pad Up",
                                  "Pad D-pad Down",
                                  "Pad D-pad Left",
                                  "Pad D-pad Right",
                                  "Pad Misc",
                                  "Pad Paddle 1",
                                  "Pad Paddle 2",
                                  "Pad Paddle 3",
                                  "Pad Paddle 4",
                                  "Pad Button 14 (Touchpad)"};
    return value >= 0 && value < (int)(sizeof(names) / sizeof(names[0])) ? names[value] : "Pad Button";
}

// Names options.ini files written before used, still read: an input kind, its value
static bool Controls__OldPadName(const char *name, ControlInput *input) {
    static const struct {
        const char *name;
        int         kind;
        int         value;
    } old[] = {
        {"Pad A",              INPUT_PAD_BUTTON, 0 },
        {"Pad B",              INPUT_PAD_BUTTON, 1 },
        {"Pad X",              INPUT_PAD_BUTTON, 2 },
        {"Pad Y",              INPUT_PAD_BUTTON, 3 },
        {"Pad Button Down",    INPUT_PAD_BUTTON, 0 },
        {"Pad Button Right",   INPUT_PAD_BUTTON, 1 },
        {"Pad Button Left",    INPUT_PAD_BUTTON, 2 },
        {"Pad Button Up",      INPUT_PAD_BUTTON, 3 },
        {"Pad Back",           INPUT_PAD_BUTTON, 4 },
        {"Pad Select",         INPUT_PAD_BUTTON, 4 },
        {"Pad Guide",          INPUT_PAD_BUTTON, 5 },
        {"Pad Home",           INPUT_PAD_BUTTON, 5 },
        {"Pad Start",          INPUT_PAD_BUTTON, 6 },
        {"Pad Left Stick",     INPUT_PAD_BUTTON, 7 },
        {"Pad Right Stick",    INPUT_PAD_BUTTON, 8 },
        {"Pad Left Shoulder",  INPUT_PAD_BUTTON, 9 },
        {"Pad Left Bumper",    INPUT_PAD_BUTTON, 9 },
        {"Pad Right Shoulder", INPUT_PAD_BUTTON, 10},
        {"Pad Right Bumper",   INPUT_PAD_BUTTON, 10},
        {"Pad Up",             INPUT_PAD_BUTTON, 11},
        {"Pad Down",           INPUT_PAD_BUTTON, 12},
        {"Pad Left",           INPUT_PAD_BUTTON, 13},
        {"Pad Right",          INPUT_PAD_BUTTON, 14},
        {"Pad Touchpad",       INPUT_PAD_BUTTON, 20},
        {"Pad Left Trigger",   INPUT_PAD_AXIS,   9 },
        {"Pad Right Trigger",  INPUT_PAD_AXIS,   11},
    };
    for (size_t o = 0; o < sizeof(old) / sizeof(old[0]); o++) {
        if (strcmp(name, old[o].name) == 0) {
            input->kind  = (InputKind)old[o].kind;
            input->value = old[o].value;
            return true;
        }
    }
    return false;
}

void Controls_InputName(const ControlInput *input, char *text, const int size) {
    char base[48] = "";
    switch (input->kind) {
    case INPUT_KEY: {
        const char *name = SDL_GetScancodeName((SDL_Scancode)input->value);
        snprintf(base, sizeof(base), "%s", name[0] != '\0' ? name : "?");
        break;
    }
    case INPUT_MOUSE_BUTTON:
        snprintf(base, sizeof(base), "%s",
                 input->value >= 1 && input->value <= 5 ? mouse_names[input->value] : "Mouse");
        break;
    case INPUT_MOUSE_WHEEL:
        snprintf(base, sizeof(base), "%s", input->value > 0 ? "Wheel Up" : "Wheel Down");
        break;
    case INPUT_PAD_BUTTON:
        snprintf(base, sizeof(base), "%s", Controls__PadButtonName(input->value));
        break;
    case INPUT_PAD_AXIS:
        snprintf(base, sizeof(base), "%s", Controls__AxisName(input->value));
        break;
    case INPUT_MOUSE_MOVE:
        snprintf(base, sizeof(base), "%s", input->value >= 0 && input->value < 4 ? mouse_move_names[input->value] : "");
        break;
    default:
        break;
    }
    snprintf(text, (size_t)size, "%s%s%s%s%s", input->twice ? "2x " : "", (input->mods & KMOD_CTRL) ? "Ctrl+" : "",
             (input->mods & KMOD_SHIFT) ? "Shift+" : "", (input->mods & KMOD_ALT) ? "Alt+" : "", base);
}

static bool Controls__StartsWith(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool Controls_ParseInput(const char *text, ControlInput *input) {
    memset(input, 0, sizeof(*input));
    while (isspace((unsigned char)*text)) {
        text++;
    }
    if (Controls__StartsWith(text, "2x ")) {
        input->twice  = true;
        text         += 3;
    }
    for (bool more = true; more;) {
        more = false;
        if (Controls__StartsWith(text, "Ctrl+")) {
            input->mods |= KMOD_CTRL, text += 5, more = true;
        } else if (Controls__StartsWith(text, "Shift+")) {
            input->mods |= KMOD_SHIFT, text += 6, more = true;
        } else if (Controls__StartsWith(text, "Alt+")) {
            input->mods |= KMOD_ALT, text += 4, more = true;
        }
    }
    char name[64];
    snprintf(name, sizeof(name), "%s", text);
    for (int n = (int)strlen(name) - 1; n >= 0 && isspace((unsigned char)name[n]); n--) {
        name[n] = '\0';
    }
    if (name[0] == '\0') {
        return false;
    }
    for (int b = 1; b <= 5; b++) {
        if (strcmp(name, mouse_names[b]) == 0) {
            input->kind  = INPUT_MOUSE_BUTTON;
            input->value = b;
            return true;
        }
    }
    for (int d = 0; d < 4; d++) {
        if (strcmp(name, mouse_move_names[d]) == 0) {
            input->kind  = INPUT_MOUSE_MOVE;
            input->value = d;
            return true;
        }
    }
    if (strcmp(name, "Wheel Up") == 0 || strcmp(name, "Wheel Down") == 0) {
        input->kind  = INPUT_MOUSE_WHEEL;
        input->value = strcmp(name, "Wheel Up") == 0 ? 1 : -1;
        return true;
    }
    for (int a = 0; a < 12; a++) {
        if (strcmp(name, Controls__AxisName(a)) == 0) {
            input->kind  = INPUT_PAD_AXIS;
            input->value = a;
            return true;
        }
    }
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        if (strcmp(name, Controls__PadButtonName(b)) == 0) {
            input->kind  = INPUT_PAD_BUTTON;
            input->value = b;
            return true;
        }
    }
    if (Controls__OldPadName(name, input)) {
        return true;
    }
    const SDL_Scancode scancode = SDL_GetScancodeFromName(name);
    if (scancode != SDL_SCANCODE_UNKNOWN) {
        input->kind  = INPUT_KEY;
        input->value = scancode;
        return true;
    }
    return false;
}

bool Controls_SameInput(const ControlInput *a, const ControlInput *b) {
    return a->kind == b->kind && a->value == b->value && a->mods == b->mods && a->twice == b->twice;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bindings

void Controls_GetDefaults(const ControlAction action, ControlBindings *out) {
    memset(out, 0, sizeof(*out));
    for (int d = 0; d < 3 && actions[action].defaults[d] != NULL; d++) {
        if (Controls_ParseInput(actions[action].defaults[d], &out->inputs[out->count])) {
            out->count++;
        } else {
            LOG_WARN("Controls: can't read the default '%s' of %s", actions[action].defaults[d], actions[action].id);
        }
    }
}

void Controls_Reset(void) {
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        for (int a = 0; a < ACTION_COUNT; a++) {
            Controls_GetDefaults((ControlAction)a, &bindings[p][a]);
        }
        own_profile[p] = p == 0;
    }
    loaded = true;
}

static void Controls__Ensure(void) {
    if (!loaded) {
        Controls_Reset();
    }
}

static int Controls__Profile(const int profile) {
    return (profile > 0 && profile < CONTROL_PROFILES && own_profile[profile]) ? profile : 0;
}

const ControlBindings *Controls_GetBindings(const int profile, const ControlAction action) {
    Controls__Ensure();
    return &bindings[Controls__Profile(profile)][action];
}

void Controls_SetBindings(const int profile, const ControlAction action, const ControlBindings *value) {
    Controls__Ensure();
    if (profile < 0 || profile >= CONTROL_PROFILES) {
        return;
    }
    bindings[Controls__Profile(profile)][action] = *value;
}

bool Controls_HasOwnProfile(const int profile) {
    Controls__Ensure();
    return profile > 0 && profile < CONTROL_PROFILES && own_profile[profile];
}

void Controls_CopyProfile(const int from, const int to) {
    Controls__Ensure();
    if (from < 0 || to <= 0 || from >= CONTROL_PROFILES || to >= CONTROL_PROFILES || from == to) {
        return;
    }
    memcpy(bindings[to], bindings[Controls__Profile(from)], sizeof(bindings[0]));
    own_profile[to] = true;
}

void Controls_SetOwnProfile(const int profile, const bool own) {
    Controls__Ensure();
    if (profile <= 0 || profile >= CONTROL_PROFILES || own_profile[profile] == own) {
        return;
    }
    if (own) {
        memcpy(bindings[profile], bindings[0], sizeof(bindings[0]));
    }
    own_profile[profile] = own;
}

ControlConflict Controls_Conflict(const int profile, const ControlAction action, const ControlInput *input) {
    Controls__Ensure();
    const int       p      = Controls__Profile(profile);
    ControlConflict result = CONFLICT_NONE;
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (a == (int)action) {
            continue;
        }
        for (int b = 0; b < bindings[p][a].count; b++) {
            if (!Controls_SameInput(&bindings[p][a].inputs[b], input)) {
                continue;
            }
            if ((actions[a].contexts & actions[action].contexts) != 0) {
                return CONFLICT_CLASH;
            }
            result = CONFLICT_SHARED;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Playing

void Controls_SetProfile(const int profile) { current_profile = profile; }
void Controls_SetContext(const unsigned context) { current_context = context; }

static bool allowed_anywhere[ACTION_COUNT];

void Controls_AllowAnywhere(const ControlAction action, const bool allowed) { allowed_anywhere[action] = allowed; }

// Whether a binding's modifiers are held (and no others than it asks for: Ctrl+X isn't X)
static bool Controls__ModsMatch(const ControlInput *input, const uint16_t mods) {
    const uint16_t held = (uint16_t)(((mods & KMOD_CTRL) ? KMOD_CTRL : 0) | ((mods & KMOD_SHIFT) ? KMOD_SHIFT : 0) |
                                     ((mods & KMOD_ALT) ? KMOD_ALT : 0));
    if (input->kind == INPUT_KEY) {
        // A modifier key on its own ("Left Ctrl") doesn't count itself as held
        const SDL_Scancode s     = (SDL_Scancode)input->value;
        uint16_t           self  = 0;
        self                    |= (s == SDL_SCANCODE_LCTRL || s == SDL_SCANCODE_RCTRL) ? KMOD_CTRL : 0;
        self                    |= (s == SDL_SCANCODE_LSHIFT || s == SDL_SCANCODE_RSHIFT) ? KMOD_SHIFT : 0;
        self                    |= (s == SDL_SCANCODE_LALT || s == SDL_SCANCODE_RALT) ? KMOD_ALT : 0;
        return (uint16_t)(held & ~self) == input->mods;
    }
    // Mouse and gamepad: the modifiers asked for must be held; others may be (Shift+click still clicks)
    return (held & input->mods) == input->mods;
}

void Controls_Update(void) {
    Controls__Ensure();
    memset(pressed, 0, sizeof(pressed));
    const int  p = Controls__Profile(current_profile);
    InputPress press;
    while (InputManager_PollPress(&press)) {
        const int slot  = press.kind == INPUT_MOUSE_WHEEL ? (press.value > 0 ? 1 : 0) : press.value;
        bool      twice = false;
        if (press.kind <= INPUT_PAD_AXIS && slot >= 0 && slot < SDL_NUM_SCANCODES) {
            twice = last_press_time[press.kind][slot] != 0 &&
                    press.time - last_press_time[press.kind][slot] <= DOUBLE_TAP_MS;
            last_press_time[press.kind][slot] = twice ? 0 : press.time; // a third tap starts over
        }
        // The first action (in list order) for each way the press can be read: once, and as a double tap
        for (int pass = 0; pass < (twice ? 2 : 1); pass++) {
            for (int a = 0; a < ACTION_COUNT; a++) {
                if (((actions[a].contexts & current_context) == 0 && !allowed_anywhere[a]) || !actions[a].works) {
                    continue; // not here, or not in the engine yet (so it doesn't take the key from one that is)
                }
                bool hit = false;
                for (int b = 0; b < bindings[p][a].count && !hit; b++) {
                    const ControlInput *input = &bindings[p][a].inputs[b];
                    hit = input->kind == press.kind && input->value == press.value && input->twice == (pass == 1) &&
                          Controls__ModsMatch(input, press.mods);
                }
                if (hit) {
                    pressed[a] = true;
                    // Clashing actions further down the list don't get it
                    break;
                }
            }
        }
    }
}

bool Controls_Pressed(const ControlAction action) { return pressed[action]; }

float Controls_Amount(const ControlAction action) {
    Controls__Ensure();
    if ((actions[action].contexts & current_context) == 0 && !allowed_anywhere[action]) {
        return 0.0f;
    }
    const ControlBindings *list   = &bindings[Controls__Profile(current_profile)][action];
    const uint16_t         mods   = InputManager_GetMods();
    float                  amount = 0.0f;
    for (int b = 0; b < list->count; b++) {
        const ControlInput *input = &list->inputs[b];
        if (!input->twice && Controls__ModsMatch(input, mods)) {
            const float a = InputManager_GetAmount(input->kind, input->value);
            amount        = a > amount ? a : amount;
        }
    }
    return amount;
}

bool Controls_Held(const ControlAction action) {
    Controls__Ensure();
    if ((actions[action].contexts & current_context) == 0 && !allowed_anywhere[action]) {
        return false;
    }
    const ControlBindings *list = &bindings[Controls__Profile(current_profile)][action];
    const uint16_t         mods = InputManager_GetMods();
    for (int b = 0; b < list->count; b++) {
        const ControlInput *input = &list->inputs[b];
        if (!input->twice && InputManager_IsDown(input->kind, input->value) && Controls__ModsMatch(input, mods)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// options.ini

void Controls_ReadLine(const int profile, const char *key, const char *value) {
    Controls__Ensure();
    if (profile < 0 || profile >= CONTROL_PROFILES) {
        return;
    }
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (strcmp(key, actions[a].id) != 0) {
            continue;
        }
        own_profile[profile]  = true;
        ControlBindings *list = &bindings[profile][a];
        memset(list, 0, sizeof(*list));
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", value);
        for (char *part = strtok(copy, ","); part != NULL && list->count < CONTROL_MAX_BINDINGS;
             part       = strtok(NULL, ",")) {
            if (Controls_ParseInput(part, &list->inputs[list->count])) {
                list->count++;
            }
        }
        return;
    }
}

void Controls_Write(FILE *file) {
    Controls__Ensure();
    static const char *class_names[CONTROL_PROFILES] = {"",        "Amazon",    "Sorceress", "Necromancer",
                                                        "Paladin", "Barbarian", "Druid",     "Assassin"};
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        if (!own_profile[p]) {
            continue;
        }
        if (p == 0) {
            fprintf(file,
                    "# Controls for all classes. Each line: action = inputs, separated by commas. An input is a key\n"
                    "# (\"W\", \"Left Ctrl\", \"F1\", \"Keypad 5\"), \"Mouse Left\" / \"Mouse Middle\" / \"Mouse "
                    "Right\" / \"Mouse 4\" /\n"
                    "# \"Mouse 5\", \"Wheel Up\" / \"Wheel Down\", a gamepad button or stick (\"Pad Button Down\", "
                    "\"Pad Left "
                    "Stick Up\"),\n"
                    "# with modifiers in front (\"Ctrl+\", \"Shift+\", \"Alt+\") or tapped twice (\"2x K\"). Empty = "
                    "not bound.\n"
                    "[Controls]\n");
        } else {
            fprintf(file, "# %s's own controls (delete this section to use the ones for all classes)\n[Controls.%s]\n",
                    class_names[p], class_names[p]);
        }
        for (int a = 0; a < ACTION_COUNT; a++) {
            if (p == 0) {
                fprintf(file, "# %s%s%s%s\n", actions[a].name, actions[a].help != NULL ? ": " : "",
                        actions[a].help != NULL ? actions[a].help : "",
                        actions[a].works ? "" : " (not in the engine yet)");
            }
            fprintf(file, "%s = ", actions[a].id);
            for (int b = 0; b < bindings[p][a].count; b++) {
                char name[64];
                Controls_InputName(&bindings[p][a].inputs[b], name, sizeof(name));
                fprintf(file, "%s%s", b > 0 ? ", " : "", name);
            }
            fprintf(file, "\n");
        }
        fprintf(file, "\n");
    }
}
