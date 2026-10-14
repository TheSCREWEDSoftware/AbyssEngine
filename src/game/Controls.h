#ifndef ABYSS_CONTROLS_H
#define ABYSS_CONTROLS_H

#include "../managers/InputManager.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Controls: every action you can bind (the game's own, with its default keys, and ours), and what's bound to each.
//
// Bindings work the way ArmA's do: an action can have any number of bindings, and the same input can be on several
// actions. An input is a keyboard key, a mouse button, a wheel step, a gamepad button or stick direction, optionally
// with modifiers held ("Left Ctrl+X") or tapped twice ("2x K"). When an input is on two actions that are active at the
// same time, only the first one in the list fires: that's a clash, shown red; on two actions that are never active
// together (Single Player's C for the character screen and Free Roam's C for combat) it's fine, and shown orange.
//
// Settings are kept for all classes, and a class can have its own (Settings: options.ini next to abyss.ini).

typedef enum ControlAction {
    // Movement
    ACTION_MOVE_ATTACK,
    ACTION_MOVE_UP,
    ACTION_MOVE_DOWN,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_RUN_TOGGLE,
    ACTION_RUN_HOLD,
    ACTION_STAND_STILL,
    // Combat and skills
    ACTION_ATTACK,
    ACTION_RIGHT_SKILL,
    ACTION_SWAP_WEAPONS,
    ACTION_SKILL_1,
    ACTION_SKILL_2,
    ACTION_SKILL_3,
    ACTION_SKILL_4,
    ACTION_SKILL_5,
    ACTION_SKILL_6,
    ACTION_SKILL_7,
    ACTION_SKILL_8,
    ACTION_SKILL_PREVIOUS,
    ACTION_SKILL_NEXT,
    ACTION_SKILL_TREE,
    ACTION_SKILL_BAR,
    // Belt
    ACTION_BELT_1,
    ACTION_BELT_2,
    ACTION_BELT_3,
    ACTION_BELT_4,
    ACTION_SHOW_BELT,
    // Screens
    ACTION_CHARACTER,
    ACTION_INVENTORY,
    ACTION_PARTY,
    ACTION_HIRELING,
    ACTION_QUESTS,
    ACTION_MESSAGE_LOG,
    ACTION_HELP,
    ACTION_CLEAR_SCREEN,
    ACTION_SHOW_ITEMS,
    ACTION_SHOW_PORTRAITS,
    ACTION_WAYPOINT,
    ACTION_TOWN,
    // Automap
    ACTION_AUTOMAP,
    ACTION_AUTOMAP_CENTER,
    ACTION_AUTOMAP_FADE,
    ACTION_AUTOMAP_PARTY,
    ACTION_AUTOMAP_NAMES,
    // View
    ACTION_ZOOM_IN,
    ACTION_ZOOM_OUT,
    ACTION_LIGHT,
    ACTION_SCREENSHOT,
    // Chat and messages
    ACTION_CHAT,
    ACTION_CLEAR_MESSAGES,
    // Free Roam / Testing Grounds (debug)
    ACTION_COMBAT,
    ACTION_COLLISION,
    ACTION_PREVIEW,
    ACTION_NEW_WORLD,
    ACTION_COUNT
} ControlAction;

// Where an action works; actions that share a context and an input clash
#define CONTROL_CONTEXT_GAME  0x01 // Single Player
#define CONTROL_CONTEXT_DEBUG 0x02 // Free Roam, Testing Grounds
#define CONTROL_CONTEXT_ALL   (CONTROL_CONTEXT_GAME | CONTROL_CONTEXT_DEBUG)

typedef enum ControlGroup {
    GROUP_MOVEMENT,
    GROUP_COMBAT,
    GROUP_BELT,
    GROUP_SCREENS,
    GROUP_AUTOMAP,
    GROUP_VIEW,
    GROUP_CHAT,
    GROUP_DEBUG,
    GROUP_COUNT
} ControlGroup;

typedef struct ControlInput {
    InputKind kind;
    int       value;
    uint16_t  mods;  // KMOD_CTRL / KMOD_SHIFT / KMOD_ALT that must be held (either side)
    bool      twice; // tapped twice quickly ("2x K")
} ControlInput;

#define CONTROL_MAX_BINDINGS 8
#define CONTROL_PROFILES     8 // 0 = all classes, then one per class (CharacterClass + 1)

typedef struct ControlBindings {
    int          count;
    ControlInput inputs[CONTROL_MAX_BINDINGS];
} ControlBindings;

typedef enum ControlConflict {
    CONFLICT_NONE,
    CONFLICT_SHARED, // orange: also on another action, never active at the same time
    CONFLICT_CLASH,  // red: also on another action active at the same time; the first in the list wins
} ControlConflict;

// The actions
const char  *Controls_ActionName(ControlAction action); // "Character screen"
const char  *Controls_ActionId(ControlAction action);   // "character" (in options.ini)
const char  *Controls_ActionHelp(ControlAction action); // a line about what it does
ControlGroup Controls_ActionGroup(ControlAction action);
unsigned     Controls_ActionContexts(ControlAction action);
bool         Controls_ActionWorks(ControlAction action); // false: the game has it, we don't yet
const char  *Controls_GroupName(ControlGroup group);

// Inputs as text: "W", "Left Ctrl+X", "2x K", "Mouse Left", "Wheel Up", "Pad Button Down", "Pad Left Stick Up"
void Controls_InputName(const ControlInput *input, char *text, int size);
bool Controls_ParseInput(const char *text, ControlInput *input);
bool Controls_SameInput(const ControlInput *a, const ControlInput *b);

// Bindings of a profile (0 = all classes, class + 1 = that class's; a class without its own uses all classes')
const ControlBindings *Controls_GetBindings(int profile, ControlAction action);
void                   Controls_SetBindings(int profile, ControlAction action, const ControlBindings *bindings);
void                   Controls_GetDefaults(ControlAction action, ControlBindings *bindings);
bool                   Controls_HasOwnProfile(int profile);           // a class with its own controls
void                   Controls_SetOwnProfile(int profile, bool own); // own: starts as a copy of all classes'
ControlConflict        Controls_Conflict(int profile, ControlAction action, const ControlInput *input);

// While playing: which profile is in use, and what's active
void Controls_SetProfile(int profile);
void Controls_SetContext(unsigned context);
void Controls_Update(void);                  // once a frame, before asking
bool Controls_Pressed(ControlAction action); // fired this frame
bool Controls_Held(ControlAction action);    // any of its bindings held down now

// Loading and saving: options.ini's [Controls] and [Controls.<Class>] sections (see Settings)
void Controls_Reset(void); // everything back to the defaults, no class profiles
void Controls_ReadLine(int profile, const char *key, const char *value); // "character = A, C"
void Controls_Write(FILE *file);

#endif // ABYSS_CONTROLS_H
