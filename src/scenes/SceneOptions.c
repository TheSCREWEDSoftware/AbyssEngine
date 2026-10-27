#include "SceneOptions.h"
#include "../audio/Sounds.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Checkbox.h"
#include "../drawing/Cursor.h"
#include "../drawing/Dropdown.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/Slider.h"
#include "../drawing/Sprite.h"
#include "../drawing/TabBar.h"
#include "../drawing/TextBox.h"
#include "../game/ControlPresets.h"
#include "../game/Controls.h"
#include "../game/ControlsExport.h"
#include "../game/Settings.h"
#include "../managers/AudioManager.h"
#include "../managers/InputManager.h"
#include "../net/Protocol.h"
#include "SceneMainMenu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Layout (the game's 800x600)
#define PANEL_X     40
#define PANEL_Y     30
#define PANEL_W     720
#define PANEL_H     520
#define CONTENT_X   70
#define TABS_Y      82
#define CONTENT_Y   122
#define LABEL_W     190 // the setting's name, then its control
#define ROW_H       46
#define BUTTONS_Y   560
#define DESCRIBE_DY 22 // a setting's description, under it

// Controls tab
#define TABLE_Y         200
#define TABLE_ROW_H     18
#define TABLE_ROWS      14
#define TABLE_KEYS_X    330
#define TABLE_W         660
#define TOOLTIP_Y       (TABLE_Y + (TABLE_ROWS * TABLE_ROW_H) + 8)
#define CONTROLS_BTN_Y  (TOOLTIP_Y + 30)
#define DOUBLE_CLICK_MS 400

// Configure dialog
#define DIALOG_X 90
#define DIALOG_Y 70
#define DIALOG_W 620
#define DIALOG_H 480

typedef enum OptionsTab {
    TAB_VIDEO,
    TAB_AUDIO,
    TAB_GAMEPLAY,
    TAB_CONTROLS,
    TAB_MULTIPLAYER,
    TAB_DEBUG,
    TAB_COUNT
} OptionsTab;

static const char *tab_names[TAB_COUNT] = {"Video", "Audio", "Gameplay", "Controls", "Multiplayer", "Debug"};

static const struct {
    const char *name;
    int         width;
    int         height;
} window_sizes[] = {
    {"800 x 600",           800,  600 },
    {"1024 x 768",          1024, 768 },
    {"1280 x 960",          1280, 960 },
    {"1600 x 1200",         1600, 1200},
    {"1280 x 720 (720p)",   1280, 720 },
    {"1920 x 1080 (1080p)", 1920, 1080},
    {"2560 x 1440 (1440p)", 2560, 1440},
    {"3840 x 2160 (4K)",    3840, 2160},
    {"Follow InitialScale", 0,    0   },
};
#define WINDOW_SIZE_COUNT (int)(sizeof(window_sizes) / sizeof(window_sizes[0]))

static const char *scale_qualities[]     = {"nearest", "linear", "best"};
static const char *scale_quality_names[] = {"Sharp pixels (nearest)", "Smooth (linear)", "Smoothest (best)"};

// Frame rate: the slider's steps (frames a second; 0 = no limit), and the two presets beside it
#define FRAME_STEPS 7
static const int   frame_steps[FRAME_STEPS]      = {10, 15, 20, 30, 45, 60, 0};
static const char *frame_step_names[FRAME_STEPS] = {"Slowest", "Slower", "Slow", "Medium", "Fast", "Faster", "Fastest"};
static const char *panel_choices[2]              = {"Classic (the game's)", "Modern"};
static const char *frame_modes[]                 = {"Classic: 25 a second (the game's)", "Modern: 60 a second",
                                                    "Your own (the slider)"};
#define FRAME_CLASSIC 25
#define FRAME_MODERN  60

#define PICTURE_COUNT 3
static const char *picture_names[PICTURE_COUNT] = {"Brightness", "Contrast", "Gamma"};

// Mouse window: pointer speed and wheel speed (percent)
#define MOUSE_MIN 25
#define MOUSE_MAX 300
#define WHEEL_MIN 25
#define WHEEL_MAX 400

#define VOLUME_COUNT 5
static const char *volume_names[VOLUME_COUNT] = {"Master", "Music", "Sound effects", "Interface", "Speech"};

static const char *movement_choices[MOVEMENT_TYPE_COUNT] = {
    "Classic: click to walk and attack",
    "Modern: keys walk, mouse attacks",
    "Combined: both",
};
static const char *pickup_choices[2] = {"Classic: comes off at once", "Modern: worn until put down"};

static const char *movement_help[MOVEMENT_TYPE_COUNT] = {
    "Like Diablo II: click where to walk, click a monster to attack it, click a chest to open it.",
    "Like Diablo III / IV: the movement keys (or a gamepad stick) walk; clicks and the attack key hit what's in reach.",
    "Movement keys and clicks both walk; clicks and the attack key attack.",
};

// Group colours: the stripe by each action in the controls table
static const uint8_t group_colors[GROUP_COUNT][3] = {
    {150, 150, 220}, // movement
    {230, 140, 140}, // combat
    {230, 200, 120}, // belt
    {150, 220, 160}, // screens
    {120, 200, 220}, // automap
    {200, 160, 230}, // view
    {200, 200, 200}, // chat
    {240, 170, 90 }, // debug
};

// Inputs you can't press while the dialog has the mouse: pick them from a list (mouse movement too: moving the mouse
// to the list would bind it). Keys, combinations ("Shift+Alt+A")
// and gamepad buttons and sticks are captured by pressing them.
static const char *extra_inputs[] = {
    "Mouse Left",         "Mouse Right",       "Mouse Middle",      "Mouse 4",          "Mouse 5",
    "Wheel Up",           "Wheel Down",        "2x Mouse Left",     "2x Mouse Right",   "Shift+Mouse Left",
    "Ctrl+Mouse Left",    "Alt+Mouse Left",    "Shift+Mouse Right", "Ctrl+Mouse Right", "Alt+Mouse Right",
    "Shift+Mouse Middle", "Ctrl+Mouse Middle", "Alt+Mouse Middle",  "Shift+Wheel Up",   "Shift+Wheel Down",
    "Ctrl+Wheel Up",      "Ctrl+Wheel Down",   "Alt+Wheel Up",      "Alt+Wheel Down",   "Mouse Move Left",
    "Mouse Move Right",   "Mouse Move Up",     "Mouse Move Down",
};
#define EXTRA_INPUT_COUNT (int)(sizeof(extra_inputs) / sizeof(extra_inputs[0]))

typedef struct SceneOptions {
    bool     closed; // OK or Cancel: done
    TabBar  *tabs;
    Label   *title_label;
    Label   *text_label;  // setting names (font16)
    Label   *small_label; // descriptions, table rows (font8)
    Button  *ok_button;
    Button  *cancel_button;
    Button  *defaults_button;
    char     message[160];
    uint64_t message_until;

    // Video
    Dropdown *window_size;
    Checkbox *fullscreen;
    Dropdown *scale_quality;
    Dropdown *frame_mode;
    Slider   *frame_slider;
    int       frame_before; // to put back on Cancel
    bool      panel_before;
    Slider   *picture[PICTURE_COUNT];
    Dropdown *control_panel;
    int       picture_before[PICTURE_COUNT]; // these, and the scaling, show straight away: put back on Cancel
    char      scale_before[16];

    // Audio
    Slider *volumes[VOLUME_COUNT];
    float   volumes_before[VOLUME_COUNT]; // to put back on Cancel

    // Gameplay and Controls: whose settings (0 = all classes)
    int       profile;
    Dropdown *profile_dropdown;
    Checkbox *own_profile;
    Dropdown *movement;
    Checkbox *see_through;
    Checkbox *confirm_entrances;
    Checkbox *even_speed;
    Dropdown *worn_pickup;
    Slider   *inventory_pages;
    Slider   *stash_pages;
    Slider   *shared_pages;

    // Multiplayer
    Checkbox   *show_names;
    Checkbox   *show_circles;
    Dropdown   *player_color;
    const char *color_names[NET_COLOR_COUNT];
    TextBox    *lobby;

    // Debug (every class)
    Dropdown *autosave;
    Checkbox *keep_changes;
    Checkbox *collision_in_game;
    Checkbox *confirm_new_seed;
    Checkbox *free_roam_in_game;

    // Controls
    Dropdown *group_filter;                     // "Show:"
    int       rows[ACTION_COUNT + GROUP_COUNT]; // an action, or a group's heading (-1 - group)
    int       row_count;
    int       scroll;
    int       hovered_row; // -1 = none
    int       last_click_row;
    uint64_t  last_click_time;
    bool      prev_left;
    Button   *export_button;
    Button   *presets_button;
    Button   *mouse_button;
    Dropdown *copy_to; // "Copy to": the class to give this profile's controls to
    Button   *copy_button;

    // Presets window
    bool     presets;
    ListBox *preset_list;
    char     preset_names[64][CONTROL_PRESET_NAME_MAX];
    char    *preset_items[64];
    int      preset_count;
    TextBox *preset_name;
    Button  *preset_save;
    Button  *preset_load;
    Button  *preset_delete;
    Button  *preset_close;

    // Mouse window
    bool      mouse;
    Checkbox *match_axes;
    Slider   *mouse_x;
    Slider   *mouse_y;
    Slider   *mouse_both;
    Slider   *wheel;
    Button   *mouse_default;
    Button   *mouse_close;
    int       mouse_before[4]; // across, up/down, match, wheel: to put back on Cancel
    bool      scroll_drag;     // dragging the controls table's scrollbar

    // Configure dialog
    bool            dialog;
    int             dialog_row; // index into rows
    ControlBindings dialog_bindings;
    ControlBindings dialog_original;
    int             dialog_selected; // binding, -1 = none
    ListBox        *extras_list;
    Button         *previous_button;
    Button         *next_button;
    Button         *default_button;
    Button         *delete_button;
    Button         *undo_button;
    Button         *dialog_ok_button;
    Button         *dialog_cancel_button;
    bool            dialog_armed;     // the mouse button that opened it has been let go
    int             pending_modifier; // a modifier key pressed alone: bound when it's let go, unless a key follows
    uint64_t        last_bind_time;
    ControlInput    last_bound;
} SceneOptions;

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

static void Options__Wrapped(struct SceneOptions *options, const char *text, int x, int y, int max_x);

static void Options__Message(SceneOptions *options, const char *text) {
    snprintf(options->message, sizeof(options->message), "%s", text);
    options->message_until = SDL_GetTicks64() + 4000;
}

static void Options__Text(SceneOptions *options, Label *label, const char *text, const int x, const int y,
                          const uint8_t r, const uint8_t g, const uint8_t b) {
    (void)options;
    Label_SetText(label, text);
    Label_SetColor(label, r, g, b, 0xFF);
    Label_Draw(label, x, y);
}

static void Options__Fill(const int x, const int y, const int w, const int h, const uint8_t r, const uint8_t g,
                          const uint8_t b, const uint8_t a) {
    const SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, r, g, b, a);
    SDL_RenderFillRect(sdl_renderer, &rect);
}

static void Options__Frame(const int x, const int y, const int w, const int h) {
    const SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
    SDL_RenderDrawRect(sdl_renderer, &rect);
}

static bool Options__AnyDropdownOpen(const SceneOptions *options) {
    return Dropdown_IsOpen(options->window_size) || Dropdown_IsOpen(options->scale_quality) ||
           Dropdown_IsOpen(options->profile_dropdown) || Dropdown_IsOpen(options->movement) ||
           Dropdown_IsOpen(options->autosave) || Dropdown_IsOpen(options->copy_to) ||
           Dropdown_IsOpen(options->group_filter) || Dropdown_IsOpen(options->frame_mode) ||
           Dropdown_IsOpen(options->player_color) || Dropdown_IsOpen(options->worn_pickup) ||
           Dropdown_IsOpen(options->control_panel);
}

