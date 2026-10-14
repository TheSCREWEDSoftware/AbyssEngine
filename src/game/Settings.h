#ifndef ABYSS_SETTINGS_H
#define ABYSS_SETTINGS_H

#include <stdbool.h>

// Gameplay settings, and the file they're kept in with the controls: options.ini, next to abyss.ini (video and audio
// stay in abyss.ini). Like the controls, they're kept for all classes, and a class can have its own.
//
// Profiles: 0 = all classes, CharacterClass + 1 = that class (see Controls.h).

typedef enum MovementType {
    MOVEMENT_CLASSIC,  // Diablo II: click to walk, click to attack
    MOVEMENT_MODERN,   // movement keys to walk, mouse (or the attack key) to attack, like Diablo III / IV
    MOVEMENT_COMBINED, // both: movement keys or clicks to walk, clicks or the attack key to attack
    MOVEMENT_TYPE_COUNT
} MovementType;

typedef struct GameplaySettings {
    MovementType movement;
    bool         see_through; // walls and trees between you and the camera fade, so you can see where you're going
    bool         confirm_entrances; // ask before going through a cave mouth, tomb door or stairs
    bool         even_screen_speed; // the same speed on screen in every direction (the game: the same over the ground)
    bool         modern_pickup; // picking up worn gear: Modern keeps it worn until it's put down (Classic: off at once)
    int          inventory_pages; // 1..255: pages of the inventory grid (Single Player; a multiplayer game has its own)
} GameplaySettings;

// The defaults' style (CMake DEFAULT_STYLE / meson default_style): Classic like Diablo II, or Modern
#ifdef ABYSS_DEFAULT_MODERN
#define DEFAULT_STYLE_MODERN true
#else
#define DEFAULT_STYLE_MODERN false
#endif
void Settings_GameplayDefaults(GameplaySettings *settings);

// Debug (Options › Gameplay › Debug): for the developer tools, the same for every class
typedef struct DebugSettings {
    int  editor_autosave_minutes; // Map Editor: save the map every this many minutes (0 = never)
    bool editor_keep_changes;     // Map Editor: keep every change in a temporary file too (back after a crash)
    bool collision_in_game;       // the collision view (B) in Single Player, not only Free Roam / Testing Grounds
    bool confirm_new_seed;        // Free Roam: ask before N makes a new world
    bool free_roam_in_game;       // Free Roam's tools (Places from anywhere, sprint) in Single Player and Multiplayer
} DebugSettings;

void Settings_Load(void); // options.ini (missing: the defaults)
void Settings_Save(void);

const char *Settings_ProfileName(int profile); // "All classes", "Amazon", ...
const char *Settings_MovementName(MovementType movement);

const GameplaySettings *Settings_GetGameplay(int profile); // a class without its own: all classes'
void                    Settings_SetGameplay(int profile, const GameplaySettings *settings);
bool                    Settings_HasOwnGameplay(int profile);
void                    Settings_SetOwnGameplay(int profile, bool own); // own: starts as a copy of all classes'

const DebugSettings *Settings_GetDebug(void);
void                 Settings_SetDebug(const DebugSettings *settings);

#endif // ABYSS_SETTINGS_H
