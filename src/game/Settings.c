#include "Settings.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "Controls.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPTIONS_FILE "options.ini"

static const char *profile_names[CONTROL_PROFILES]     = {"All classes", "Amazon",    "Sorceress", "Necromancer",
                                                          "Paladin",     "Barbarian", "Druid",     "Assassin"};
static const char *movement_names[MOVEMENT_TYPE_COUNT] = {"Classic", "Modern", "Combined"};

static GameplaySettings gameplay[CONTROL_PROFILES];
static bool             own_gameplay[CONTROL_PROFILES];

const char *Settings_ProfileName(const int profile) {
    return profile >= 0 && profile < CONTROL_PROFILES ? profile_names[profile] : "?";
}

const char *Settings_MovementName(const MovementType movement) {
    return movement < MOVEMENT_TYPE_COUNT ? movement_names[movement] : "?";
}

static int Settings__Profile(const int profile) {
    return (profile > 0 && profile < CONTROL_PROFILES && own_gameplay[profile]) ? profile : 0;
}

const GameplaySettings *Settings_GetGameplay(const int profile) { return &gameplay[Settings__Profile(profile)]; }

void Settings_SetGameplay(const int profile, const GameplaySettings *settings) {
    if (profile >= 0 && profile < CONTROL_PROFILES) {
        gameplay[Settings__Profile(profile)] = *settings;
    }
}

bool Settings_HasOwnGameplay(const int profile) {
    return profile > 0 && profile < CONTROL_PROFILES && own_gameplay[profile];
}

void Settings_SetOwnGameplay(const int profile, const bool own) {
    if (profile <= 0 || profile >= CONTROL_PROFILES || own_gameplay[profile] == own) {
        return;
    }
    if (own) {
        gameplay[profile] = gameplay[0];
    }
    own_gameplay[profile] = own;
}

static void Settings__Path(char *path, const size_t size) {
    snprintf(path, size, "%s/%s", AbyssConfiguration_GetConfigFolder(), OPTIONS_FILE);
}

static char *Settings__Trim(char *text) {
    while (isspace((unsigned char)*text)) {
        text++;
    }
    for (int n = (int)strlen(text) - 1; n >= 0 && isspace((unsigned char)text[n]); n--) {
        text[n] = '\0';
    }
    return text;
}

// "Controls.Barbarian" -> the Barbarian's profile
static int Settings__SectionProfile(const char *section, const char *prefix) {
    const size_t len = strlen(prefix);
    if (strncmp(section, prefix, len) != 0) {
        return -1;
    }
    if (section[len] == '\0') {
        return 0;
    }
    if (section[len] != '.') {
        return -1;
    }
    for (int p = 1; p < CONTROL_PROFILES; p++) {
        if (strcmp(section + len + 1, profile_names[p]) == 0) {
            return p;
        }
    }
    return -1;
}

static DebugSettings debug_settings = {5, true, false};

const DebugSettings *Settings_GetDebug(void) { return &debug_settings; }
void                 Settings_SetDebug(const DebugSettings *settings) { debug_settings = *settings; }

void Settings_Load(void) {
    debug_settings.editor_autosave_minutes = 5;
    debug_settings.editor_keep_changes     = true;
    debug_settings.collision_in_game       = false;
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        gameplay[p].movement    = MOVEMENT_CLASSIC;
        gameplay[p].see_through = true;
        own_gameplay[p]         = p == 0;
    }
    Controls_Reset();

    char path[4096];
    Settings__Path(path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return; // the defaults; written the first time something's changed
    }
    char section[64] = "";
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = Settings__Trim(line);
        if (text[0] == '#' || text[0] == ';' || text[0] == '\0') {
            continue;
        }
        if (text[0] == '[') {
            char *end = strchr(text, ']');
            if (end != NULL) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", text + 1);
            }
            continue;
        }
        char *equals = strchr(text, '=');
        if (equals == NULL) {
            continue;
        }
        *equals           = '\0';
        const char *key   = Settings__Trim(text);
        const char *value = Settings__Trim(equals + 1);

        const int controls = Settings__SectionProfile(section, "Controls");
        if (controls >= 0) {
            Controls_ReadLine(controls, key, value);
            continue;
        }
        if (strcmp(section, "Debug") == 0) {
            if (strcmp(key, "EditorAutoSaveMinutes") == 0) {
                const int minutes                      = atoi(value);
                debug_settings.editor_autosave_minutes = minutes < 0 ? 0 : (minutes > 120 ? 120 : minutes);
            } else if (strcmp(key, "EditorKeepChanges") == 0) {
                debug_settings.editor_keep_changes = strcmp(value, "true") == 0;
            } else if (strcmp(key, "CollisionInGame") == 0) {
                debug_settings.collision_in_game = strcmp(value, "true") == 0;
            }
            continue;
        }
        const int profile = Settings__SectionProfile(section, "Gameplay");
        if (profile < 0) {
            continue;
        }
        own_gameplay[profile] = true;
        if (strcmp(key, "Movement") == 0) {
            for (int m = 0; m < MOVEMENT_TYPE_COUNT; m++) {
                gameplay[profile].movement =
                    strcmp(value, movement_names[m]) == 0 ? (MovementType)m : gameplay[profile].movement;
            }
        } else if (strcmp(key, "SeeThroughObstructions") == 0) {
            gameplay[profile].see_through = strcmp(value, "true") == 0;
        }
    }
    fclose(file);
}

void Settings_Save(void) {
    char path[4096];
    Settings__Path(path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        LOG_WARN("Could not save the options to %s", path);
        return;
    }
    fprintf(file, "# Abyss Engine options: gameplay and controls. Video, audio and the game files are in abyss.ini.\n"
                  "# The game writes this file when you change something in Options; you can edit it too.\n"
                  "# Sections ending in a class name (Gameplay.Barbarian) are that class's own settings.\n\n");
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        if (!own_gameplay[p]) {
            continue;
        }
        if (p == 0) {
            fprintf(file,
                    "[Gameplay]\n"
                    "# Classic (click to walk and attack, like Diablo II), Modern (movement keys to walk, mouse or\n"
                    "# the attack key to attack, like Diablo III / IV) or Combined (both)\n");
        } else {
            fprintf(file, "[Gameplay.%s]\n", profile_names[p]);
        }
        fprintf(file, "Movement = %s\n", movement_names[gameplay[p].movement]);
        if (p == 0) {
            fprintf(file,
                    "# true: walls and trees between you and the camera fade so you can see where you're going\n");
        }
        fprintf(file, "SeeThroughObstructions = %s\n\n", gameplay[p].see_through ? "true" : "false");
    }
    fprintf(file,
            "[Debug]\n"
            "# Map Editor: save the map you're editing every this many minutes (0 = never). A map that was never\n"
            "# saved goes to the temporary file below.\n"
            "EditorAutoSaveMinutes = %d\n"
            "# Map Editor: keep every change in a temporary file too (editor-recovery.abm next to this file), so\n"
            "# nothing is lost if the game closes; the editor offers it back the next time\n"
            "EditorKeepChanges = %s\n"
            "# The collision view (B) in Single Player as well as Free Roam and Testing Grounds\n"
            "CollisionInGame = %s\n\n",
            debug_settings.editor_autosave_minutes, debug_settings.editor_keep_changes ? "true" : "false",
            debug_settings.collision_in_game ? "true" : "false");
    Controls_Write(file);
    fclose(file);
}