// The widgets showing what's set now
static void Options__LoadVideo(SceneOptions *options) {
    int width;
    int height;
    AbyssConfiguration_GetWindowSize(&width, &height);
    int pick = WINDOW_SIZE_COUNT - 1;
    for (int i = 0; i < WINDOW_SIZE_COUNT - 1; i++) {
        pick = (window_sizes[i].width == width && window_sizes[i].height == height) ? i : pick;
    }
    Dropdown_SetSelected(options->window_size, pick);
    Checkbox_SetChecked(options->fullscreen, AbyssConfiguration_GetFullScreen());
    for (int q = 0; q < 3; q++) {
        if (strcmp(AbyssConfiguration_GetScaleQuality(), scale_qualities[q]) == 0) {
            Dropdown_SetSelected(options->scale_quality, q);
        }
    }
    const int rate = AbyssConfiguration_GetFrameRate(false);
    int       step = FRAME_STEPS - 1; // no limit, unless it's one of the steps (or the nearest above it)
    for (int i = FRAME_STEPS - 2; i >= 0; i--) {
        step = rate != 0 && frame_steps[i] >= rate ? i : step;
    }
    Slider_SetValue(options->frame_slider, step);
    Dropdown_SetSelected(options->frame_mode, rate == FRAME_CLASSIC ? 0 : (rate == FRAME_MODERN ? 1 : 2));
    int picture[PICTURE_COUNT];
    AbyssConfiguration_GetPicture(&picture[0], &picture[1], &picture[2]);
    for (int i = 0; i < PICTURE_COUNT; i++) {
        Slider_SetValue(options->picture[i], picture[i]);
    }
}

// The frame rate the Video tab shows; it's used straight away, so you see the difference
static int Options__FrameRate(const SceneOptions *options) {
    switch (Dropdown_GetSelected(options->frame_mode)) {
    case 0:
        return FRAME_CLASSIC;
    case 1:
        return FRAME_MODERN;
    default:
        return frame_steps[Slider_GetValue(options->frame_slider)];
    }
}

// Mouse window: the sliders showing what's set, and the settings from them (used straight away)
static void Options__LoadMouse(SceneOptions *options) {
    int  x;
    int  y;
    bool match;
    int  wheel;
    AbyssConfiguration_GetMouse(&x, &y, &match, &wheel);
    Checkbox_SetChecked(options->match_axes, match);
    Slider_SetValue(options->mouse_x, x);
    Slider_SetValue(options->mouse_y, y);
    Slider_SetValue(options->mouse_both, x);
    Slider_SetValue(options->wheel, wheel);
}

static void Options__ApplyMouse(const SceneOptions *options) {
    const bool match = Checkbox_IsChecked(options->match_axes);
    const int  x     = Slider_GetValue(match ? options->mouse_both : options->mouse_x);
    AbyssConfiguration_SetMouse(x, match ? x : Slider_GetValue(options->mouse_y), match,
                                Slider_GetValue(options->wheel));
    InputManager_ApplyMouseSettings();
}

static void Options__ApplyVolumes(const SceneOptions *options) {
    static const enum AudioSetVolumeType types[VOLUME_COUNT] = {AUDIO_SET_VOLUME_TYPE_MASTER,
                                                                AUDIO_SET_VOLUME_TYPE_MUSIC, AUDIO_SET_VOLUME_TYPE_SFX,
                                                                AUDIO_SET_VOLUME_TYPE_UI, AUDIO_SET_VOLUME_TYPE_SPEECH};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        AudioManager_SetVolume(types[v], (float)Slider_GetValue(options->volumes[v]) / 100.0f);
    }
}

static void Options__LoadGameplay(SceneOptions *options) {
    const GameplaySettings *settings = Settings_GetGameplay(options->profile);
    Dropdown_SetSelected(options->movement, settings->movement);
    Checkbox_SetChecked(options->see_through, settings->see_through);
    Checkbox_SetChecked(options->confirm_entrances, settings->confirm_entrances);
    Checkbox_SetChecked(options->even_speed, settings->even_screen_speed);
    Dropdown_SetSelected(options->worn_pickup, settings->modern_pickup ? 1 : 0);
    Slider_SetValue(options->inventory_pages, settings->inventory_pages);
    Slider_SetValue(options->stash_pages, settings->stash_pages);
    Slider_SetValue(options->shared_pages, settings->shared_pages);
    char text[64];
    snprintf(text, sizeof(text), "Own settings for the %s", Settings_ProfileName(options->profile));
    Checkbox_Destroy(&options->own_profile);
    options->own_profile = Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + 30, text);
    Checkbox_SetChecked(options->own_profile, options->profile == 0 || (Controls_HasOwnProfile(options->profile) ||
                                                                        Settings_HasOwnGameplay(options->profile)));
    Checkbox_SetEnabled(options->own_profile, options->profile != 0);
}

// Debug: the editor's auto-save choices (minutes)
static const int   autosave_minutes[] = {0, 1, 2, 5, 10, 15, 30};
static const char *autosave_names[]   = {"Off",
                                         "Every minute",
                                         "Every 2 minutes",
                                         "Every 5 minutes",
                                         "Every 10 minutes",
                                         "Every 15 minutes",
                                         "Every 30 minutes"};
#define AUTOSAVE_CHOICES (int)(sizeof(autosave_minutes) / sizeof(autosave_minutes[0]))
#define DEBUG_Y          CONTENT_Y

static void Options__LoadDebug(SceneOptions *options) {
    const DebugSettings *debug = Settings_GetDebug();
    int                  pick  = 0;
    for (int i = 0; i < AUTOSAVE_CHOICES; i++) {
        pick = autosave_minutes[i] <= debug->editor_autosave_minutes ? i : pick;
    }
    Dropdown_SetSelected(options->autosave, pick);
    Checkbox_SetChecked(options->keep_changes, debug->editor_keep_changes);
    Checkbox_SetChecked(options->collision_in_game, debug->collision_in_game);
    Checkbox_SetChecked(options->confirm_new_seed, debug->confirm_new_seed);
    Checkbox_SetChecked(options->free_roam_in_game, debug->free_roam_in_game);
}

static void Options__StoreDebug(const SceneOptions *options) {
    DebugSettings debug           = *Settings_GetDebug();
    debug.editor_autosave_minutes = autosave_minutes[Dropdown_GetSelected(options->autosave)];
    debug.editor_keep_changes     = Checkbox_IsChecked(options->keep_changes);
    debug.collision_in_game       = Checkbox_IsChecked(options->collision_in_game);
    debug.confirm_new_seed        = Checkbox_IsChecked(options->confirm_new_seed);
    debug.free_roam_in_game       = Checkbox_IsChecked(options->free_roam_in_game);
    Settings_SetDebug(&debug);
}

// The actions the "Show:" filter lets through
static void Options__BuildRows(SceneOptions *options) {
    const int group    = Dropdown_GetSelected(options->group_filter) - 1; // 0 = all
    options->row_count = 0;
    if (group >= 0) {
        for (int a = 0; a < ACTION_COUNT; a++) {
            if ((int)Controls_ActionGroup((ControlAction)a) == group) {
                options->rows[options->row_count++] = a;
            }
        }
    } else {
        // Everything: group by group, each under its heading
        for (int g = 0; g < GROUP_COUNT; g++) {
            options->rows[options->row_count++] = -1 - g;
            for (int a = 0; a < ACTION_COUNT; a++) {
                if ((int)Controls_ActionGroup((ControlAction)a) == g) {
                    options->rows[options->row_count++] = a;
                }
            }
        }
    }
    options->scroll = 0;
}

