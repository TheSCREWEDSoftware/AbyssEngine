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
#include "../game/Controls.h"
#include "../game/Settings.h"
#include "../managers/AudioManager.h"
#include "../managers/InputManager.h"
#include "SceneMainMenu.h"
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
#define TABLE_Y         182
#define TABLE_ROW_H     18
#define TABLE_ROWS      15
#define TABLE_KEYS_X    330
#define TABLE_W         660
#define TOOLTIP_Y       (TABLE_Y + (TABLE_ROWS * TABLE_ROW_H) + 8)
#define CONTROLS_BTN_Y  (TOOLTIP_Y + 26)
#define DOUBLE_CLICK_MS 400

// Configure dialog
#define DIALOG_X 90
#define DIALOG_Y 70
#define DIALOG_W 620
#define DIALOG_H 450

typedef enum OptionsTab { TAB_VIDEO, TAB_AUDIO, TAB_GAMEPLAY, TAB_CONTROLS, TAB_COUNT } OptionsTab;

static const char *tab_names[TAB_COUNT] = {"Video", "Audio", "Gameplay", "Controls"};

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

#define VOLUME_COUNT 4
static const char *volume_names[VOLUME_COUNT] = {"Master", "Music", "Sound effects", "Interface"};

static const char *movement_choices[MOVEMENT_TYPE_COUNT] = {
    "Classic: click to walk and attack",
    "Modern: keys walk, mouse attacks",
    "Combined: both",
};
static const char *movement_help[MOVEMENT_TYPE_COUNT] = {
    "Like Diablo II: click where to walk, click a monster to attack it, click a chest to open it.",
    "Like Diablo III / IV: the movement keys (or a gamepad stick) walk; clicks and the attack key hit what's in reach.",
    "Movement keys and clicks both walk; clicks and the attack key attack.",
};

// Group colours: the controls table's legend and the keyboard diagram (ArmA / the game's guide style)
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

// Inputs you can't press while the dialog has the mouse, or wouldn't think of: pick them from a list
static const char *extra_inputs[] = {
    "Mouse Left",
    "Mouse Right",
    "Mouse Middle",
    "Mouse 4",
    "Mouse 5",
    "Wheel Up",
    "Wheel Down",
    "Shift+Mouse Left",
    "Ctrl+Mouse Left",
    "Alt+Mouse Left",
    "Shift+Mouse Right",
    "Ctrl+Mouse Right",
    "Ctrl+Wheel Up",
    "Ctrl+Wheel Down",
    "2x Mouse Left",
    "2x Mouse Right",
    "Pad A",
    "Pad B",
    "Pad X",
    "Pad Y",
    "Pad Left Shoulder",
    "Pad Right Shoulder",
    "Pad Left Trigger",
    "Pad Right Trigger",
    "Pad Back",
    "Pad Start",
    "Pad Up",
    "Pad Down",
    "Pad Left",
    "Pad Right",
    "Pad Left Stick Up",
    "Pad Left Stick Down",
    "Pad Left Stick Left",
    "Pad Left Stick Right",
    "Pad Right Stick Up",
    "Pad Right Stick Down",
    "Pad Right Stick Left",
    "Pad Right Stick Right",
    "Pad Left Stick",
    "Pad Right Stick",
};
#define EXTRA_INPUT_COUNT (int)(sizeof(extra_inputs) / sizeof(extra_inputs[0]))

typedef struct SceneOptions {
    Sprite  *background;
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

    // Audio
    Slider  *volumes[VOLUME_COUNT];
    TextBox *volume_boxes[VOLUME_COUNT];
    float    volumes_before[VOLUME_COUNT]; // to put back on Cancel

    // Gameplay and Controls: whose settings (0 = all classes)
    int       profile;
    Dropdown *profile_dropdown;
    Checkbox *own_profile;
    Dropdown *movement;
    Checkbox *see_through;

    // Controls
    Dropdown     *group_filter; // "Show:"
    ControlAction rows[ACTION_COUNT];
    int           row_count;
    int           scroll;
    int           hovered_row; // -1 = none
    int           last_click_row;
    uint64_t      last_click_time;
    bool          prev_left;
    bool          keyboard_view;
    Button       *keyboard_button;
    Button       *export_button;

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
    int             pending_modifier; // a modifier key pressed alone: bound when it's let go, unless a key follows
    uint64_t        last_bind_time;
    ControlInput    last_bound;
} SceneOptions;

