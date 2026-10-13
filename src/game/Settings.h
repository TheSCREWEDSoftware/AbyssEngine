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
} GameplaySettings;

void Settings_Load(void); // options.ini (missing: the defaults)
void Settings_Save(void);

const char *Settings_ProfileName(int profile); // "All classes", "Amazon", ...
const char *Settings_MovementName(MovementType movement);

const GameplaySettings *Settings_GetGameplay(int profile); // a class without its own: all classes'
void                    Settings_SetGameplay(int profile, const GameplaySettings *settings);
bool                    Settings_HasOwnGameplay(int profile);
void                    Settings_SetOwnGameplay(int profile, bool own); // own: starts as a copy of all classes'

#endif // ABYSS_SETTINGS_H