static bool Options__IsHeading(const SceneOptions *options, const int row) { return options->rows[row] < 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Create / free

OptionsPanel *OptionsPanel_Create(void) {
    SceneOptions *options = calloc(1, sizeof(SceneOptions));
    FAIL_IF_NULL(options);
#ifdef ABYSS_DEBUG_SHOW_OPTIONS_DEBUG
    options->tabs = TabBar_Create(tab_names, TAB_COUNT, TAB_VIDEO);
#else
    options->tabs = TabBar_Create(tab_names, TAB_COUNT - 1, TAB_VIDEO); // CMake DEBUG_SHOW_OPTIONS_DEBUG is off
#endif
    options->hovered_row    = -1;
    options->last_click_row = -1;

    options->title_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetText(options->title_label, "Options");
    Label_SetAlignment(options->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(options->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    options->text_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(options->text_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    options->small_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetAlignment(options->small_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);

    options->cancel_button   = Button_Create(BUTTON_TYPE_MEDIUM, "Cancel");
    options->defaults_button = Button_Create(BUTTON_TYPE_MEDIUM, "Defaults");
    options->ok_button       = Button_Create(BUTTON_TYPE_MEDIUM, "OK");
    Button_SetPosition(options->cancel_button, PANEL_X, BUTTONS_Y);
    Button_SetPosition(options->defaults_button, 400 - 64, BUTTONS_Y);
    Button_SetPosition(options->ok_button, PANEL_X + PANEL_W - 128, BUTTONS_Y);

    // Video
    const char *size_names[WINDOW_SIZE_COUNT];
    for (int i = 0; i < WINDOW_SIZE_COUNT; i++) {
        size_names[i] = window_sizes[i].name;
    }
    options->window_size   = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y, 260, size_names, WINDOW_SIZE_COUNT);
    options->fullscreen    = Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + ROW_H + 3, "Full screen (Alt+Enter)");
    options->scale_quality = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (2 * ROW_H), 260, scale_quality_names, 3);
    options->frame_mode    = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (3 * ROW_H), 260, frame_modes, 3);
    options->frame_slider  = Slider_Create(CONTENT_X + LABEL_W, CONTENT_Y + (4 * ROW_H) + 6, 300, 0, FRAME_STEPS - 1);
    Slider_SetEndNames(options->frame_slider, frame_step_names[0], frame_step_names[FRAME_STEPS - 1]);
    Slider_SetTypable(options->frame_slider, false);
    for (int i = 0; i < PICTURE_COUNT; i++) {
        options->picture[i] = Slider_Create(CONTENT_X + LABEL_W, CONTENT_Y + ((5 + i) * ROW_H) + 6, 300, 0, 100);
    }
    options->control_panel = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (8 * ROW_H) + 24, 220, panel_choices, 2);
    Dropdown_SetSelected(options->control_panel, AbyssConfiguration_GetModernPanel() ? 1 : 0);
    Options__LoadVideo(options);
    options->frame_before = AbyssConfiguration_GetFrameRate(false);
    options->panel_before = AbyssConfiguration_GetModernPanel();
    AbyssConfiguration_GetPicture(&options->picture_before[0], &options->picture_before[1],
                                  &options->picture_before[2]);
    snprintf(options->scale_before, sizeof(options->scale_before), "%s", AbyssConfiguration_GetScaleQuality());

    // Audio
    const float volumes[VOLUME_COUNT] = {AbyssConfiguration_GetMasterVolume(), AbyssConfiguration_GetMusicVolume(),
                                         AbyssConfiguration_GetSfxVolume(), AbyssConfiguration_GetUiVolume(),
                                         AbyssConfiguration_GetSpeechVolume()};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        options->volumes_before[v] = volumes[v];
        options->volumes[v]        = Slider_Create(CONTENT_X + LABEL_W, CONTENT_Y + (v * ROW_H), 300, 0, 100);
        Slider_SetValue(options->volumes[v], (int)((volumes[v] * 100.0f) + 0.5f));
        Slider_SetFormat(options->volumes[v], 1, 0, "%");
    }

    // Gameplay
    const char *profiles[CONTROL_PROFILES];
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        profiles[p] = Settings_ProfileName(p);
    }
    options->profile_dropdown = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y, 240, profiles, CONTROL_PROFILES);
    options->movement =
        Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (2 * ROW_H), 340, movement_choices, MOVEMENT_TYPE_COUNT);
    options->see_through =
        Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + (4 * ROW_H) + 3, "See through obstructions");
    options->confirm_entrances =
        Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + (3 * ROW_H) + 6, "Ask before going through an entrance");
    options->worn_pickup = Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (7 * ROW_H) + 4, 260, pickup_choices, 2);
    options->inventory_pages = Slider_Create(CONTENT_X + LABEL_W, CONTENT_Y + (8 * ROW_H) + 6, 50, 1, 255);
    options->stash_pages     = Slider_Create(CONTENT_X + LABEL_W + 180, CONTENT_Y + (8 * ROW_H) + 6, 50, 1, 255);
    options->shared_pages    = Slider_Create(CONTENT_X + LABEL_W + 380, CONTENT_Y + (8 * ROW_H) + 6, 45, 1, 255);
    options->even_speed =
        Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + (5 * ROW_H) + 20, "Same speed on screen in every direction");
    Options__LoadGameplay(options);
    options->autosave = Dropdown_Create(CONTENT_X + LABEL_W, DEBUG_Y, 220, autosave_names, AUTOSAVE_CHOICES);
    options->keep_changes =
        Checkbox_Create(CONTENT_X + LABEL_W, DEBUG_Y + 36, "Keep every change in a temporary file too");
    options->collision_in_game =
        Checkbox_Create(CONTENT_X + LABEL_W, DEBUG_Y + 62, "Collision view (B) in Single and Multiplayer");
    options->confirm_new_seed =
        Checkbox_Create(CONTENT_X + LABEL_W, DEBUG_Y + 88, "Ask before a new world (N in Free Roam)");
    options->free_roam_in_game =
        Checkbox_Create(CONTENT_X + LABEL_W, DEBUG_Y + 114, "Free Roam in Single Player and Multiplayer");
    Options__LoadDebug(options);

    // Multiplayer
    {
        bool names;
        bool circles;
        AbyssConfiguration_GetPlayerMarkers(&names, &circles);
        options->show_names   = Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + 3, "Names over the other players");
        options->show_circles = Checkbox_Create(CONTENT_X + LABEL_W, CONTENT_Y + 29, "Circles under their feet");
        Checkbox_SetChecked(options->show_names, names);
        Checkbox_SetChecked(options->show_circles, circles);
        for (int c = 0; c < NET_COLOR_COUNT; c++) {
            options->color_names[c] = NetColor_Name(c);
        }
        options->player_color =
            Dropdown_Create(CONTENT_X + LABEL_W, CONTENT_Y + (2 * ROW_H), 180, options->color_names, NET_COLOR_COUNT);
        for (int c = 0; c < NET_COLOR_COUNT; c++) {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            NetColor_Get(c, &r, &g, &b);
            Dropdown_SetMark(options->player_color, c, r, g, b);
        }
        Dropdown_SetSelected(options->player_color, AbyssConfiguration_GetPlayerColor() % NET_COLOR_COUNT);
        options->lobby = TextBox_Create(FONTS_FONT16, CONTENT_X + LABEL_W, CONTENT_Y + (4 * ROW_H) - 3, 300, 22,
                                        "host:port (none set)");
        TextBox_SetText(options->lobby, AbyssConfiguration_GetLobby());
    }

    // Controls
    const char *groups[GROUP_COUNT + 1];
    groups[0] = "Everything";
    for (int g = 0; g < GROUP_COUNT; g++) {
        groups[g + 1] = Controls_GroupName((ControlGroup)g);
    }
    options->group_filter = Dropdown_Create(CONTENT_X + 480, CONTENT_Y + 27, 180, groups, GROUP_COUNT + 1);
    for (int g = 0; g < GROUP_COUNT; g++) {
        Dropdown_SetMark(options->group_filter, g + 1, group_colors[g][0], group_colors[g][1], group_colors[g][2]);
    }
    Options__BuildRows(options);
    options->export_button  = Button_Create(BUTTON_TYPE_MEDIUM, "Export");
    options->presets_button = Button_Create(BUTTON_TYPE_MEDIUM, "Presets");
    options->mouse_button   = Button_Create(BUTTON_TYPE_CANCEL, "Mouse");
    options->copy_button    = Button_Create(BUTTON_TYPE_CANCEL, "Copy");
    Button_SetPosition(options->export_button, CONTENT_X, CONTROLS_BTN_Y);
    Button_SetPosition(options->presets_button, CONTENT_X + 136, CONTROLS_BTN_Y);
    Button_SetPosition(options->mouse_button, CONTENT_X + 272, CONTROLS_BTN_Y + 1);
    const char *copy_targets[CONTROL_PROFILES];
    for (int p = 0; p < CONTROL_PROFILES; p++) {
        copy_targets[p] = Settings_ProfileName(p);
    }
    options->copy_to = Dropdown_Create(CONTENT_X + 440, CONTROLS_BTN_Y + 7, 120, copy_targets, CONTROL_PROFILES);
    Dropdown_SetSelected(options->copy_to, 1);
    Button_SetPosition(options->copy_button, CONTENT_X + 568, CONTROLS_BTN_Y + 1);

    // Presets window (over the Controls tab, the same place as the configure window)
    options->preset_list   = ListBox_Create(FONTS_FONT16, DIALOG_X + 16, DIALOG_Y + 70, 300, 14, 20);
    options->preset_name   = TextBox_Create(FONTS_FONT16, DIALOG_X + 340, DIALOG_Y + 90, 260, 24, "Preset name");
    options->preset_save   = Button_Create(BUTTON_TYPE_CANCEL, "Save");
    options->preset_load   = Button_Create(BUTTON_TYPE_CANCEL, "Load");
    options->preset_delete = Button_Create(BUTTON_TYPE_CANCEL, "Delete");
    options->preset_close  = Button_Create(BUTTON_TYPE_CANCEL, "Close");
    Button_SetPosition(options->preset_save, DIALOG_X + 340, DIALOG_Y + 130);
    Button_SetPosition(options->preset_load, DIALOG_X + 340, DIALOG_Y + 200);
    Button_SetPosition(options->preset_delete, DIALOG_X + 450, DIALOG_Y + 200);
    Button_SetPosition(options->preset_close, DIALOG_X + DIALOG_W - 112, DIALOG_Y + DIALOG_H - 44);

    // Mouse window
    options->match_axes = Checkbox_Create(DIALOG_X + 16, DIALOG_Y + 48, "Match axes: one speed across and up/down");
    options->mouse_x    = Slider_Create(DIALOG_X + 200, DIALOG_Y + 100, 300, MOUSE_MIN, MOUSE_MAX);
    options->mouse_y    = Slider_Create(DIALOG_X + 200, DIALOG_Y + 146, 300, MOUSE_MIN, MOUSE_MAX);
    options->mouse_both = Slider_Create(DIALOG_X + 200, DIALOG_Y + 100, 300, MOUSE_MIN, MOUSE_MAX);
    options->wheel      = Slider_Create(DIALOG_X + 200, DIALOG_Y + 210, 300, WHEEL_MIN, WHEEL_MAX);
    Slider *speeds[4]   = {options->mouse_x, options->mouse_y, options->mouse_both, options->wheel};
    for (int i = 0; i < 4; i++) {
        Slider_SetFormat(speeds[i], 100, 2, "x");
    }
    options->mouse_default = Button_Create(BUTTON_TYPE_CANCEL, "Default");
    options->mouse_close   = Button_Create(BUTTON_TYPE_CANCEL, "Close");
    Button_SetPosition(options->mouse_default, DIALOG_X + 16, DIALOG_Y + DIALOG_H - 44);
    Button_SetPosition(options->mouse_close, DIALOG_X + DIALOG_W - 112, DIALOG_Y + DIALOG_H - 44);
    Options__LoadMouse(options);
    {
        bool match;
        AbyssConfiguration_GetMouse(&options->mouse_before[0], &options->mouse_before[1], &match,
                                    &options->mouse_before[3]);
        options->mouse_before[2] = match;
    }

    // Configure dialog
    options->extras_list = ListBox_Create(FONTS_FONT16, DIALOG_X + 330, DIALOG_Y + 110, 270, 14, 20);
    ListBox_SetItems(options->extras_list, extra_inputs, EXTRA_INPUT_COUNT);
    ListBox_SetSelectedIndex(options->extras_list, -1);
    options->previous_button      = Button_Create(BUTTON_TYPE_MEDIUM, "Previous");
    options->next_button          = Button_Create(BUTTON_TYPE_MEDIUM, "Next");
    options->default_button       = Button_Create(BUTTON_TYPE_CANCEL, "Default");
    options->delete_button        = Button_Create(BUTTON_TYPE_CANCEL, "Delete");
    options->undo_button          = Button_Create(BUTTON_TYPE_CANCEL, "Undo");
    options->dialog_cancel_button = Button_Create(BUTTON_TYPE_CANCEL, "Cancel");
    options->dialog_ok_button     = Button_Create(BUTTON_TYPE_CANCEL, "OK");
    Button_SetPosition(options->previous_button, DIALOG_X + 16, DIALOG_Y + 42);
    Button_SetPosition(options->next_button, DIALOG_X + DIALOG_W - 144, DIALOG_Y + 42);
    Button_SetPosition(options->default_button, DIALOG_X + 16, DIALOG_Y + 360);
    Button_SetPosition(options->delete_button, DIALOG_X + 116, DIALOG_Y + 360);
    Button_SetPosition(options->undo_button, DIALOG_X + 216, DIALOG_Y + 360);
    Button_SetPosition(options->dialog_cancel_button, DIALOG_X + 16, DIALOG_Y + DIALOG_H - 44);
    Button_SetPosition(options->dialog_ok_button, DIALOG_X + DIALOG_W - 112, DIALOG_Y + DIALOG_H - 44);
    options->pending_modifier = -1;
    return options;
}