DEFINE_SCENE_CALLBACKS(Options);

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

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
           Dropdown_IsOpen(options->group_filter);
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
}

static void Options__SetVolumeBox(SceneOptions *options, const int v) {
    char text[8];
    snprintf(text, sizeof(text), "%d", Slider_GetValue(options->volumes[v]));
    TextBox_SetText(options->volume_boxes[v], text);
}

static void Options__ApplyVolumes(const SceneOptions *options) {
    static const enum AudioSetVolumeType types[VOLUME_COUNT] = {
        AUDIO_SET_VOLUME_TYPE_MASTER, AUDIO_SET_VOLUME_TYPE_MUSIC, AUDIO_SET_VOLUME_TYPE_SFX, AUDIO_SET_VOLUME_TYPE_UI};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        AudioManager_SetVolume(types[v], (float)Slider_GetValue(options->volumes[v]) / 100.0f);
    }
}

static void Options__LoadGameplay(SceneOptions *options) {
    const GameplaySettings *settings = Settings_GetGameplay(options->profile);
    Dropdown_SetSelected(options->movement, settings->movement);
    Checkbox_SetChecked(options->see_through, settings->see_through);
    char text[64];
    snprintf(text, sizeof(text), "Own settings for the %s", Settings_ProfileName(options->profile));
    Checkbox_Destroy(&options->own_profile);
    options->own_profile = Checkbox_Create(CONTENT_X + LABEL_W + 260, CONTENT_Y + 3, text);
    Checkbox_SetChecked(options->own_profile, options->profile == 0 || (Controls_HasOwnProfile(options->profile) ||
                                                                        Settings_HasOwnGameplay(options->profile)));
    Checkbox_SetEnabled(options->own_profile, options->profile != 0);
}

// The actions the "Show:" filter lets through
static void Options__BuildRows(SceneOptions *options) {
    const int group    = Dropdown_GetSelected(options->group_filter) - 1; // 0 = all
    options->row_count = 0;
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (group < 0 || (int)Controls_ActionGroup((ControlAction)a) == group) {
            options->rows[options->row_count++] = (ControlAction)a;
        }
    }
    options->scroll = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Create / free

