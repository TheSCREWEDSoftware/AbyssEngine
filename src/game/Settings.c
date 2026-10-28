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
static const char *movement_names[MOVEMENT_TYPE_COUNT] = {"Original", "Modern", "Combined"};

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

static DebugSettings debug_settings = {5, true, false, true, false};

const DebugSettings *Settings_GetDebug(void) { return &debug_settings; }
void                 Settings_SetDebug(const DebugSettings *settings) { debug_settings = *settings; }

void Settings_GameplayDefaults(GameplaySettings *settings) {
    settings->movement          = DEFAULT_STYLE_MODERN ? MOVEMENT_MODERN : MOVEMENT_CLASSIC;
    settings->see_through       = true;
    settings->confirm_entrances = false;
    settings->even_screen_speed = false;
    settings->modern_pickup     = DEFAULT_STYLE_MODERN;
    settings->inventory_pages   = 1;
    settings->stash_pages       = 1;
    settings->shared_pages      = 1;
    settings->corner_automap    = false;
}

void Settings_Load(void) {
    debug_settings.editor_autosave_minutes = 5;
    debug_settings.editor_keep_changes     = true;
    debug_settings.collision_in_game       = false;
    debug_settings.confirm_new_seed        = true;
    debug_settings.free_roam_in_game       = false;
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        Settings_GameplayDefaults(&gameplay[p]);
        own_gameplay[p] = p == 0;
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
            } else if (strcmp(key, "ConfirmNewSeed") == 0) {
                debug_settings.confirm_new_seed = strcmp(value, "true") == 0;
            } else if (strcmp(key, "FreeRoamInGame") == 0) {
                debug_settings.free_roam_in_game = strcmp(value, "true") == 0;
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
                    strcmp(value, movement_names[m]) == 0 || (m == 0 && strcmp(value, "Classic") == 0) // (the old name)
                        ? (MovementType)m
                        : gameplay[profile].movement;
            }
        } else if (strcmp(key, "SeeThroughObstructions") == 0) {
            gameplay[profile].see_through = strcmp(value, "true") == 0;
        } else if (strcmp(key, "ConfirmEntrances") == 0) {
            gameplay[profile].confirm_entrances = strcmp(value, "true") == 0;
        } else if (strcmp(key, "EvenScreenSpeed") == 0) {
            gameplay[profile].even_screen_speed = strcmp(value, "true") == 0;
        } else if (strcmp(key, "Automap") == 0) {
            gameplay[profile].corner_automap = strcmp(value, "Corner") == 0;
        } else if (strcmp(key, "WornGearPickup") == 0) {
            gameplay[profile].modern_pickup = strcmp(value, "Modern") == 0;
        } else if (strcmp(key, "InventoryPages") == 0) {
            const int pages                   = atoi(value);
            gameplay[profile].inventory_pages = pages < 1 ? 1 : (pages > 255 ? 255 : pages);
        } else if (strcmp(key, "StashPages") == 0 || strcmp(key, "SharedStashPages") == 0) {
            const int pages = atoi(value);
            *(strcmp(key, "StashPages") == 0 ? &gameplay[profile].stash_pages : &gameplay[profile].shared_pages) =
                pages < 1 ? 1 : (pages > 255 ? 255 : pages);
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
                    "# Original (click to walk and attack, like Diablo II), Modern (movement keys to walk, mouse or\n"
                    "# the attack key to attack, like Diablo III / IV) or Combined (both)\n");
        } else {
            fprintf(file, "[Gameplay.%s]\n", profile_names[p]);
        }
        fprintf(file, "Movement = %s\n", movement_names[gameplay[p].movement]);
        if (p == 0) {
            fprintf(file,
                    "# true: walls and trees between you and the camera fade so you can see where you're going\n");
        }
        fprintf(file, "SeeThroughObstructions = %s\n", gameplay[p].see_through ? "true" : "false");
        if (p == 0) {
            fprintf(file, "# true: ask before going through a cave mouth, tomb door or stairs\n");
        }
        fprintf(file, "ConfirmEntrances = %s\n", gameplay[p].confirm_entrances ? "true" : "false");
        if (p == 0) {
            fprintf(file, "# false: like the game, the same speed over the ground in every direction (so on screen,\n"
                          "# sideways looks twice as fast as up and down); true: the same speed on screen\n");
        }
        fprintf(file, "EvenScreenSpeed = %s\n", gameplay[p].even_screen_speed ? "true" : "false");
        if (p == 0) {
            fprintf(file, "# Full: the automap (Tab) over the whole view, like the game; Corner: small, top right\n");
        }
        fprintf(file, "Automap = %s\n", gameplay[p].corner_automap ? "Corner" : "Full");
        if (p == 0) {
            fprintf(file, "# Original: picking up something you wear takes it off at once, like the game; Modern: it\n"
                          "# stays worn until you put it down somewhere else\n");
        }
        fprintf(file, "WornGearPickup = %s\n", gameplay[p].modern_pickup ? "Modern" : "Original");
        if (p == 0) {
            fprintf(file, "# Pages of the inventory grid, 1 to 255 (Single Player; a multiplayer game sets its own)\n");
        }
        fprintf(file, "InventoryPages = %d\n", gameplay[p].inventory_pages);
        if (p == 0) {
            fprintf(file, "# Pages of the stash and of the shared stash, 1 to 255 (the same)\n");
        }
        fprintf(file, "StashPages = %d\nSharedStashPages = %d\n\n", gameplay[p].stash_pages, gameplay[p].shared_pages);
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
            "CollisionInGame = %s\n"
            "# Free Roam: ask before N makes a new random world\n"
            "ConfirmNewSeed = %s\n"
            "# Free Roam's tools in Single Player and Multiplayer: the Places list from anywhere, and sprint\n"
            "FreeRoamInGame = %s\n\n",
            debug_settings.editor_autosave_minutes, debug_settings.editor_keep_changes ? "true" : "false",
            debug_settings.collision_in_game ? "true" : "false", debug_settings.confirm_new_seed ? "true" : "false",
            debug_settings.free_roam_in_game ? "true" : "false");
    Controls_Write(file);
    fclose(file);
}