void OptionsPanel_Destroy(OptionsPanel **panel) {
    if (panel == NULL || *panel == NULL) {
        return;
    }
    SceneOptions *options = *panel;
    TabBar_Destroy(&options->tabs);
    Label_Destroy(&options->title_label);
    Label_Destroy(&options->text_label);
    Label_Destroy(&options->small_label);
    Button *buttons[] = {
        options->ok_button,        options->cancel_button, options->defaults_button, options->export_button,
        options->presets_button,   options->copy_button,   options->preset_save,     options->preset_load,
        options->preset_delete,    options->preset_close,  options->previous_button, options->next_button,
        options->default_button,   options->delete_button, options->undo_button,     options->dialog_cancel_button,
        options->dialog_ok_button, options->mouse_button,  options->mouse_default,   options->mouse_close};
    for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
        Button_Destroy(&buttons[b]);
    }
    Dropdown_Destroy(&options->window_size);
    Dropdown_Destroy(&options->scale_quality);
    Dropdown_Destroy(&options->frame_mode);
    Dropdown_Destroy(&options->control_panel);
    Slider_Destroy(&options->frame_slider);
    for (int i = 0; i < PICTURE_COUNT; i++) {
        Slider_Destroy(&options->picture[i]);
    }
    Checkbox_Destroy(&options->even_speed);
    Dropdown_Destroy(&options->worn_pickup);
    Slider_Destroy(&options->inventory_pages);
    Slider_Destroy(&options->stash_pages);
    Slider_Destroy(&options->shared_pages);
    Checkbox_Destroy(&options->show_names);
    Checkbox_Destroy(&options->show_circles);
    Dropdown_Destroy(&options->player_color);
    TextBox_Destroy(&options->lobby);
    Checkbox_Destroy(&options->match_axes);
    Slider_Destroy(&options->mouse_x);
    Slider_Destroy(&options->mouse_y);
    Slider_Destroy(&options->mouse_both);
    Slider_Destroy(&options->wheel);
    Dropdown_Destroy(&options->profile_dropdown);
    Dropdown_Destroy(&options->movement);
    Dropdown_Destroy(&options->autosave);
    Dropdown_Destroy(&options->copy_to);
    ListBox_Destroy(&options->preset_list);
    TextBox_Destroy(&options->preset_name);
    Checkbox_Destroy(&options->keep_changes);
    Checkbox_Destroy(&options->collision_in_game);
    Checkbox_Destroy(&options->confirm_new_seed);
    Checkbox_Destroy(&options->free_roam_in_game);
    Dropdown_Destroy(&options->group_filter);
    Checkbox_Destroy(&options->fullscreen);
    Checkbox_Destroy(&options->own_profile);
    Checkbox_Destroy(&options->see_through);
    Checkbox_Destroy(&options->confirm_entrances);
    for (int v = 0; v < VOLUME_COUNT; v++) {
        Slider_Destroy(&options->volumes[v]);
    }
    ListBox_Destroy(&options->extras_list);
    free(options);
    *panel = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// OK / Cancel / Defaults

static void Options__Leave(SceneOptions *options) { options->closed = true; }

static void Options__Save(SceneOptions *options) {
    // Video
    const int size = Dropdown_GetSelected(options->window_size);
    AbyssConfiguration_SetWindowSize(window_sizes[size].width, window_sizes[size].height);
    AbyssConfiguration_SetScaleQuality(scale_qualities[Dropdown_GetSelected(options->scale_quality)]);
    const bool fullscreen = Checkbox_IsChecked(options->fullscreen);
    if (fullscreen != AbyssConfiguration_GetFullScreen()) {
        SDL_SetWindowFullscreen(sdl_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        AbyssConfiguration_SetFullScreen(fullscreen);
    }
    if (!fullscreen) {
        int width;
        int height;
        AbyssConfiguration_GetWindowSize(&width, &height);
        SDL_SetWindowSize(sdl_window, width, height);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, AbyssConfiguration_GetScaleQuality()); // new pictures from now on
    AbyssConfiguration_SetFrameRate(Options__FrameRate(options));
    AbyssConfiguration_SetPicture(Slider_GetValue(options->picture[0]), Slider_GetValue(options->picture[1]),
                                  Slider_GetValue(options->picture[2]));
    Options__ApplyMouse(options);
    AbyssConfiguration_SetPlayerMarkers(Checkbox_IsChecked(options->show_names),
                                        Checkbox_IsChecked(options->show_circles));
    AbyssConfiguration_SetPlayerColor(Dropdown_GetSelected(options->player_color));
    AbyssConfiguration_SetLobby(TextBox_GetText(options->lobby));
    // Audio
    AbyssConfiguration_SetVolumes(
        (float)Slider_GetValue(options->volumes[0]) / 100.0f, (float)Slider_GetValue(options->volumes[1]) / 100.0f,
        (float)Slider_GetValue(options->volumes[2]) / 100.0f, (float)Slider_GetValue(options->volumes[3]) / 100.0f);
    AbyssConfiguration_SetSpeechVolume((float)Slider_GetValue(options->volumes[4]) / 100.0f);
    AbyssConfiguration_Save();
    // Gameplay and controls
    Settings_Save();
}

static void Options__Cancel(SceneOptions *options) {
    static const enum AudioSetVolumeType types[VOLUME_COUNT] = {AUDIO_SET_VOLUME_TYPE_MASTER,
                                                                AUDIO_SET_VOLUME_TYPE_MUSIC, AUDIO_SET_VOLUME_TYPE_SFX,
                                                                AUDIO_SET_VOLUME_TYPE_UI, AUDIO_SET_VOLUME_TYPE_SPEECH};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        AudioManager_SetVolume(types[v], options->volumes_before[v]);
    }
    Settings_Load(); // gameplay and controls as they were saved
    AbyssConfiguration_SetFrameRate(options->frame_before);
    AbyssConfiguration_SetModernPanel(options->panel_before);
    AbyssConfiguration_SetPicture(options->picture_before[0], options->picture_before[1], options->picture_before[2]);
    AbyssConfiguration_SetScaleQuality(options->scale_before);
    AbyssConfiguration_SetMouse(options->mouse_before[0], options->mouse_before[1], options->mouse_before[2] != 0,
                                options->mouse_before[3]);
    InputManager_ApplyMouseSettings();
}

static void Options__Defaults(SceneOptions *options, const OptionsTab tab) {
    switch (tab) {
    case TAB_VIDEO:
        Dropdown_SetSelected(options->window_size, 0);
        Checkbox_SetChecked(options->fullscreen, false);
        Dropdown_SetSelected(options->scale_quality, 0);
        Dropdown_SetSelected(options->frame_mode, 1);
#ifdef ABYSS_DEFAULT_MODERN
        Dropdown_SetSelected(options->control_panel, 1);
        AbyssConfiguration_SetModernPanel(true);
#else
        Dropdown_SetSelected(options->control_panel, 0);
        AbyssConfiguration_SetModernPanel(false);
#endif
        Slider_SetValue(options->frame_slider, 5);
        AbyssConfiguration_SetFrameRate(FRAME_MODERN);
        for (int i = 0; i < PICTURE_COUNT; i++) {
            Slider_SetValue(options->picture[i], 50);
        }
        AbyssConfiguration_SetPicture(50, 50, 50);
        AbyssConfiguration_SetScaleQuality(scale_qualities[0]);
        break;
    case TAB_AUDIO: {
        static const int defaults[VOLUME_COUNT] = {80, 100, 100, 100, 100};
        for (int v = 0; v < VOLUME_COUNT; v++) {
            Slider_SetValue(options->volumes[v], defaults[v]);
        }
        Options__ApplyVolumes(options);
        break;
    }
    case TAB_GAMEPLAY: {
        GameplaySettings defaults;
        Settings_GameplayDefaults(&defaults);
        Settings_SetGameplay(options->profile, &defaults);
        Options__LoadGameplay(options);
        break;
    }
    case TAB_MULTIPLAYER:
        Checkbox_SetChecked(options->show_names, true);
        Checkbox_SetChecked(options->show_circles, true);
        Dropdown_SetSelected(options->player_color, 0);
        TextBox_Clear(options->lobby);
        break;
    case TAB_DEBUG: {
        const DebugSettings debug_defaults = {5, true, false, true, false};
        Settings_SetDebug(&debug_defaults);
        Options__LoadDebug(options);
        break;
    }
    case TAB_CONTROLS:
        for (int a = 0; a < ACTION_COUNT; a++) {
            ControlBindings defaults;
            Controls_GetDefaults((ControlAction)a, &defaults);
            Controls_SetBindings(options->profile, (ControlAction)a, &defaults);
        }
        break;
    default:
        break;
    }
    Options__Message(options, "Back to the defaults on this tab (OK to keep them).");
}

// ---------------------------------------------------------------------------------------------------------------------
// Controls: table, dialog, export

static void Options__OpenDialog(SceneOptions *options, const int row) {
    options->dialog           = true;
    options->dialog_row       = row;
    options->dialog_selected  = -1;
    options->dialog_armed     = false;
    options->dialog_bindings  = *Controls_GetBindings(options->profile, (ControlAction)options->rows[row]);
    options->dialog_original  = options->dialog_bindings;
    options->pending_modifier = -1;
    InputManager_ClearPresses();
    ListBox_SetSelectedIndex(options->extras_list, -1);
}

static void Options__CommitDialog(SceneOptions *options) {
    Controls_SetBindings(options->profile, (ControlAction)options->rows[options->dialog_row],
                         &options->dialog_bindings);
}

static void Options__Bind(SceneOptions *options, const ControlInput *input) {
    ControlBindings *list = &options->dialog_bindings;
    const uint64_t   now  = SDL_GetTicks64();
    // The same key again straight away: a double tap ("2x K")
    if (!input->twice && list->count > 0 && Controls_SameInput(&options->last_bound, input) &&
        now - options->last_bind_time <= 300) {
        for (int b = 0; b < list->count; b++) {
            if (Controls_SameInput(&list->inputs[b], input)) {
                list->inputs[b].twice   = true;
                options->last_bind_time = 0;
                return;
            }
        }
    }
    for (int b = 0; b < list->count; b++) {
        if (Controls_SameInput(&list->inputs[b], input)) {
            options->dialog_selected = b; // already there
            options->last_bound      = *input;
            options->last_bind_time  = now;
            return;
        }
    }
    if (list->count >= CONTROL_MAX_BINDINGS) {
        Options__Message(options, "That's as many as one action can have: delete one first.");
        return;
    }
    list->inputs[list->count] = *input;
    options->dialog_selected  = list->count++;
    options->last_bound       = *input;
    options->last_bind_time   = now;
}

static bool Options__IsModifierKey(const int scancode) {
    return scancode == SDL_SCANCODE_LCTRL || scancode == SDL_SCANCODE_RCTRL || scancode == SDL_SCANCODE_LSHIFT ||
           scancode == SDL_SCANCODE_RSHIFT || scancode == SDL_SCANCODE_LALT || scancode == SDL_SCANCODE_RALT;
}