void *Options_Create(void) {
    Cursor_SetVisible(true);
    SceneOptions *options = calloc(1, sizeof(SceneOptions));
    FAIL_IF_NULL(options);
    options->background     = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    options->tabs           = TabBar_Create(tab_names, TAB_COUNT, TAB_VIDEO);
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
    Options__LoadVideo(options);

    // Audio
    const float volumes[VOLUME_COUNT] = {AbyssConfiguration_GetMasterVolume(), AbyssConfiguration_GetMusicVolume(),
                                         AbyssConfiguration_GetSfxVolume(), AbyssConfiguration_GetUiVolume()};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        options->volumes_before[v] = volumes[v];
        options->volumes[v]        = Slider_Create(CONTENT_X + LABEL_W, CONTENT_Y + (v * ROW_H), 300, 0, 100);
        Slider_SetValue(options->volumes[v], (int)((volumes[v] * 100.0f) + 0.5f));
        options->volume_boxes[v] =
            TextBox_Create(FONTS_FONT16, CONTENT_X + LABEL_W + 320, CONTENT_Y + (v * ROW_H) - 3, 56, 22, "0-100");
        Options__SetVolumeBox(options, v);
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
    Options__LoadGameplay(options);

    // Controls
    const char *groups[GROUP_COUNT + 1];
    groups[0] = "Everything";
    for (int g = 0; g < GROUP_COUNT; g++) {
        groups[g + 1] = Controls_GroupName((ControlGroup)g);
    }
    options->group_filter = Dropdown_Create(CONTENT_X + 470, CONTENT_Y + 26, 220, groups, GROUP_COUNT + 1);
    Options__BuildRows(options);
    options->keyboard_button = Button_Create(BUTTON_TYPE_MEDIUM, "Keyboard");
    options->export_button   = Button_Create(BUTTON_TYPE_MEDIUM, "Export");
    Button_SetPosition(options->keyboard_button, CONTENT_X, CONTROLS_BTN_Y);
    Button_SetPosition(options->export_button, CONTENT_X + 140, CONTROLS_BTN_Y);

    // Configure dialog
    options->extras_list = ListBox_Create(FONTS_FONT16, DIALOG_X + 330, DIALOG_Y + 110, 270, 14, 20);
    ListBox_SetItems(options->extras_list, extra_inputs, EXTRA_INPUT_COUNT);
    ListBox_SetSelectedIndex(options->extras_list, -1);
    options->previous_button      = Button_Create(BUTTON_TYPE_CANCEL, "Previous");
    options->next_button          = Button_Create(BUTTON_TYPE_CANCEL, "Next");
    options->default_button       = Button_Create(BUTTON_TYPE_CANCEL, "Default");
    options->delete_button        = Button_Create(BUTTON_TYPE_CANCEL, "Delete");
    options->undo_button          = Button_Create(BUTTON_TYPE_CANCEL, "Undo");
    options->dialog_cancel_button = Button_Create(BUTTON_TYPE_CANCEL, "Cancel");
    options->dialog_ok_button     = Button_Create(BUTTON_TYPE_CANCEL, "OK");
    Button_SetPosition(options->previous_button, DIALOG_X + 16, DIALOG_Y + 42);
    Button_SetPosition(options->next_button, DIALOG_X + DIALOG_W - 112, DIALOG_Y + 42);
    Button_SetPosition(options->default_button, DIALOG_X + 16, DIALOG_Y + 360);
    Button_SetPosition(options->delete_button, DIALOG_X + 116, DIALOG_Y + 360);
    Button_SetPosition(options->undo_button, DIALOG_X + 216, DIALOG_Y + 360);
    Button_SetPosition(options->dialog_cancel_button, DIALOG_X + 16, DIALOG_Y + DIALOG_H - 44);
    Button_SetPosition(options->dialog_ok_button, DIALOG_X + DIALOG_W - 112, DIALOG_Y + DIALOG_H - 44);
    options->pending_modifier = -1;
    return options;
}

void Options_Free(void **scene_ref) {
    SceneOptions *options = *scene_ref;
    Sprite_Destroy(&options->background);
    TabBar_Destroy(&options->tabs);
    Label_Destroy(&options->title_label);
    Label_Destroy(&options->text_label);
    Label_Destroy(&options->small_label);
    Button *buttons[] = {options->ok_button,       options->cancel_button,        options->defaults_button,
                         options->keyboard_button, options->export_button,        options->previous_button,
                         options->next_button,     options->default_button,       options->delete_button,
                         options->undo_button,     options->dialog_cancel_button, options->dialog_ok_button};
    for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
        Button_Destroy(&buttons[b]);
    }
    Dropdown_Destroy(&options->window_size);
    Dropdown_Destroy(&options->scale_quality);
    Dropdown_Destroy(&options->profile_dropdown);
    Dropdown_Destroy(&options->movement);
    Dropdown_Destroy(&options->group_filter);
    Checkbox_Destroy(&options->fullscreen);
    Checkbox_Destroy(&options->own_profile);
    Checkbox_Destroy(&options->see_through);
    for (int v = 0; v < VOLUME_COUNT; v++) {
        Slider_Destroy(&options->volumes[v]);
        TextBox_Destroy(&options->volume_boxes[v]);
    }
    ListBox_Destroy(&options->extras_list);
    free(options);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// OK / Cancel / Defaults

static void Options__Leave(void) { Scene_Set(SCENE_REF(MainMenu)); }

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
    // Audio
    AbyssConfiguration_SetVolumes(
        (float)Slider_GetValue(options->volumes[0]) / 100.0f, (float)Slider_GetValue(options->volumes[1]) / 100.0f,
        (float)Slider_GetValue(options->volumes[2]) / 100.0f, (float)Slider_GetValue(options->volumes[3]) / 100.0f);
    AbyssConfiguration_Save();
    // Gameplay and controls
    Settings_Save();
}