static void Options__UpdateDialog(SceneOptions *options, const int mouse_x, const int mouse_y, const bool clicked) {
    // Inputs pressed: keys (with the modifiers held), gamepad buttons and sticks. Mouse buttons and the wheel come
    // from the list (the mouse is busy with the dialog).
    InputPress press;
    while (InputManager_PollPress(&press)) {
        if (press.kind == INPUT_KEY && press.value == SDL_SCANCODE_ESCAPE) {
            options->dialog = false; // Esc: cancel
            return;
        }
        if (press.kind == INPUT_KEY && Options__IsModifierKey(press.value)) {
            options->pending_modifier = press.value; // bound alone if nothing follows before it's let go
            continue;
        }
        if (press.kind == INPUT_KEY || press.kind == INPUT_PAD_BUTTON || press.kind == INPUT_PAD_AXIS) {
            ControlInput input = {press.kind, press.value, (uint16_t)(press.mods & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT)),
                                  false};
            // Either side of a modifier counts
            input.mods =
                (uint16_t)(((input.mods & KMOD_CTRL) ? KMOD_CTRL : 0) | ((input.mods & KMOD_SHIFT) ? KMOD_SHIFT : 0) |
                           ((input.mods & KMOD_ALT) ? KMOD_ALT : 0));
            options->pending_modifier = -1;
            Options__Bind(options, &input);
        }
    }
    if (options->pending_modifier >= 0 && !InputManager_IsDown(INPUT_KEY, options->pending_modifier)) {
        const ControlInput input  = {INPUT_KEY, options->pending_modifier, 0, false};
        options->pending_modifier = -1;
        Options__Bind(options, &input);
    }

    bool left;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    if (!options->dialog_armed) {
        // The click that opened it (or pressed Previous / Next) isn't a click in it
        options->dialog_armed = !left;
        return;
    }
    if (ListBox_Update(options->extras_list)) {
        const int    pick = ListBox_GetSelectedIndex(options->extras_list);
        ControlInput input;
        if (pick >= 0 && Controls_ParseInput(extra_inputs[pick], &input)) {
            Options__Bind(options, &input);
        }
        ListBox_SetSelectedIndex(options->extras_list, -1);
    }

    // Picking a binding in the list (to delete it)
    if (clicked && mouse_x >= DIALOG_X + 16 && mouse_x < DIALOG_X + 310 && mouse_y >= DIALOG_Y + 110 &&
        mouse_y < DIALOG_Y + 110 + (CONTROL_MAX_BINDINGS * 22)) {
        const int b              = (mouse_y - (DIALOG_Y + 110)) / 22;
        options->dialog_selected = b < options->dialog_bindings.count ? b : -1;
    }

    const bool previous = Button_Update(options->previous_button);
    const bool next     = Button_Update(options->next_button);
    if (previous || next) {
        Options__CommitDialog(options);
        int row = options->dialog_row;
        do { // the next action, past the headings
            row = (row + (next ? 1 : options->row_count - 1)) % options->row_count;
        } while (Options__IsHeading(options, row));
        Options__OpenDialog(options, row);
    }
    if (Button_Update(options->default_button)) {
        Controls_GetDefaults((ControlAction)options->rows[options->dialog_row], &options->dialog_bindings);
        options->dialog_selected = -1;
    }
    if (Button_Update(options->delete_button) && options->dialog_selected >= 0) {
        ControlBindings *list = &options->dialog_bindings;
        for (int b = options->dialog_selected; b + 1 < list->count; b++) {
            list->inputs[b] = list->inputs[b + 1];
        }
        list->count--;
        options->dialog_selected = options->dialog_selected < list->count ? options->dialog_selected : list->count - 1;
    }
    if (Button_Update(options->undo_button)) {
        options->dialog_bindings = options->dialog_original;
        options->dialog_selected = -1;
    }
    if (Button_Update(options->dialog_cancel_button)) {
        options->dialog = false;
    }
    if (Button_Update(options->dialog_ok_button)) {
        Options__CommitDialog(options);
        options->dialog = false;
    }
}

// The controls for outside the game, next to abyss.ini: controls.md (a table, with the diagrams folded away in it) and
// the keyboard and gamepad diagrams, light and dark
static void Options__Export(SceneOptions *options) {
    static const char *names[4] = {"controls-keyboard.svg", "controls-keyboard-dark.svg", "controls-gamepad.svg",
                                   "controls-gamepad-dark.svg"};
    const char        *folder   = AbyssConfiguration_GetConfigFolder();
    char               path[4200];
    bool               ok = true;
    for (int n = 0; n < 4; n++) {
        snprintf(path, sizeof(path), "%s/%s", folder, names[n]);
        ok = ok && (n < 2 ? ControlsExport_Svg(options->profile, path, n == 1)
                          : ControlsExport_PadSvg(options->profile, path, n == 3));
    }
    snprintf(path, sizeof(path), "%s/controls.md", folder);
    if (!ok || !ControlsExport_Markdown(options->profile, path, true)) {
        Options__Message(options, "Could not write the controls files.");
        return;
    }
    Options__Message(options, "Saved controls.md and the keyboard and gamepad pictures (next to abyss.ini).");
}

// Presets: saved sets of bindings (presets/<name>.ini next to abyss.ini)
static void Options__RefreshPresets(SceneOptions *options) {
    options->preset_count = ControlPresets_List(options->preset_names, 64);
    for (int p = 0; p < options->preset_count; p++) {
        options->preset_items[p] = options->preset_names[p];
    }
    ListBox_SetItems(options->preset_list, (const char *const *)options->preset_items, options->preset_count);
    ListBox_SetSelectedIndex(options->preset_list, -1);
}

static void Options__OpenPresets(SceneOptions *options) {
    options->presets = true;
    Options__RefreshPresets(options);
    TextBox_Clear(options->preset_name);
}

static void Options__UpdatePresets(SceneOptions *options) {
    if (ListBox_Update(options->preset_list)) {
        const int pick = ListBox_GetSelectedIndex(options->preset_list);
        if (pick >= 0 && pick < options->preset_count) {
            TextBox_SetText(options->preset_name, options->preset_names[pick]);
        }
    }
    TextBox_Update(options->preset_name);
    const char *name = TextBox_GetText(options->preset_name);
    char        text[160];
    if (Button_Update(options->preset_save)) {
        if (name[0] == '\0') {
            Options__Message(options, "Type a name for the preset first.");
        } else if (ControlPresets_Save(options->profile, name)) {
            snprintf(text, sizeof(text), "Saved \"%s\".", name);
            Options__Message(options, text);
            Options__RefreshPresets(options);
        }
    }
    if (Button_Update(options->preset_load) && name[0] != '\0') {
        if (ControlPresets_Load(options->profile, name)) {
            snprintf(text, sizeof(text), "Loaded \"%s\" into %s (OK to keep).", name,
                     Settings_ProfileName(options->profile));
        } else {
            snprintf(text, sizeof(text), "There's no preset called \"%s\".", name);
        }
        Options__Message(options, text);
    }
    if (Button_Update(options->preset_delete) && name[0] != '\0') {
        snprintf(text, sizeof(text), ControlPresets_Delete(name) ? "Deleted \"%s\"." : "Could not delete \"%s\".",
                 name);
        Options__Message(options, text);
        TextBox_Clear(options->preset_name);
        Options__RefreshPresets(options);
    }
    if (Button_Update(options->preset_close) ||
        (!TextBox_IsFocused(options->preset_name) && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE))) {
        options->presets = false;
    }
}

static void Options__DrawPresets(SceneOptions *options) {
    Options__Fill(0, 0, 800, 600, 0, 0, 0, 140);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, 18, 16, 14, 245);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, 26, 160, 110, 40, 255);
    Options__Frame(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H);
    Options__Text(options, options->text_label, "PRESETS", DIALOG_X + 10, DIALOG_Y + 13, 0xFF, 0xFF, 0xFF);
    Options__Text(options, options->small_label, "Saved presets:", DIALOG_X + 16, DIALOG_Y + 58, 0xC7, 0xB3, 0x77);
    ListBox_Draw(options->preset_list);
    if (options->preset_count == 0) {
        Options__Text(options, options->small_label, "None yet: name one and Save.", DIALOG_X + 24, DIALOG_Y + 82, 0x90,
                      0x90, 0x90);
    }
    Options__Text(options, options->small_label, "Name:", DIALOG_X + 340, DIALOG_Y + 78, 0xC7, 0xB3, 0x77);
    TextBox_Draw(options->preset_name);
    char text[160];
    snprintf(text, sizeof(text), "Save: these controls (%s) under this name.", Settings_ProfileName(options->profile));
    Options__Wrapped(options, text, DIALOG_X + 440, DIALOG_Y + 140, DIALOG_X + DIALOG_W - 16);
    Options__Wrapped(options, "Load: the named preset into the controls you're setting up. Delete: remove it.",
                     DIALOG_X + 340, DIALOG_Y + 244, DIALOG_X + DIALOG_W - 16);
    Options__Wrapped(options,
                     "Presets are kept in the presets folder next to abyss.ini, one file each, in the same lines as "
                     "options.ini: share them, or edit them by hand.",
                     DIALOG_X + 340, DIALOG_Y + 290, DIALOG_X + DIALOG_W - 16);
    Button_Draw(options->preset_save);
    Button_Draw(options->preset_load);
    Button_Draw(options->preset_delete);
    Button_Draw(options->preset_close);
}

static void Options__UpdateMouse(SceneOptions *options) {
    const bool match   = Checkbox_IsChecked(options->match_axes);
    bool       changed = false;
    if (Checkbox_Update(options->match_axes)) {
        if (Checkbox_IsChecked(options->match_axes)) {
            Slider_SetValue(options->mouse_both, Slider_GetValue(options->mouse_x));
        } else {
            Slider_SetValue(options->mouse_x, Slider_GetValue(options->mouse_both));
            Slider_SetValue(options->mouse_y, Slider_GetValue(options->mouse_both));
        }
        changed = true;
    } else if (match) {
        changed = Slider_Update(options->mouse_both);
    } else {
        changed = Slider_Update(options->mouse_x) | Slider_Update(options->mouse_y);
    }
    changed = Slider_Update(options->wheel) || changed;
    if (Button_Update(options->mouse_default)) {
        Checkbox_SetChecked(options->match_axes, true);
        Slider_SetValue(options->mouse_x, 100);
        Slider_SetValue(options->mouse_y, 100);
        Slider_SetValue(options->mouse_both, 100);
        Slider_SetValue(options->wheel, 100);
        changed = true;
    }
    if (changed) {
        Options__ApplyMouse(options);
    }
    const bool typing = Slider_IsTyping(options->mouse_x) || Slider_IsTyping(options->mouse_y) ||
                        Slider_IsTyping(options->mouse_both) || Slider_IsTyping(options->wheel);
    if (Button_Update(options->mouse_close) || (!typing && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE))) {
        options->mouse = false;
    }
}

static void Options__SliderRow(SceneOptions *options, const char *name, const Slider *slider, const int y) {
    Options__Text(options, options->text_label, name, DIALOG_X + 16, y + 6, 0xC7, 0xB3, 0x77);
    Slider_Draw(slider);
}

static void Options__DrawMouse(SceneOptions *options) {
    Options__Fill(0, 0, 800, 600, 0, 0, 0, 140);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, 18, 16, 14, 245);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, 26, 160, 110, 40, 255);
    Options__Frame(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H);
    Options__Text(options, options->text_label, "MOUSE", DIALOG_X + 10, DIALOG_Y + 13, 0xFF, 0xFF, 0xFF);
    Checkbox_Draw(options->match_axes);
    if (Checkbox_IsChecked(options->match_axes)) {
        Options__SliderRow(options, "Pointer speed", options->mouse_both, DIALOG_Y + 100);
    } else {
        Options__SliderRow(options, "Across (X)", options->mouse_x, DIALOG_Y + 100);
        Options__SliderRow(options, "Up and down (Y)", options->mouse_y, DIALOG_Y + 146);
    }
    Options__SliderRow(options, "Wheel", options->wheel, DIALOG_Y + 210);
    Options__Wrapped(options,
                     "1.00x is the system's own pointer speed (drag, or type a number from 0.25 to 3.00). At any other "
                     "speed the game moves its own pointer, "
                     "so the mouse stays in the window while it has the focus (Alt+Tab to leave).",
                     DIALOG_X + 16, DIALOG_Y + 260, DIALOG_X + DIALOG_W - 16);
    Options__Wrapped(options,
                     "Wheel: how far each notch scrolls lists and the Map Editor (fine-stepping wheels and touchpads "
                     "add up their part steps). The changes work straight away; Options' Cancel puts them back.",
                     DIALOG_X + 16, DIALOG_Y + 300, DIALOG_X + DIALOG_W - 16);
    Button_Draw(options->mouse_default);
    Button_Draw(options->mouse_close);
}

// The controls table's scrollbar: the track and thumb (wider than the old 4 pixels), and where to click
#define SCROLLBAR_W 10
#define SCROLLBAR_X (CONTENT_X + TABLE_W - SCROLLBAR_W - 1)

static void Options__ScrollTo(SceneOptions *options, const int mouse_y) {
    const int most  = options->row_count - TABLE_ROWS;
    const int track = TABLE_ROWS * TABLE_ROW_H;
    const int thumb = track * TABLE_ROWS / options->row_count;
    const int at    = mouse_y - TABLE_Y - (thumb / 2);
    int       pick  = track > thumb ? (at * most + ((track - thumb) / 2)) / (track - thumb) : 0;
    options->scroll = pick < 0 ? 0 : (pick > most ? most : pick);
}

static void Options__UpdateControls(SceneOptions *options, const int mouse_x, const int mouse_y, const bool clicked) {
    if (Dropdown_Update(options->group_filter)) {
        Options__BuildRows(options);
    }
    if (Button_Update(options->export_button)) {
        Options__Export(options);
    }
    if (Button_Update(options->presets_button)) {
        Options__OpenPresets(options);
        return;
    }
    if (Button_Update(options->mouse_button)) {
        Options__LoadMouse(options);
        options->mouse = true;
        return;
    }
    Dropdown_Update(options->copy_to);
    if (Button_Update(options->copy_button)) {
        const int to = Dropdown_GetSelected(options->copy_to);
        char      text[160];
        if (to == 0 || to == options->profile) {
            snprintf(text, sizeof(text),
                     "Pick a class to copy to (all classes' controls are the defaults for the rest).");
        } else {
            Controls_CopyProfile(options->profile, to);
            snprintf(text, sizeof(text), "%s now has its own copy of these controls (OK to keep).",
                     Settings_ProfileName(to));
        }
        Options__Message(options, text);
    }
    const int wheel = InputManager_ConsumeMouseWheel();
    const int most  = options->row_count > TABLE_ROWS ? options->row_count - TABLE_ROWS : 0;
    options->scroll = options->scroll - (wheel * 3);
    options->scroll = options->scroll < 0 ? 0 : (options->scroll > most ? most : options->scroll);

    // The scrollbar: click or drag along it
    bool left;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool on_bar = most > 0 && mouse_x >= SCROLLBAR_X - 2 && mouse_x < CONTENT_X + TABLE_W && mouse_y >= TABLE_Y &&
                        mouse_y < TABLE_Y + (TABLE_ROWS * TABLE_ROW_H);
    if (clicked && on_bar) {
        options->scroll_drag = true;
    }
    options->scroll_drag = options->scroll_drag && left;
    if (options->scroll_drag) {
        Options__ScrollTo(options, mouse_y);
        options->hovered_row = -1;
        return;
    }

    options->hovered_row = -1;
    if (mouse_x >= CONTENT_X && mouse_x < SCROLLBAR_X - 2 && mouse_y >= TABLE_Y &&
        mouse_y < TABLE_Y + (TABLE_ROWS * TABLE_ROW_H)) {
        const int row        = options->scroll + ((mouse_y - TABLE_Y) / TABLE_ROW_H);
        options->hovered_row = row < options->row_count && !Options__IsHeading(options, row) ? row : -1;
    }
    if (clicked && options->hovered_row >= 0) {
        // Double click (or click the keys) to set it up
        const uint64_t now = SDL_GetTicks64();
        if ((options->last_click_row == options->hovered_row && now - options->last_click_time <= DOUBLE_CLICK_MS) ||
            mouse_x >= CONTENT_X + TABLE_KEYS_X) {
            Options__OpenDialog(options, options->hovered_row);
        }
        options->last_click_row  = options->hovered_row;
        options->last_click_time = now;
    }
}

// One action's inputs, each coloured: white, orange (also used elsewhere), red (clashes)
static void Options__DrawInputs(SceneOptions *options, const ControlAction action, const ControlBindings *list, int x,
                                const int y, const int max_x, const int selected) {
    if (list->count == 0) {
        Options__Text(options, options->small_label, "-", x, y, 0x80, 0x80, 0x80);
        return;
    }
    for (int b = 0; b < list->count; b++) {
        char name[72];
        char quoted[80];
        Controls_InputName(&list->inputs[b], name, sizeof(name));
        snprintf(quoted, sizeof(quoted), "\"%s\"%s", name, b + 1 < list->count ? ", " : "");
        const int width = Label_MeasureText(options->small_label, quoted);
        if (x + width > max_x) {
            Options__Text(options, options->small_label, "...", x, y, 0xC0, 0xC0, 0xC0);
            return;
        }
        const ControlConflict conflict = Controls_Conflict(options->profile, action, &list->inputs[b]);
        if (b == selected) {
            Options__Fill(x - 2, y - 7, width, 14, 120, 95, 40, 200);
        }
        Options__Text(options, options->small_label, quoted, x, y, conflict == CONFLICT_CLASH ? 0xFF : 0xFF,
                      conflict == CONFLICT_CLASH ? 0x40 : (conflict == CONFLICT_SHARED ? 0xA8 : 0xFF),
                      conflict == CONFLICT_NONE ? 0xFF : 0x30);
        x += width;
    }
}

static void Options__DrawControls(SceneOptions *options) {
    Options__Text(options, options->text_label, "Show:", CONTENT_X + 430, CONTENT_Y + 37, 0xC7, 0xB3, 0x77);
    // Header
    Options__Fill(CONTENT_X, TABLE_Y - 20, TABLE_W, 18, 60, 50, 35, 230);
    Options__Text(options, options->small_label, "ACTION", CONTENT_X + 6, TABLE_Y - 11, 0xFF, 0xFF, 0xFF);
    Options__Text(options, options->small_label, "ASSIGNED INPUTS", CONTENT_X + TABLE_KEYS_X, TABLE_Y - 11, 0xFF, 0xFF,
                  0xFF);
    Options__Fill(CONTENT_X, TABLE_Y, TABLE_W, TABLE_ROWS * TABLE_ROW_H, 10, 10, 10, 200);
    for (int r = 0; r < TABLE_ROWS && options->scroll + r < options->row_count; r++) {
        const int row = options->scroll + r;
        const int y   = TABLE_Y + (r * TABLE_ROW_H);
        if (Options__IsHeading(options, row)) {
            // A group's heading: its colour, its name, a line across
            const int      group = -1 - options->rows[row];
            const uint8_t *gc    = group_colors[group];
            Options__Fill(CONTENT_X, y, SCROLLBAR_X - CONTENT_X - 2, TABLE_ROW_H, 45, 38, 26, 230);
            Options__Fill(CONTENT_X + 2, y + TABLE_ROW_H - 2, SCROLLBAR_X - CONTENT_X - 6, 1, gc[0], gc[1], gc[2], 200);
            char heading[64];
            snprintf(heading, sizeof(heading), "%s", Controls_GroupName((ControlGroup)group));
            for (char *c = heading; *c != '\0'; c++) {
                *c = (char)toupper((unsigned char)*c);
            }
            Options__Text(options, options->small_label, heading, CONTENT_X + 6, y + (TABLE_ROW_H / 2), gc[0], gc[1],
                          gc[2]);
            continue;
        }
        const ControlAction action = (ControlAction)options->rows[row];
        if (row == options->hovered_row) {
            Options__Fill(CONTENT_X, y, TABLE_W, TABLE_ROW_H, 80, 70, 50, 160);
        }
        const bool     works = Controls_ActionWorks(action);
        const uint8_t *c     = group_colors[Controls_ActionGroup(action)];
        Options__Fill(CONTENT_X + 2, y + 4, 4, TABLE_ROW_H - 8, c[0], c[1], c[2], 255);
        char name[72];
        snprintf(name, sizeof(name), "%s%s", Controls_ActionName(action), works ? "" : " *");
        Options__Text(options, options->small_label, name, CONTENT_X + 12, y + (TABLE_ROW_H / 2), works ? 0xFF : 0x90,
                      works ? 0xFF : 0x90, works ? 0xFF : 0x90);
        Options__DrawInputs(options, action, Controls_GetBindings(options->profile, action), CONTENT_X + TABLE_KEYS_X,
                            y + (TABLE_ROW_H / 2), SCROLLBAR_X - 6, -1);
    }
    Options__Frame(CONTENT_X, TABLE_Y, TABLE_W, TABLE_ROWS * TABLE_ROW_H);
    // Scrollbar
    if (options->row_count > TABLE_ROWS) {
        const int track = TABLE_ROWS * TABLE_ROW_H;
        const int thumb = track * TABLE_ROWS / options->row_count;
        const int at    = (track - thumb) * options->scroll / (options->row_count - TABLE_ROWS);
        Options__Fill(SCROLLBAR_X, TABLE_Y + 1, SCROLLBAR_W, track - 2, 35, 30, 22, 255);
        Options__Fill(SCROLLBAR_X + 1, TABLE_Y + at + 1, SCROLLBAR_W - 2, thumb - 2, options->scroll_drag ? 200 : 150,
                      options->scroll_drag ? 170 : 125, options->scroll_drag ? 95 : 70, 255);
    }
    // What the hovered action does (the tooltip), or how to use the table
    char help[200];
    if (options->hovered_row >= 0) {
        const ControlAction action = (ControlAction)options->rows[options->hovered_row];
        const char         *text   = Controls_ActionHelp(action);
        snprintf(help, sizeof(help), "%s%s%s", text != NULL ? text : Controls_ActionName(action),
                 Controls_ActionWorks(action) ? "" : "  (* not in the engine yet)", "   - double click to change");
    } else {
        snprintf(help, sizeof(help),
                 "Double click an action (or click its inputs) to change them.  Red: clashes "
                 "with another action (the first wins).  Orange: also used elsewhere.");
    }
    Options__Wrapped(options, help, CONTENT_X, TOOLTIP_Y + 7, PANEL_X + PANEL_W - 16);
}