static void Options__Cancel(SceneOptions *options) {
    static const enum AudioSetVolumeType types[VOLUME_COUNT] = {
        AUDIO_SET_VOLUME_TYPE_MASTER, AUDIO_SET_VOLUME_TYPE_MUSIC, AUDIO_SET_VOLUME_TYPE_SFX, AUDIO_SET_VOLUME_TYPE_UI};
    for (int v = 0; v < VOLUME_COUNT; v++) {
        AudioManager_SetVolume(types[v], options->volumes_before[v]);
    }
    Settings_Load(); // gameplay and controls as they were saved
}

static void Options__Defaults(SceneOptions *options, const OptionsTab tab) {
    switch (tab) {
    case TAB_VIDEO:
        Dropdown_SetSelected(options->window_size, 0);
        Checkbox_SetChecked(options->fullscreen, false);
        Dropdown_SetSelected(options->scale_quality, 0);
        break;
    case TAB_AUDIO: {
        static const int defaults[VOLUME_COUNT] = {80, 100, 100, 100};
        for (int v = 0; v < VOLUME_COUNT; v++) {
            Slider_SetValue(options->volumes[v], defaults[v]);
            Options__SetVolumeBox(options, v);
        }
        Options__ApplyVolumes(options);
        break;
    }
    case TAB_GAMEPLAY: {
        const GameplaySettings defaults = {MOVEMENT_CLASSIC, true};
        Settings_SetGameplay(options->profile, &defaults);
        Options__LoadGameplay(options);
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
// Controls: table, dialog, keyboard, export

static void Options__OpenDialog(SceneOptions *options, const int row) {
    options->dialog           = true;
    options->dialog_row       = row;
    options->dialog_selected  = -1;
    options->dialog_bindings  = *Controls_GetBindings(options->profile, options->rows[row]);
    options->dialog_original  = options->dialog_bindings;
    options->pending_modifier = -1;
    InputManager_ClearPresses();
    ListBox_SetSelectedIndex(options->extras_list, -1);
}

static void Options__CommitDialog(SceneOptions *options) {
    Controls_SetBindings(options->profile, options->rows[options->dialog_row], &options->dialog_bindings);
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
        Options__OpenDialog(options, (options->dialog_row + (next ? 1 : options->row_count - 1)) % options->row_count);
    }
    if (Button_Update(options->default_button)) {
        Controls_GetDefaults(options->rows[options->dialog_row], &options->dialog_bindings);
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

// The keyboard, row by row: key names (SDL's), widths in quarter keys (4 = one key); "" = a gap
static const struct {
    const char *name;
    const char *label;
    int         width;
} keyboard[6][20] = {
    {{"Escape", "Esc", 4}, {"", "", 2},       {"F1", "F1", 4},
     {"F2", "F2", 4},      {"F3", "F3", 4},   {"F4", "F4", 4},
     {"", "", 2},          {"F5", "F5", 4},   {"F6", "F6", 4},
     {"F7", "F7", 4},      {"F8", "F8", 4},   {"", "", 2},
     {"F9", "F9", 4},      {"F10", "F10", 4}, {"F11", "F11", 4},
     {"F12", "F12", 4},    {"", "", 2},       {"PrintScreen", "PrtSc", 4},
     {"Home", "Home", 4},  {NULL, NULL, 0}},
    {{"`", "`", 4},
     {"1", "1", 4},
     {"2", "2", 4},
     {"3", "3", 4},
     {"4", "4", 4},
     {"5", "5", 4},
     {"6", "6", 4},
     {"7", "7", 4},
     {"8", "8", 4},
     {"9", "9", 4},
     {"0", "0", 4},
     {"-", "-", 4},
     {"=", "=", 4},
     {"Backspace", "Back", 8},
     {"", "", 2},
     {"Insert", "Ins", 4},
     {"PageUp", "PgUp", 4},
     {NULL, NULL, 0}},
    {{"Tab", "Tab", 6},
     {"Q", "Q", 4},
     {"W", "W", 4},
     {"E", "E", 4},
     {"R", "R", 4},
     {"T", "T", 4},
     {"Y", "Y", 4},
     {"U", "U", 4},
     {"I", "I", 4},
     {"O", "O", 4},
     {"P", "P", 4},
     {"[", "[", 4},
     {"]", "]", 4},
     {"\\", "\\", 6},
     {"", "", 2},
     {"Delete", "Del", 4},
     {"PageDown", "PgDn", 4},
     {NULL, NULL, 0}},
    {{"CapsLock", "Caps", 7},
     {"A", "A", 4},
     {"S", "S", 4},
     {"D", "D", 4},
     {"F", "F", 4},
     {"G", "G", 4},
     {"H", "H", 4},
     {"J", "J", 4},
     {"K", "K", 4},
     {"L", "L", 4},
     {";", ";", 4},
     {"'", "'", 4},
     {"Return", "Enter", 9},
     {"", "", 2},
     {"End", "End", 4},
     {NULL, NULL, 0}},
    {{"Left Shift", "Shift", 9},
     {"Z", "Z", 4},
     {"X", "X", 4},
     {"C", "C", 4},
     {"V", "V", 4},
     {"B", "B", 4},
     {"N", "N", 4},
     {"M", "M", 4},
     {",", ",", 4},
     {".", ".", 4},
     {"/", "/", 4},
     {"Right Shift", "Shift", 11},
     {"", "", 6},
     {"Up", "Up", 4},
     {NULL, NULL, 0}},
    {{"Left Ctrl", "Ctrl", 6},
     {"Left GUI", "Win", 5},
     {"Left Alt", "Alt", 5},
     {"Space", "Space", 25},
     {"Right Alt", "Alt", 5},
     {"Right Ctrl", "Ctrl", 6},
     {"", "", 2},
     {"Left", "Left", 4},
     {"Down", "Down", 4},
     {"Right", "Right", 4},
     {NULL, NULL, 0}},
};

// The first action (in list order) a plain key is on, and whether it clashes
static int Options__KeyAction(const SceneOptions *options, const char *key_name, ControlConflict *conflict) {
    const SDL_Scancode scancode = SDL_GetScancodeFromName(key_name);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return -1;
    }
    const ControlInput input = {INPUT_KEY, scancode, 0, false};
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(options->profile, (ControlAction)a);
        for (int b = 0; b < list->count; b++) {
            if (Controls_SameInput(&list->inputs[b], &input)) {
                *conflict = Controls_Conflict(options->profile, (ControlAction)a, &input);
                return a;
            }
        }
    }
    return -1;
}

static void Options__DrawKeyboard(SceneOptions *options) {
    const int unit = 7; // pixels per quarter key
    const int x0   = CONTENT_X;
    const int y0   = TABLE_Y;
    for (int r = 0; r < 6; r++) {
        int x = x0;
        for (int k = 0; k < 20 && keyboard[r][k].name != NULL; k++) {
            const int w = keyboard[r][k].width * unit;
            if (keyboard[r][k].name[0] == '\0') {
                x += w;
                continue;
            }
            ControlConflict conflict = CONFLICT_NONE;
            const int       action   = Options__KeyAction(options, keyboard[r][k].name, &conflict);
            const int       y        = y0 + (r * 46) + (r > 0 ? 6 : 0);
            if (action >= 0) {
                const uint8_t *c = group_colors[Controls_ActionGroup((ControlAction)action)];
                Options__Fill(x + 1, y, w - 2, 42, c[0] / 2, c[1] / 2, c[2] / 2, 230);
            } else {
                Options__Fill(x + 1, y, w - 2, 42, 30, 28, 26, 230);
            }
            const SDL_Rect frame = {x + 1, y, w - 2, 42};
            if (conflict == CONFLICT_CLASH) {
                SDL_SetRenderDrawColor(sdl_renderer, 255, 60, 50, 255);
            } else {
                SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
            }
            SDL_RenderDrawRect(sdl_renderer, &frame);
            Options__Text(options, options->small_label, keyboard[r][k].label, x + 4, y + 8, 0xFF, 0xFF, 0xFF);
            if (action >= 0) {
                // The action's name, cut to fit
                char name[32];
                snprintf(name, sizeof(name), "%s", Controls_ActionName((ControlAction)action));
                while (strlen(name) > 1 && Label_MeasureText(options->small_label, name) > w - 6) {
                    name[strlen(name) - 1] = '\0';
                }
                Options__Text(options, options->small_label, name, x + 4, y + 27, 0xE8, 0xE0, 0xC0);
            }
            x += w;
        }
    }
    // Legend
    int lx = CONTENT_X;
    for (int g = 0; g < GROUP_COUNT; g++) {
        const uint8_t *c = group_colors[g];
        Options__Fill(lx, TOOLTIP_Y + 2, 10, 10, c[0], c[1], c[2], 255);
        Options__Text(options, options->small_label, Controls_GroupName((ControlGroup)g), lx + 14, TOOLTIP_Y + 7, 0xFF,
                      0xFF, 0xFF);
        lx += 18 + Label_MeasureText(options->small_label, Controls_GroupName((ControlGroup)g)) + 10;
    }
}

// A Markdown table of every binding, next to options.ini
static void Options__Export(SceneOptions *options) {
    char path[4200];
    snprintf(path, sizeof(path), "%s/controls.md", AbyssConfiguration_GetConfigFolder());
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        Options__Message(options, "Could not write controls.md.");
        return;
    }
    fprintf(file, "# Controls (%s)\n\n| Input | Action | Group | Works | Note |\n|---|---|---|---|---|\n",
            Settings_ProfileName(options->profile));
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(options->profile, (ControlAction)a);
        for (int b = 0; b < (list->count > 0 ? list->count : 1); b++) {
            char name[64] = "(not bound)";
            if (list->count > 0) {
                Controls_InputName(&list->inputs[b], name, sizeof(name));
            }
            const ControlConflict conflict =
                list->count > 0 ? Controls_Conflict(options->profile, (ControlAction)a, &list->inputs[b])
                                : CONFLICT_NONE;
            fprintf(file, "| %s | %s | %s | %s | %s |\n", name, Controls_ActionName((ControlAction)a),
                    Controls_GroupName(Controls_ActionGroup((ControlAction)a)),
                    Controls_ActionWorks((ControlAction)a) ? "yes" : "not yet",
                    conflict == CONFLICT_CLASH    ? "clashes with another action"
                    : conflict == CONFLICT_SHARED ? "also used elsewhere"
                                                  : "");
        }
    }
    fclose(file);
    char text[160];
    snprintf(text, sizeof(text), "Saved the table to controls.md (next to abyss.ini).");
    Options__Message(options, text);
}