static void Options__DrawDialog(SceneOptions *options) {
    Options__Fill(0, 0, 800, 600, 0, 0, 0, 140);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, 18, 16, 14, 245);
    Options__Fill(DIALOG_X, DIALOG_Y, DIALOG_W, 26, 160, 110, 40, 255);
    Options__Frame(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H);
    Options__Text(options, options->text_label, "CONFIGURE ACTION", DIALOG_X + 10, DIALOG_Y + 13, 0xFF, 0xFF, 0xFF);
    Label_SetAlignment(options->text_label, LABEL_ALIGN_END, LABEL_ALIGN_CENTER);
    Options__Text(options, options->text_label, Settings_ProfileName(options->profile), DIALOG_X + DIALOG_W - 10,
                  DIALOG_Y + 13, 0xFF, 0xFF, 0xFF);
    Label_SetAlignment(options->text_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    const ControlAction action = (ControlAction)options->rows[options->dialog_row];
    Options__Text(options, options->text_label, Controls_ActionName(action), DIALOG_X + (DIALOG_W / 2), DIALOG_Y + 58,
                  0xFF, 0xE8, 0xA0);
    Label_SetAlignment(options->text_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Button_Draw(options->previous_button);
    Button_Draw(options->next_button);

    // Its inputs
    Options__Text(options, options->small_label, "Bound to:", DIALOG_X + 16, DIALOG_Y + 98, 0xC7, 0xB3, 0x77);
    Options__Fill(DIALOG_X + 16, DIALOG_Y + 110, 294, CONTROL_MAX_BINDINGS * 22, 0, 0, 0, 200);
    for (int b = 0; b < options->dialog_bindings.count; b++) {
        char                  name[72];
        const ControlConflict conflict =
            Controls_Conflict(options->profile, action, &options->dialog_bindings.inputs[b]);
        Controls_InputName(&options->dialog_bindings.inputs[b], name, sizeof(name));
        const int y = DIALOG_Y + 110 + (b * 22);
        if (b == options->dialog_selected) {
            Options__Fill(DIALOG_X + 16, y, 294, 22, 120, 95, 40, 200);
        }
        Options__Text(options, options->text_label, name, DIALOG_X + 24, y + 11, 0xFF,
                      conflict == CONFLICT_CLASH ? 0x40 : (conflict == CONFLICT_SHARED ? 0xA8 : 0xFF),
                      conflict == CONFLICT_NONE ? 0xFF : 0x30);
    }
    Options__Frame(DIALOG_X + 16, DIALOG_Y + 110, 294, CONTROL_MAX_BINDINGS * 22);
    Button_Draw(options->default_button);
    Button_Draw(options->delete_button);
    Button_Draw(options->undo_button);

    // More inputs
    Options__Text(options, options->small_label, "Mouse and wheel:", DIALOG_X + 330, DIALOG_Y + 98, 0xC7, 0xB3, 0x77);
    ListBox_Draw(options->extras_list);

    const char *hint = options->pending_modifier >= 0
                           ? "Holding a modifier: press another key for a combination, or "
                             "let go to bind the modifier itself."
                           : "Press any key or gamepad button to bind it (twice quickly for a "
                             "double tap).  Esc: cancel.";
    Options__Text(options, options->small_label, hint, DIALOG_X + 16, DIALOG_Y + 406, 0xFF, 0xFF, 0xFF);
    const char *help = Controls_ActionHelp(action);
    if (help != NULL) {
        Options__Text(options, options->small_label, help, DIALOG_X + 16, DIALOG_Y + 84, 0xA0, 0xA0, 0xA0);
    }
    Button_Draw(options->dialog_cancel_button);
    Button_Draw(options->dialog_ok_button);
}

// ---------------------------------------------------------------------------------------------------------------------
// Update / render

static void OptionsPanel__Update(SceneOptions *options) {
    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked = left && !options->prev_left;
    options->prev_left = left;

    if (options->dialog) {
        Options__UpdateDialog(options, mouse_x, mouse_y, clicked);
        return;
    }
    if (options->presets) {
        Options__UpdatePresets(options);
        return;
    }
    if (options->mouse) {
        Options__UpdateMouse(options);
        InputManager_ClearPresses();
        return;
    }
    InputManager_ClearPresses(); // presses are only read by the dialog

    const OptionsTab tab = (OptionsTab)TabBar_GetSelected(options->tabs);
    if (Options__AnyDropdownOpen(options)) {
        // An open list takes all the clicks. Only the open one hears them: a closed one at the same place on
        // another tab (Window size sits where "Settings for" is) would open on the click that closes this one.
        if (Dropdown_IsOpen(options->window_size)) {
            Dropdown_Update(options->window_size);
        } else if (Dropdown_IsOpen(options->scale_quality)) {
            if (Dropdown_Update(options->scale_quality)) {
                AbyssConfiguration_SetScaleQuality(scale_qualities[Dropdown_GetSelected(options->scale_quality)]);
            }
        } else if (Dropdown_IsOpen(options->profile_dropdown)) {
            if (Dropdown_Update(options->profile_dropdown)) {
                options->profile = Dropdown_GetSelected(options->profile_dropdown);
                Options__LoadGameplay(options);
            }
        } else if (Dropdown_IsOpen(options->movement)) {
            if (Dropdown_Update(options->movement)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.movement         = (MovementType)Dropdown_GetSelected(options->movement);
                Settings_SetGameplay(options->profile, &settings);
            }
        } else if (Dropdown_IsOpen(options->group_filter)) {
            if (Dropdown_Update(options->group_filter)) {
                Options__BuildRows(options);
            }
        } else if (Dropdown_IsOpen(options->copy_to)) {
            Dropdown_Update(options->copy_to);
        } else if (Dropdown_IsOpen(options->frame_mode)) {
            if (Dropdown_Update(options->frame_mode)) {
                if (Dropdown_GetSelected(options->frame_mode) == 1) {
                    Slider_SetValue(options->frame_slider, 5);
                }
                AbyssConfiguration_SetFrameRate(Options__FrameRate(options));
            }
        } else if (Dropdown_IsOpen(options->control_panel)) {
            if (Dropdown_Update(options->control_panel)) {
                AbyssConfiguration_SetModernPanel(Dropdown_GetSelected(options->control_panel) == 1); // straight away
            }
        } else if (Dropdown_IsOpen(options->worn_pickup)) {
            if (Dropdown_Update(options->worn_pickup)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.modern_pickup    = Dropdown_GetSelected(options->worn_pickup) == 1;
                Settings_SetGameplay(options->profile, &settings);
            }
        } else if (Dropdown_IsOpen(options->player_color)) {
            Dropdown_Update(options->player_color);
        } else if (Dropdown_IsOpen(options->autosave)) {
            if (Dropdown_Update(options->autosave)) {
                Options__StoreDebug(options);
            }
        }
        return;
    }

    if (TabBar_Update(options->tabs, CONTENT_X, TABS_Y, PANEL_W - 60)) {
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    switch (tab) {
    case TAB_VIDEO:
        Dropdown_Update(options->window_size);
        Checkbox_Update(options->fullscreen);
        if (Dropdown_Update(options->scale_quality)) {
            AbyssConfiguration_SetScaleQuality(scale_qualities[Dropdown_GetSelected(options->scale_quality)]);
        }
        Dropdown_Update(options->frame_mode);
        if (Dropdown_Update(options->control_panel)) {
            AbyssConfiguration_SetModernPanel(Dropdown_GetSelected(options->control_panel) == 1);
        }
        if (Slider_Update(options->frame_slider)) {
            Dropdown_SetSelected(options->frame_mode, Slider_GetValue(options->frame_slider) == 5 ? 1 : 2);
            AbyssConfiguration_SetFrameRate(Options__FrameRate(options));
        }
        bool picture_changed = false;
        for (int i = 0; i < PICTURE_COUNT; i++) {
            picture_changed = Slider_Update(options->picture[i]) || picture_changed;
        }
        if (picture_changed) {
            AbyssConfiguration_SetPicture(Slider_GetValue(options->picture[0]), Slider_GetValue(options->picture[1]),
                                          Slider_GetValue(options->picture[2]));
        }
        break;
    case TAB_AUDIO:
        for (int v = 0; v < VOLUME_COUNT; v++) {
            if (Slider_Update(options->volumes[v])) {
                Options__ApplyVolumes(options);
            }
        }
        break;
    case TAB_GAMEPLAY:
    case TAB_CONTROLS:
        if (Dropdown_Update(options->profile_dropdown)) {
            options->profile = Dropdown_GetSelected(options->profile_dropdown);
            Options__LoadGameplay(options);
        }
        if (Checkbox_Update(options->own_profile)) {
            const bool own = Checkbox_IsChecked(options->own_profile);
            Settings_SetOwnGameplay(options->profile, own);
            Controls_SetOwnProfile(options->profile, own);
            Options__LoadGameplay(options);
        }
        if (tab == TAB_GAMEPLAY) {
            const bool editable = options->profile == 0 || Settings_HasOwnGameplay(options->profile);
            if (editable && Dropdown_Update(options->movement)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.movement         = (MovementType)Dropdown_GetSelected(options->movement);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Checkbox_Update(options->see_through)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.see_through      = Checkbox_IsChecked(options->see_through);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Checkbox_Update(options->confirm_entrances)) {
                GameplaySettings settings  = *Settings_GetGameplay(options->profile);
                settings.confirm_entrances = Checkbox_IsChecked(options->confirm_entrances);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Dropdown_Update(options->worn_pickup)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.modern_pickup    = Dropdown_GetSelected(options->worn_pickup) == 1;
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Slider_Update(options->inventory_pages)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.inventory_pages  = Slider_GetValue(options->inventory_pages);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Slider_Update(options->stash_pages)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.stash_pages      = Slider_GetValue(options->stash_pages);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Slider_Update(options->shared_pages)) {
                GameplaySettings settings = *Settings_GetGameplay(options->profile);
                settings.shared_pages     = Slider_GetValue(options->shared_pages);
                Settings_SetGameplay(options->profile, &settings);
            }
            if (editable && Checkbox_Update(options->even_speed)) {
                GameplaySettings settings  = *Settings_GetGameplay(options->profile);
                settings.even_screen_speed = Checkbox_IsChecked(options->even_speed);
                Settings_SetGameplay(options->profile, &settings);
            }
        } else if (options->profile == 0 || Controls_HasOwnProfile(options->profile)) {
            Options__UpdateControls(options, mouse_x, mouse_y, clicked);
        } else if (clicked && mouse_y >= TABLE_Y && mouse_y < TABLE_Y + (TABLE_ROWS * TABLE_ROW_H)) {
            Options__Message(options, "Tick \"Own settings\" to give this class controls of its own.");
        }
        break;
    case TAB_MULTIPLAYER:
        Checkbox_Update(options->show_names);
        Checkbox_Update(options->show_circles);
        Dropdown_Update(options->player_color);
        TextBox_Update(options->lobby);
        break;
    case TAB_DEBUG:
        if (Dropdown_Update(options->autosave) | Checkbox_Update(options->keep_changes) |
            Checkbox_Update(options->collision_in_game) | Checkbox_Update(options->confirm_new_seed) |
            Checkbox_Update(options->free_roam_in_game)) {
            Options__StoreDebug(options);
        }
        break;
    default:
        break;
    }

    if (Button_Update(options->defaults_button)) {
        Options__Defaults(options, tab);
    }
    if (Button_Update(options->ok_button)) {
        Options__Save(options);
        Options__Leave(options);
        return;
    }
    bool typing = TextBox_IsFocused(options->lobby); // Esc in a box clears the box, not the whole window
    for (int v = 0; v < VOLUME_COUNT; v++) {
        typing = typing || Slider_IsTyping(options->volumes[v]);
    }
    for (int i = 0; i < PICTURE_COUNT; i++) {
        typing = typing || Slider_IsTyping(options->picture[i]);
    }
    typing = typing || Slider_IsTyping(options->inventory_pages) || Slider_IsTyping(options->stash_pages) ||
             Slider_IsTyping(options->shared_pages);
    if (Button_Update(options->cancel_button) || (!typing && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE))) {
        Options__Cancel(options);
        Options__Leave(options);
        return;
    }
}

// Small grey text, wrapped at the panel's edge
static void Options__Wrapped(SceneOptions *options, const char *text, const int x, int y, const int max_x) {
    char        line[256];
    const char *word = text;
    line[0]          = '\0';
    while (*word != '\0') {
        const char *end = strchr(word, ' ');
        const int   len = end != NULL ? (int)(end - word) : (int)strlen(word);
        char        candidate[512];
        snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] != '\0' ? " " : "", len, word);
        if (line[0] != '\0' && x + Label_MeasureText(options->small_label, candidate) > max_x) {
            Options__Text(options, options->small_label, line, x, y, 0xA0, 0xA0, 0xA0);
            y += 12;
            snprintf(line, sizeof(line), "%.*s", len, word);
        } else {
            snprintf(line, sizeof(line), "%.255s", candidate);
        }
        word += len;
        while (*word == ' ') {
            word++;
        }
    }
    if (line[0] != '\0') {
        Options__Text(options, options->small_label, line, x, y, 0xA0, 0xA0, 0xA0);
    }
}

static void Options__Describe(SceneOptions *options, const char *text, const int y) {
    Options__Wrapped(options, text, CONTENT_X + LABEL_W, y + DESCRIBE_DY + 4, PANEL_X + PANEL_W - 16);
}

static void Options__Name(SceneOptions *options, const char *text, const int y) {
    Options__Text(options, options->text_label, text, CONTENT_X, y + 10, 0xC7, 0xB3, 0x77);
}