static void Options__UpdateControls(SceneOptions *options, const int mouse_x, const int mouse_y, const bool clicked) {
    if (Dropdown_Update(options->group_filter)) {
        Options__BuildRows(options);
    }
    if (Button_Update(options->keyboard_button)) {
        options->keyboard_view = !options->keyboard_view;
        Button_SetText(options->keyboard_button, options->keyboard_view ? "List" : "Keyboard");
    }
    if (Button_Update(options->export_button)) {
        Options__Export(options);
    }
    if (options->keyboard_view) {
        return;
    }
    const int wheel = InputManager_ConsumeMouseWheel();
    const int most  = options->row_count > TABLE_ROWS ? options->row_count - TABLE_ROWS : 0;
    options->scroll = options->scroll - (wheel * 3);
    options->scroll = options->scroll < 0 ? 0 : (options->scroll > most ? most : options->scroll);

    options->hovered_row = -1;
    if (mouse_x >= CONTENT_X && mouse_x < CONTENT_X + TABLE_W && mouse_y >= TABLE_Y &&
        mouse_y < TABLE_Y + (TABLE_ROWS * TABLE_ROW_H)) {
        const int row        = options->scroll + ((mouse_y - TABLE_Y) / TABLE_ROW_H);
        options->hovered_row = row < options->row_count ? row : -1;
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
    Options__Text(options, options->text_label, "Show:", CONTENT_X + 410, CONTENT_Y + 36, 0xC7, 0xB3, 0x77);
    if (options->keyboard_view) {
        Options__DrawKeyboard(options);
        return;
    }
    // Header
    Options__Fill(CONTENT_X, TABLE_Y - 20, TABLE_W, 18, 60, 50, 35, 230);
    Options__Text(options, options->small_label, "ACTION", CONTENT_X + 6, TABLE_Y - 11, 0xFF, 0xFF, 0xFF);
    Options__Text(options, options->small_label, "ASSIGNED INPUTS", CONTENT_X + TABLE_KEYS_X, TABLE_Y - 11, 0xFF, 0xFF,
                  0xFF);
    Options__Fill(CONTENT_X, TABLE_Y, TABLE_W, TABLE_ROWS * TABLE_ROW_H, 10, 10, 10, 200);
    for (int r = 0; r < TABLE_ROWS && options->scroll + r < options->row_count; r++) {
        const int           row    = options->scroll + r;
        const ControlAction action = options->rows[row];
        const int           y      = TABLE_Y + (r * TABLE_ROW_H);
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
                            y + (TABLE_ROW_H / 2), CONTENT_X + TABLE_W - 6, -1);
    }
    Options__Frame(CONTENT_X, TABLE_Y, TABLE_W, TABLE_ROWS * TABLE_ROW_H);
    // Scrollbar
    if (options->row_count > TABLE_ROWS) {
        const int track = TABLE_ROWS * TABLE_ROW_H;
        const int thumb = track * TABLE_ROWS / options->row_count;
        const int at    = (track - thumb) * options->scroll / (options->row_count - TABLE_ROWS);
        Options__Fill(CONTENT_X + TABLE_W - 5, TABLE_Y + at, 4, thumb, 150, 125, 70, 255);
    }
    // What the hovered action does (the tooltip), or how to use the table
    char help[200];
    if (options->hovered_row >= 0) {
        const ControlAction action = options->rows[options->hovered_row];
        const char         *text   = Controls_ActionHelp(action);
        snprintf(help, sizeof(help), "%s%s%s", text != NULL ? text : Controls_ActionName(action),
                 Controls_ActionWorks(action) ? "" : "  (* not in the engine yet)", "   - double click to change");
    } else {
        snprintf(help, sizeof(help),
                 "Double click an action (or click its inputs) to change them.  Red: clashes "
                 "with another action (the first wins).  Orange: also used elsewhere.");
    }
    Options__Text(options, options->small_label, help, CONTENT_X, TOOLTIP_Y + 7, 0xC7, 0xB3, 0x77);
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
    const ControlAction action = options->rows[options->dialog_row];
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
    Options__Text(options, options->small_label, "Or pick one:", DIALOG_X + 330, DIALOG_Y + 98, 0xC7, 0xB3, 0x77);
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

void Options_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneOptions *options = scene_ref;
    int           mouse_x;
    int           mouse_y;
    bool          left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked = left && !options->prev_left;
    options->prev_left = left;

    if (options->dialog) {
        Options__UpdateDialog(options, mouse_x, mouse_y, clicked);
        return;
    }
    InputManager_ClearPresses(); // presses are only read by the dialog

    const OptionsTab tab = (OptionsTab)TabBar_GetSelected(options->tabs);
    if (Options__AnyDropdownOpen(options)) {
        // An open list takes all the clicks
        Dropdown_Update(options->window_size);
        Dropdown_Update(options->scale_quality);
        if (Dropdown_Update(options->profile_dropdown)) {
            options->profile = Dropdown_GetSelected(options->profile_dropdown);
            Options__LoadGameplay(options);
        }
        if (Dropdown_Update(options->movement)) {
            GameplaySettings settings = *Settings_GetGameplay(options->profile);
            settings.movement         = (MovementType)Dropdown_GetSelected(options->movement);
            Settings_SetGameplay(options->profile, &settings);
        }
        if (Dropdown_Update(options->group_filter)) {
            Options__BuildRows(options);
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
        Dropdown_Update(options->scale_quality);
        break;
    case TAB_AUDIO:
        for (int v = 0; v < VOLUME_COUNT; v++) {
            if (Slider_Update(options->volumes[v])) {
                Options__SetVolumeBox(options, v);
                Options__ApplyVolumes(options);
            }
            if (TextBox_Update(options->volume_boxes[v])) {
                const int typed = atoi(TextBox_GetText(options->volume_boxes[v]));
                Slider_SetValue(options->volumes[v], typed);
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
        } else if (options->profile == 0 || Controls_HasOwnProfile(options->profile)) {
            Options__UpdateControls(options, mouse_x, mouse_y, clicked);
        } else if (clicked && mouse_y >= TABLE_Y && mouse_y < TABLE_Y + (TABLE_ROWS * TABLE_ROW_H)) {
            Options__Message(options, "Tick \"Own settings\" to give this class controls of its own.");
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
        Options__Leave();
        return;
    }
    if (Button_Update(options->cancel_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        Options__Cancel(options);
        Options__Leave();
        return;
    }
}

static void Options__Describe(SceneOptions *options, const char *text, const int y) {
    Options__Text(options, options->small_label, text, CONTENT_X + LABEL_W, y + DESCRIBE_DY + 4, 0xA0, 0xA0, 0xA0);
}

static void Options__Name(SceneOptions *options, const char *text, const int y) {
    Options__Text(options, options->text_label, text, CONTENT_X, y + 10, 0xC7, 0xB3, 0x77);
}

void Options_Render(void *scene_ref) {
    SceneOptions *options = scene_ref;
    Sprite_DrawMulti(options->background, 0, 0, 0, 4, 3);
    Options__Fill(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, 7, 6, 225);
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
        Options__Describe(options, "How the game's 800x600 picture is stretched to the window (from the next start).",
                          CONTENT_Y + (2 * ROW_H));
        Options__Describe(options, "The game keeps its 4:3 shape: wider windows get black bars.", CONTENT_Y);
        break;
    case TAB_AUDIO:
        for (int v = 0; v < VOLUME_COUNT; v++) {
            Options__Name(options, volume_names[v], CONTENT_Y + (v * ROW_H) - 2);
            Slider_Draw(options->volumes[v]);
            TextBox_Draw(options->volume_boxes[v]);
            Options__Text(options, options->text_label, "%", CONTENT_X + LABEL_W + 382, CONTENT_Y + (v * ROW_H) + 8,
                          0xFF, 0xFF, 0xFF);
        }
        Options__Describe(options, "Drag a slider or type a number from 0 to 100. You hear the change right away.",
                          CONTENT_Y + (VOLUME_COUNT * ROW_H) - 20);
        break;
    case TAB_GAMEPLAY: {
        Options__Name(options, "Settings for", CONTENT_Y);
        Dropdown_Draw(options->profile_dropdown);
        if (options->profile != 0) {
            Checkbox_Draw(options->own_profile);
        }
        Options__Describe(options,
                          options->profile == 0 ? "Every class uses these, unless it has its own."
                          : Settings_HasOwnGameplay(options->profile)
                              ? "This class has its own settings."
                              : "This class uses the settings for all classes. Tick the box to give it its own.",
                          CONTENT_Y);
        Options__Name(options, "Type of movement", CONTENT_Y + (2 * ROW_H));
        Dropdown_Draw(options->movement);
        Options__Describe(options, movement_help[Dropdown_GetSelected(options->movement)], CONTENT_Y + (2 * ROW_H));
        Options__Name(options, "View", CONTENT_Y + (4 * ROW_H));
        Checkbox_Draw(options->see_through);
        Options__Describe(options,
                          "Walls, trees, tents and roofs between you and the camera fade, so you can see "
                          "where you're walking.",
                          CONTENT_Y + (4 * ROW_H));
        break;
    }
    case TAB_CONTROLS:
        Options__Name(options, "Settings for", CONTENT_Y);
        Dropdown_Draw(options->profile_dropdown);
        if (options->profile != 0) {
            Checkbox_Draw(options->own_profile);
        }
        if (options->profile == 0 || Controls_HasOwnProfile(options->profile)) {
            Button_Draw(options->keyboard_button);
            Button_Draw(options->export_button);
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
    Dropdown_DrawList(options->profile_dropdown);
    Dropdown_DrawList(options->movement);
    Dropdown_DrawList(options->group_filter);

    if (options->dialog) {
        Options__DrawDialog(options);
    }
}