// "Settings for": whose settings these are, in a band of its own so it stands out (it changes the whole tab)
static void Options__ClassBand(SceneOptions *options) {
    Options__Fill(CONTENT_X - 10, CONTENT_Y - 8, PANEL_W - 40, 74, 48, 38, 20, 255);
    const SDL_Rect band = {CONTENT_X - 10, CONTENT_Y - 8, PANEL_W - 40, 74};
    SDL_SetRenderDrawColor(sdl_renderer, 199, 179, 119, 255);
    SDL_RenderDrawRect(sdl_renderer, &band);
    Options__Text(options, options->text_label, "Settings for", CONTENT_X, CONTENT_Y + 10, 0xFF, 0xE8, 0x90);
}

void OptionsPanel_Draw(OptionsPanel *options) {
    Options__Fill(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, 7, 6, 255);
    Options__Frame(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    Label_Draw(options->title_label, 400, PANEL_Y + 24);
    TabBar_Draw(options->tabs, CONTENT_X, TABS_Y, PANEL_W - 60);

    const OptionsTab tab = (OptionsTab)TabBar_GetSelected(options->tabs);
    switch (tab) {
    case TAB_VIDEO:
        Options__Name(options, "Window size", CONTENT_Y);
        Dropdown_Draw(options->window_size);
        Options__Name(options, "Display", CONTENT_Y + ROW_H);
        Checkbox_Draw(options->fullscreen);
        Options__Name(options, "Scaling", CONTENT_Y + (2 * ROW_H));
        Dropdown_Draw(options->scale_quality);
        Options__Describe(options, "How the 800x600 picture is stretched to the window.", CONTENT_Y + (2 * ROW_H));
        Options__Describe(options, "The game keeps its 4:3 shape: wider windows get black bars.", CONTENT_Y);
        Options__Name(options, "Frame rate", CONTENT_Y + (3 * ROW_H));
        Dropdown_Draw(options->frame_mode);
        Slider_Draw(options->frame_slider);
        {
            const int step = Slider_GetValue(options->frame_slider);
            char      text[96];
            const int rate = Options__FrameRate(options);
            snprintf(text, sizeof(text), "%s%s",
                     Dropdown_GetSelected(options->frame_mode) == 2 ? frame_step_names[step] : "",
                     Dropdown_GetSelected(options->frame_mode) == 2 ? ": " : "");
            if (rate == 0) {
                snprintf(text + strlen(text), sizeof(text) - strlen(text), "no limit");
            } else {
                snprintf(text + strlen(text), sizeof(text) - strlen(text), "%d a second", rate);
            }
            Options__Text(options, options->small_label, text, CONTENT_X + LABEL_W + 316, CONTENT_Y + (4 * ROW_H) + 14,
                          0xFF, 0xFF, 0xFF);
            Options__Describe(options,
                              "How smooth it looks; the game runs at the same speed whatever you pick. Multiplayer "
                              "never goes above 60.",
                              CONTENT_Y + (3 * ROW_H));
        }
        for (int i = 0; i < PICTURE_COUNT; i++) {
            const int y = CONTENT_Y + ((5 + i) * ROW_H);
            Options__Name(options, picture_names[i], y);
            Slider_Draw(options->picture[i]);
        }
        Options__Describe(options,
                          "50 is the picture as the game made it. Changes show straight away, also while playing.",
                          CONTENT_Y + (7 * ROW_H) + 22);
        Options__Name(options, "Control panel", CONTENT_Y + (8 * ROW_H) + 20);
        Dropdown_Draw(options->control_panel);
        break;
    case TAB_AUDIO:
        for (int v = 0; v < VOLUME_COUNT; v++) {
            Options__Name(options, volume_names[v], CONTENT_Y + (v * ROW_H) - 2);
            Slider_Draw(options->volumes[v]);
        }
        Options__Describe(
            options,
            "Drag a slider or type a number from 0 to 100; you hear the change right away. Speech is for NPCs and "
            "quests, once the engine plays them.",
            CONTENT_Y + (VOLUME_COUNT * ROW_H) - 20);
        break;
    case TAB_GAMEPLAY: {
        Options__ClassBand(options);
        Dropdown_Draw(options->profile_dropdown);
        if (options->profile != 0) {
            Checkbox_Draw(options->own_profile);
        }
        Options__Describe(options,
                          options->profile == 0 ? "Every class uses these, unless it has its own."
                          : Settings_HasOwnGameplay(options->profile)
                              ? "This class has its own settings."
                              : "This class uses the settings for all classes. Tick the box to give it its own.",
                          CONTENT_Y + 28);
        Options__Name(options, "Type of movement", CONTENT_Y + (2 * ROW_H));
        Dropdown_Draw(options->movement);
        Options__Describe(options, movement_help[Dropdown_GetSelected(options->movement)], CONTENT_Y + (2 * ROW_H));
        Options__Name(options, "View", CONTENT_Y + (4 * ROW_H));
        Checkbox_Draw(options->see_through);
        Options__Name(options, "Entrances", CONTENT_Y + (3 * ROW_H) + 4);
        Checkbox_Draw(options->confirm_entrances);
        Options__Describe(options,
                          "Walls, trees, tents and roofs between you and the camera fade, so you can see "
                          "where you're walking.",
                          CONTENT_Y + (4 * ROW_H));
        Options__Name(options, "Walking", CONTENT_Y + (5 * ROW_H) + 16);
        Checkbox_Draw(options->even_speed);
        Options__Describe(options,
                          "In the game you cover the same ground in every direction, which on the slanted view looks "
                          "twice as fast across as up and down. Tick it to keep the speed on screen the same instead.",
                          CONTENT_Y + (5 * ROW_H) + 20);
        Options__Name(options, "Worn gear", CONTENT_Y + (7 * ROW_H));
        Dropdown_Draw(options->worn_pickup);
        Options__Name(options, "Inventory pages", CONTENT_Y + (8 * ROW_H) + 2);
        Slider_Draw(options->inventory_pages);
        Options__Text(options, options->text_label, "Stash", CONTENT_X + LABEL_W + 132, CONTENT_Y + (8 * ROW_H) + 12,
                      0xC7, 0xB3, 0x77);
        Slider_Draw(options->stash_pages);
        Options__Text(options, options->text_label, "Shared", CONTENT_X + LABEL_W + 312, CONTENT_Y + (8 * ROW_H) + 12,
                      0xC7, 0xB3, 0x77);
        Slider_Draw(options->shared_pages);
        Dropdown_DrawList(options->worn_pickup);
        break;
    }
    case TAB_MULTIPLAYER:
        Options__Name(options, "Other players", CONTENT_Y);
        Checkbox_Draw(options->show_names);
        Checkbox_Draw(options->show_circles);
        Options__Name(options, "Your colour", CONTENT_Y + (2 * ROW_H));
        Options__Describe(options, "The others see your name and circle in it (from the next game you join or host).",
                          CONTENT_Y + (2 * ROW_H));
        Options__Name(options, "Lobby server", CONTENT_Y + (4 * ROW_H) - 6);
        TextBox_Draw(options->lobby);
        Options__Describe(options,
                          "The list of online games (Multiplayer > Browse > Online): the address of an abyss-lobby "
                          "server, \"host\" or \"host:port\" (port 4002 if you leave it out).",
                          CONTENT_Y + (4 * ROW_H) - 6);
        Dropdown_Draw(options->player_color); // last: its list opens over the rest
        break;
    case TAB_DEBUG: {
        // The developer tools, the same for every class
        Options__Name(options, "Map Editor auto-save", DEBUG_Y);
        Checkbox_Draw(options->keep_changes);
        Checkbox_Draw(options->collision_in_game);
        Checkbox_Draw(options->confirm_new_seed);
        Checkbox_Draw(options->free_roam_in_game);
        Options__Name(options, "Debug", DEBUG_Y + 50);
        Options__Describe(options,
                          "For the Map Editor and testing; the same for every class. A map never saved auto-saves "
                          "to the temporary file, which the editor offers back if it closed with changes.",
                          DEBUG_Y + 144);
        Dropdown_Draw(options->autosave); // last: its list opens over the rest
        break;
    }
    case TAB_CONTROLS:
        Options__ClassBand(options);
        Dropdown_Draw(options->profile_dropdown);
        if (options->profile != 0) {
            Checkbox_Draw(options->own_profile);
        }
        if (options->profile == 0 || Controls_HasOwnProfile(options->profile)) {
            Button_Draw(options->export_button);
            Button_Draw(options->presets_button);
            Button_Draw(options->mouse_button);
            Options__Text(options, options->text_label, "Copy to:", CONTENT_X + 376, CONTROLS_BTN_Y + 17, 0xC7, 0xB3,
                          0x77);
            Dropdown_Draw(options->copy_to);
            Button_Draw(options->copy_button);
            Options__DrawControls(options);
        } else {
            Options__Text(options, options->text_label,
                          "This class uses the controls for all classes. Tick the box to give it its own.", CONTENT_X,
                          TABLE_Y + 20, 0xA0, 0xA0, 0xA0);
        }
        Dropdown_Draw(options->group_filter);
        break;
    default:
        break;
    }

    Button_Draw(options->cancel_button);
    Button_Draw(options->defaults_button);
    Button_Draw(options->ok_button);
    if (SDL_GetTicks64() < options->message_until) {
        Label_SetAlignment(options->text_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Options__Text(options, options->text_label, options->message, 400, PANEL_Y + PANEL_H - 14, 0xFF, 0xE8, 0x60);
        Label_SetAlignment(options->text_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    }

    // Open lists on top
    Dropdown_DrawList(options->window_size);
    Dropdown_DrawList(options->scale_quality);
    Dropdown_DrawList(options->frame_mode);
    Dropdown_DrawList(options->control_panel);
    Dropdown_DrawList(options->player_color);
    Dropdown_DrawList(options->profile_dropdown);
    Dropdown_DrawList(options->movement);
    Dropdown_DrawList(options->group_filter);
    Dropdown_DrawList(options->autosave);
    Dropdown_DrawList(options->copy_to);

    if (options->dialog) {
        Options__DrawDialog(options);
    }
    if (options->presets) {
        Options__DrawPresets(options);
    }
    if (options->mouse) {
        Options__DrawMouse(options);
    }
}

bool OptionsPanel_Update(OptionsPanel *panel) {
    if (!panel->closed) {
        OptionsPanel__Update(panel);
    }
    return !panel->closed;
}

// ---------------------------------------------------------------------------------------------------------------------
// The Options scene (from the main menu): the panel over the main menu's picture

typedef struct OptionsScene {
    Sprite       *background;
    OptionsPanel *panel;
} OptionsScene;

DEFINE_SCENE_CALLBACKS(Options);

void *Options_Create(void) {
    Cursor_SetVisible(true);
    OptionsScene *scene = calloc(1, sizeof(OptionsScene));
    FAIL_IF_NULL(scene);
    scene->background = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    scene->panel      = OptionsPanel_Create();
    return scene;
}

bool options_standalone = false;

void Options_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    OptionsScene *scene = scene_ref;
    if (!OptionsPanel_Update(scene->panel)) {
        if (options_standalone) {
            running = false; // AbyssEngine --settings: closing Options closes the program
        } else {
            Scene_Set(SCENE_REF(MainMenu));
        }
    }
}

void Options_Render(void *scene_ref) {
    OptionsScene *scene = scene_ref;
    Sprite_DrawMulti(scene->background, 0, 0, 0, 4, 3);
    OptionsPanel_Draw(scene->panel);
}

void Options_Free(void **scene_ref) {
    OptionsScene *scene = *scene_ref;
    Sprite_Destroy(&scene->background);
    OptionsPanel_Destroy(&scene->panel);
    free(scene);
    *scene_ref = NULL;
}
