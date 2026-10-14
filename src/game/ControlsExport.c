#include "ControlsExport.h"
#include "Controls.h"
#include "Settings.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The keyboard, row by row: key names (SDL's), labels, widths in quarter keys (4 = one key); "" = a gap
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

static const char *group_colors[GROUP_COUNT] = {"#9696dc", "#e68c8c", "#e6c878", "#96dca0",
                                                "#78c8dc", "#c8a0e6", "#c8c8c8", "#f0aa5a"};

static const char *ControlsExport__Conflict(const ControlConflict conflict) {
    return conflict == CONFLICT_CLASH    ? "clashes with another action (the first in the list wins)"
           : conflict == CONFLICT_SHARED ? "also on an action that's never active at the same time"
                                         : "";
}

// ---------------------------------------------------------------------------------------------------------------------
// Dark theme: the light picture with every colour swapped for its dark counterpart

static const char *dark_colors[][2] = {
    {"#f4f4f4", "#1b1b1f"}, // background
    {"#111111", "#e6e6e6"}, // text
    {"#ffffff", "#2c2c31"}, // a key with nothing on it
    {"#606060", "#7a7a80"}, // key outline
    {"#555555", "#a0a0a8"}, // not in the engine yet
    {"#909090", "#7c7c84"}, // (free)
    {"#707070", "#8c8c94"}, // leader lines
    {"#505050", "#8a8a90"}, // pad outline
    {"#404040", "#18181b"}, // D-pad and face button outlines
    {"#d6d6d6", "#3a3a40"}, // pad body
    {"#bdbdbd", "#2c2c31"}, // bumpers
    {"#9a9a9a", "#55555c"}, // sticks
    {"#7a7a7a", "#44444a"},
    {"#8a8a8a", "#5c5c64"}, // the four buttons
    {"#f0f0f0", "#d8d8dc"}, // their arrows
    {"#6a6a6a", "#202024"}, // D-pad, Back / Guide / Start
    {"#9696dc", "#4b4b94"}, // the groups, deeper so light text reads on them
    {"#e68c8c", "#8f3d3d"},
    {"#e6c878", "#8a7230"},
    {"#96dca0", "#3b7a47"},
    {"#78c8dc", "#2e6e80"},
    {"#c8a0e6", "#6c4b8c"},
    {"#c8c8c8", "#5a5a60"},
    {"#f0aa5a", "#95602a"},
    {"#e02020", "#ff4a4a"}, // clashes
    {"#d01010", "#ff5a5a"},
};

static bool ControlsExport__MakeDark(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return false;
    }
    fclose(file);
    text[size] = '\0';
    for (char *c = strchr(text, '#'); c != NULL; c = strchr(c + 1, '#')) {
        for (size_t k = 0; k < sizeof(dark_colors) / sizeof(dark_colors[0]); k++) {
            if (strncmp(c, dark_colors[k][0], 7) == 0) {
                memcpy(c, dark_colors[k][1], 7);
                break;
            }
        }
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        free(text);
        return false;
    }
    fwrite(text, 1, (size_t)size, file);
    fclose(file);
    free(text);
    return true;
}

// An action's inputs as Markdown code, by kind: [0] mouse and wheel, [1] keyboard, [2] controller; and a note on the
// first clash
static void ControlsExport__Columns(const int profile, const ControlAction action, char columns[3][384],
                                    char notes[256]) {
    const ControlBindings *list = Controls_GetBindings(profile, action);
    columns[0][0] = columns[1][0] = columns[2][0] = '\0';
    notes[0]                                      = '\0';
    for (int b = 0; b < list->count; b++) {
        const ControlInput *input = &list->inputs[b];
        char                name[64];
        Controls_InputName(input, name, sizeof(name));
        const int    column = input->kind == INPUT_KEY                                             ? 1
                              : (input->kind == INPUT_PAD_BUTTON || input->kind == INPUT_PAD_AXIS) ? 2
                                                                                                   : 0;
        const size_t used   = strlen(columns[column]);
        // A backtick key needs a longer fence to show as code
        const bool tick = strchr(name, '`') != NULL;
        snprintf(columns[column] + used, 384 - used, "%s%s%s%s", used > 0 ? ", " : "", tick ? "`` " : "`", name,
                 tick ? " ``" : "`");
        const ControlConflict conflict = Controls_Conflict(profile, action, input);
        if (conflict != CONFLICT_NONE && notes[0] == '\0') {
            snprintf(notes, 256, "`%s` %s", name, ControlsExport__Conflict(conflict));
        }
    }
}

void ControlsExport_ModeTable(FILE *file, const int profile, const unsigned contexts) {
    fprintf(file, "| Action | Mouse | Keyboard | Controller | What it does |\n|---|---|---|---|---|\n");
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (!Controls_ActionWorks((ControlAction)a) || !(Controls_ActionContexts((ControlAction)a) & contexts)) {
            continue;
        }
        char columns[3][384];
        char notes[256];
        ControlsExport__Columns(profile, (ControlAction)a, columns, notes);
        fprintf(file, "| %s | %s | %s | %s | %s |\n", Controls_ActionName((ControlAction)a),
                columns[0][0] != '\0' ? columns[0] : "-", columns[1][0] != '\0' ? columns[1] : "-",
                columns[2][0] != '\0' ? columns[2] : "-",
                Controls_ActionHelp((ControlAction)a) != NULL ? Controls_ActionHelp((ControlAction)a) : "-");
    }
}

bool ControlsExport_Markdown(const int profile, const char *path, const bool pictures) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "# Abyss Engine controls (%s)\n\n", Settings_ProfileName(profile));
    if (pictures) {
        // Folded away, each in a light and a dark version
        static const char *shown[4][2] = {
            {"Keyboard Layout (Light Theme)", "controls-keyboard.svg"     },
            {"Keyboard Layout (Dark Theme)",  "controls-keyboard-dark.svg"},
            {"Gamepad Layout (Light Theme)",  "controls-gamepad.svg"      },
            {"Gamepad Layout (Dark Theme)",   "controls-gamepad-dark.svg" }
        };
        for (int p = 0; p < 4; p++) {
            fprintf(file, "<details>\n<summary>Show %s</summary>\n\n![%s](%s)\n\n</details>\n\n", shown[p][0],
                    shown[p][0], shown[p][1]);
        }
    }
    for (int g = 0; g < GROUP_COUNT; g++) {
        fprintf(file, "## %s\n\n| Action | Mouse | Keyboard | Controller | Note |\n|---|---|---|---|---|\n",
                Controls_GroupName((ControlGroup)g));
        char missing[1024] = ""; // the actions not in the engine yet, for the note under the table
        for (int a = 0; a < ACTION_COUNT; a++) {
            if ((int)Controls_ActionGroup((ControlAction)a) != g) {
                continue;
            }
            char columns[3][384]; // mouse (and wheel), keyboard, controller
            char notes[256];
            ControlsExport__Columns(profile, (ControlAction)a, columns, notes);
            const bool works = Controls_ActionWorks((ControlAction)a);
            if (!works) {
                const size_t used = strlen(missing);
                snprintf(missing + used, sizeof(missing) - used, "%s%s", used > 0 ? ", " : "",
                         Controls_ActionName((ControlAction)a));
            }
            fprintf(file, "| %s%s | %s | %s | %s | %s |\n", Controls_ActionName((ControlAction)a), works ? "" : " \\*",
                    columns[0][0] != '\0' ? columns[0] : "-", columns[1][0] != '\0' ? columns[1] : "-",
                    columns[2][0] != '\0' ? columns[2] : "-", notes);
        }
        // GitHub shows this as a note box (other Markdown viewers as a quote)
        if (missing[0] != '\0') {
            fprintf(file,
                    "\n> [!NOTE]\n> \\* **Not implemented yet:** %s. Their inputs are already set (the game's "
                    "defaults) so they work as soon as the engine does them.\n",
                    missing);
        }
        fprintf(file, "\n");
    }
    fclose(file);
    return true;
}

// The actions a plain key is on (the first one picks the colour)
static int ControlsExport__KeyActions(const int profile, const char *key_name, int *actions, const int max,
                                      bool *clash) {
    *clash                      = false;
    int                count    = 0;
    const SDL_Scancode scancode = SDL_GetScancodeFromName(key_name);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return 0;
    }
    const ControlInput input = {INPUT_KEY, scancode, 0, false};
    for (int a = 0; a < ACTION_COUNT && count < max; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        for (int b = 0; b < list->count; b++) {
            if (Controls_SameInput(&list->inputs[b], &input)) {
                actions[count++] = a;
                *clash           = *clash || Controls_Conflict(profile, (ControlAction)a, &input) == CONFLICT_CLASH;
                break;
            }
        }
    }
    return count;
}

static void ControlsExport__Text(FILE *file, const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        switch (*c) {
        case '<':
            fputs("&lt;", file);
            break;
        case '>':
            fputs("&gt;", file);
            break;
        case '&':
            fputs("&amp;", file);
            break;
        default:
            fputc(*c, file);
            break;
        }
    }
}

bool ControlsExport_Svg(const int profile, const char *path, const bool dark) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    const int unit   = 20; // pixels per quarter key
    const int key_h  = 62;
    const int step   = 68;
    const int margin = 20;
    const int width  = (76 * unit) + (2 * margin);
    const int top    = 50;
    const int legend = top + (6 * step) + 20;

    // How many lines the mouse / gamepad / combinations list needs
    int extra_lines = 0;
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        for (int b = 0; b < list->count; b++) {
            const ControlInput *input = &list->inputs[b];
            if (!(input->kind == INPUT_KEY && input->mods == 0 && !input->twice)) {
                extra_lines++;
                break;
            }
        }
    }
    const int height = legend + 90 + (((extra_lines + 2) / 3) * 16);

    fprintf(file,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" "
            "font-family=\"Segoe UI, Helvetica, Arial, sans-serif\" fill=\"#111111\">\n",
            width, height, width, height);
    fprintf(file, "<rect width=\"100%%\" height=\"100%%\" fill=\"#f4f4f4\"/>\n");
    fprintf(file, "<text x=\"%d\" y=\"30\" font-size=\"20\" font-weight=\"bold\">Abyss Engine controls: %s</text>\n",
            margin, Settings_ProfileName(profile));

    for (int r = 0; r < 6; r++) {
        int x = margin;
        for (int k = 0; k < 20 && keyboard[r][k].name != NULL; k++) {
            const int w = keyboard[r][k].width * unit;
            if (keyboard[r][k].name[0] == '\0') {
                x += w;
                continue;
            }
            const int y = top + (r * step) + (r > 0 ? 10 : 0);
            int       actions[3];
            bool      clash = false;
            const int count = ControlsExport__KeyActions(profile, keyboard[r][k].name, actions, 3, &clash);
            fprintf(file,
                    "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"6\" fill=\"%s\" stroke=\"%s\" "
                    "stroke-width=\"%d\"/>\n",
                    x + 2, y, w - 4, key_h,
                    count > 0 ? group_colors[Controls_ActionGroup((ControlAction)actions[0])] : "#ffffff",
                    clash ? "#e02020" : "#606060", clash ? 3 : 1);
            fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"13\" font-weight=\"bold\">", x + 8, y + 16);
            ControlsExport__Text(file, keyboard[r][k].label);
            fprintf(file, "</text>\n");
            // Its actions, one line each, cut short to fit
            for (int a = 0; a < count; a++) {
                char      name[48];
                const int chars = (w - 12) / 5;
                snprintf(name, sizeof(name), "%s", Controls_ActionName((ControlAction)actions[a]));
                if ((int)strlen(name) > chars && chars > 2) {
                    name[chars - 1] = '\0';
                    strcat(name, ".");
                }
                fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"9.5\"%s>", x + 8, y + 31 + (a * 12),
                        Controls_ActionWorks((ControlAction)actions[a]) ? ""
                                                                        : " fill=\"#555555\" font-style=\"italic\"");
                ControlsExport__Text(file, name);
                fprintf(file, "</text>\n");
            }
            x += w;
        }
    }

    // Legend
    int lx = margin;
    for (int g = 0; g < GROUP_COUNT; g++) {
        fprintf(file, "<rect x=\"%d\" y=\"%d\" width=\"14\" height=\"14\" fill=\"%s\" stroke=\"#606060\"/>\n", lx,
                legend, group_colors[g]);
        fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"13\">%s</text>\n", lx + 20, legend + 12,
                Controls_GroupName((ControlGroup)g));
        lx += 32 + ((int)strlen(Controls_GroupName((ControlGroup)g)) * 7);
    }
    fprintf(
        file,
        "<text x=\"%d\" y=\"%d\" font-size=\"12\">Keys without Ctrl, Shift or Alt. Red border: the key clashes with "
        "another action. Grey italics: not in the engine yet.</text>\n",
        margin, legend + 36);

    // Mouse, gamepad and combinations, as a list in three columns
    fprintf(file,
            "<text x=\"%d\" y=\"%d\" font-size=\"13\" font-weight=\"bold\">Mouse, gamepad and combinations</text>\n",
            margin, legend + 62);
    int line = 0;
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        char                   text[512];
        text[0] = '\0';
        for (int b = 0; b < list->count; b++) {
            const ControlInput *input = &list->inputs[b];
            if (input->kind == INPUT_KEY && input->mods == 0 && !input->twice) {
                continue; // on the keyboard above
            }
            char name[64];
            Controls_InputName(input, name, sizeof(name));
            const size_t used = strlen(text);
            snprintf(text + used, sizeof(text) - used, "%s%s", used > 0 ? ", " : "", name);
        }
        if (text[0] == '\0') {
            continue;
        }
        fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"11\">", margin + ((line % 3) * 520),
                legend + 82 + ((line / 3) * 16));
        ControlsExport__Text(file, Controls_ActionName((ControlAction)a));
        fprintf(file, ": ");
        ControlsExport__Text(file, text);
        fprintf(file, "</text>\n");
        line++;
    }
    fprintf(file, "</svg>\n");
    fclose(file);
    return !dark || ControlsExport__MakeDark(path);
}

// ---------------------------------------------------------------------------------------------------------------------
// The gamepad diagram: a generic pad (D-pad top left, four buttons top right, both sticks low in the middle, sticks
// with a ring around a dished top). Buttons carry Windows' numbers and where they are, the same for any pad.

typedef struct PadPart {
    const char *label; // "Up", or "" for a control with one input
    int         kind;  // INPUT_PAD_BUTTON / INPUT_PAD_AXIS
    int         value; // SDL's button / our axis numbering (see Controls.c)
} PadPart;

typedef struct PadCallout {
    const char *title;
    int         side; // where the label goes: 0 left column, 1 right column, 2 under the pad, in the middle
    int         y;    // the title's baseline
    int         target_x;
    int         target_y;
    int         part_count;
    PadPart     parts[5];
} PadCallout;

// Each column is ordered so that its leader lines fan out without crossing (and miss the D-pad and buttons)
// clang-format off
static const PadCallout pad_callouts[] = {
    {"Left trigger (Button 7)",             0, 60,  410, 175, 1, {{"", INPUT_PAD_AXIS, 9}}   },
    {"Left bumper (Button 5)",              0, 110, 395, 207, 1, {{"", INPUT_PAD_BUTTON, 9}} },
    {"Select (Button 9)",                   0, 165, 505, 278, 1, {{"", INPUT_PAD_BUTTON, 4}} },
    {"D-pad",
     0,                             215,
     387,                                     285,
     4,                                               {{"Up", INPUT_PAD_BUTTON, 11},
      {"Down", INPUT_PAD_BUTTON, 12},
      {"Left", INPUT_PAD_BUTTON, 13},
      {"Right", INPUT_PAD_BUTTON, 14}}                                     },
    {"Left stick",
     0,                             315,
     446,                                     375,
     5,                                               {{"Up", INPUT_PAD_AXIS, 2},
      {"Down", INPUT_PAD_AXIS, 3},
      {"Left", INPUT_PAD_AXIS, 0},
      {"Right", INPUT_PAD_AXIS, 1},
      {"Press (Button 11)", INPUT_PAD_BUTTON, 7}}                          },
    {"Right trigger (Button 8)", 1, 60,  690, 175, 1, {{"", INPUT_PAD_AXIS, 11}}  },
    {"Right bumper (Button 6)",             1, 110, 705, 207, 1, {{"", INPUT_PAD_BUTTON, 10}}},
    {"Start (Button 10)",                    1, 165, 595, 278, 1, {{"", INPUT_PAD_BUTTON, 6}} },
    {"Buttons (the right-hand D-pad)",
     1,                             215,
     726,                                     285,
     4,                                               {{"Up (Button 4)", INPUT_PAD_BUTTON, 3},
      {"Down (Button 2)", INPUT_PAD_BUTTON, 0},
      {"Left (Button 1)", INPUT_PAD_BUTTON, 2},
      {"Right (Button 3)", INPUT_PAD_BUTTON, 1}}                           },
    {"Right stick",
     1,                             315,
     654,                                     375,
     5,                                               {{"Up", INPUT_PAD_AXIS, 6},
      {"Down", INPUT_PAD_AXIS, 7},
      {"Left", INPUT_PAD_AXIS, 4},
      {"Right", INPUT_PAD_AXIS, 5},
      {"Press (Button 12)", INPUT_PAD_BUTTON, 8}}                          },
    {"Home (Button 13)",                     2, 462, 550, 343, 1, {{"", INPUT_PAD_BUTTON, 5}} },
};
// clang-format on

// "Move up, 2x Attack": every action on that pad input (with modifiers or a double tap said so)
static bool ControlsExport__PadActions(const int profile, const int kind, const int value, char *text,
                                       const size_t size, bool *clash) {
    text[0] = '\0';
    *clash  = false;
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        for (int b = 0; b < list->count; b++) {
            const ControlInput *input = &list->inputs[b];
            if ((int)input->kind != kind || input->value != value) {
                continue;
            }
            const size_t used = strlen(text);
            snprintf(text + used, size - used, "%s%s%s%s%s%s", used > 0 ? ", " : "", input->twice ? "2x " : "",
                     (input->mods & KMOD_CTRL) ? "Ctrl+" : "", (input->mods & KMOD_SHIFT) ? "Shift+" : "",
                     (input->mods & KMOD_ALT) ? "Alt+" : "", Controls_ActionName((ControlAction)a));
            *clash = *clash || Controls_Conflict(profile, (ControlAction)a, input) == CONFLICT_CLASH;
        }
    }
    return text[0] != '\0';
}

// The shell: triggers, bumpers and the body (the leader lines go over it, under the buttons)
// The shell: triggers, bumpers and the body (the leader lines go over it, under the controls)
static void ControlsExport__PadBody(FILE *file) {
    const char *style = "fill=\"#d6d6d6\" stroke=\"#505050\" stroke-width=\"2\"";
    fprintf(file, "<rect x=\"385\" y=\"160\" width=\"95\" height=\"32\" rx=\"10\" %s/>\n", style);
    fprintf(file, "<rect x=\"620\" y=\"160\" width=\"95\" height=\"32\" rx=\"10\" %s/>\n", style);
    fprintf(file, "<rect x=\"378\" y=\"196\" width=\"110\" height=\"22\" rx=\"8\" fill=\"#bdbdbd\" stroke=\"#505050\" "
                  "stroke-width=\"2\"/>\n");
    fprintf(file, "<rect x=\"612\" y=\"196\" width=\"110\" height=\"22\" rx=\"8\" fill=\"#bdbdbd\" stroke=\"#505050\" "
                  "stroke-width=\"2\"/>\n");
    fprintf(file,
            "<path d=\"M 380 215 L 720 215 Q 790 215 810 290 L 845 450 Q 860 530 800 535 Q 755 538 725 470 "
            "L 690 420 L 410 420 L 375 470 Q 345 538 300 535 Q 240 530 255 450 L 290 290 Q 310 215 380 215 Z\" %s/>\n",
            style);
}

// A stick: a ring around a dished top
static void ControlsExport__PadStick(FILE *file, const int x, const int y) {
    fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"34\" fill=\"#9a9a9a\" stroke=\"#505050\" stroke-width=\"2\"/>\n", x,
            y);
    fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"26\" fill=\"#7a7a7a\" stroke=\"#404040\" stroke-width=\"1.5\"/>\n",
            x, y);
    fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"19\" fill=\"#6a6a6a\"/>\n", x, y);
    fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"21\" fill=\"none\" stroke=\"#9a9a9a\" stroke-width=\"2\"/>\n", x, y);
}

// D-pad, the four buttons, Select / Start / Home and the sticks, over the leader lines
static void ControlsExport__PadControls(FILE *file) {
    // D-pad: four separate arrow keys, pointed towards the middle
    static const char *dpad[4] = {"409,252 431,252 431,272 420,282 409,272", "409,318 431,318 431,298 420,288 409,298",
                                  "387,274 407,274 417,285 407,296 387,296", "453,274 433,274 423,285 433,296 453,296"};
    for (int d = 0; d < 4; d++) {
        fprintf(file, "<polygon points=\"%s\" fill=\"#6a6a6a\" stroke=\"#404040\" stroke-width=\"1.5\"/>\n", dpad[d]);
    }
    // The four buttons, each with a small arrow for its name (up, left, right, down)
    static const struct {
        int         x, y;
        const char *arrow;
    } buttons[4] = {
        {680, 253, "0,-6 -5,3 5,3" },
        {648, 285, "-6,0 3,-5 3,5" },
        {712, 285, "6,0 -3,-5 -3,5"},
        {680, 317, "0,6 -5,-3 5,-3"}
    };
    for (int b = 0; b < 4; b++) {
        fprintf(file,
                "<circle cx=\"%d\" cy=\"%d\" r=\"14\" fill=\"#8a8a8a\" stroke=\"#404040\" stroke-width=\"1.5\"/>\n",
                buttons[b].x, buttons[b].y);
        fprintf(file, "<polygon points=\"%s\" transform=\"translate(%d %d)\" fill=\"#f0f0f0\"/>\n", buttons[b].arrow,
                buttons[b].x, buttons[b].y);
    }
    // Select, Start, Home
    fprintf(file, "<rect x=\"494\" y=\"279\" width=\"22\" height=\"12\" rx=\"4\" fill=\"#6a6a6a\"/>\n");
    fprintf(file, "<polygon points=\"584,279 606,285 584,291\" fill=\"#6a6a6a\"/>\n");
    fprintf(file, "<circle cx=\"550\" cy=\"330\" r=\"13\" fill=\"#6a6a6a\" stroke=\"#404040\"/>\n");
    ControlsExport__PadStick(file, 480, 375);
    ControlsExport__PadStick(file, 620, 375);
}

bool ControlsExport_PadSvg(const int profile, const char *path, const bool dark) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    const int width  = 1100;
    const int height = 620;
    fprintf(file,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" "
            "font-family=\"Segoe UI, Helvetica, Arial, sans-serif\" fill=\"#111111\">\n",
            width, height, width, height);
    fprintf(file, "<rect width=\"100%%\" height=\"100%%\" fill=\"#f4f4f4\"/>\n");
    fprintf(file, "<text x=\"20\" y=\"30\" font-size=\"20\" font-weight=\"bold\">Abyss Engine gamepad: %s</text>\n",
            Settings_ProfileName(profile));
    ControlsExport__PadBody(file);

    // Leader lines: from the label straight to the control
    const size_t count = sizeof(pad_callouts) / sizeof(pad_callouts[0]);
    for (size_t c = 0; c < count; c++) {
        const PadCallout *callout = &pad_callouts[c];
        const int         from_x  = callout->side == 2 ? callout->target_x : (callout->side == 1 ? 845 : 255);
        const int         from_y  = callout->side == 2 ? callout->y - 13 : callout->y - 4;
        fprintf(file, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"#707070\" stroke-width=\"1.2\"/>\n",
                from_x, from_y, callout->target_x, callout->target_y);
    }
    ControlsExport__PadControls(file);
    for (size_t c = 0; c < count; c++) {
        const PadCallout *callout = &pad_callouts[c];
        if (callout->side != 2) {
            fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"3\" fill=\"#707070\"/>\n", callout->target_x,
                    callout->target_y);
        }
    }

    const int line = 15;
    for (size_t c = 0; c < count; c++) {
        const PadCallout *callout = &pad_callouts[c];
        if (callout->side == 2) {
            char       actions[256];
            bool       clash = false;
            const bool bound = ControlsExport__PadActions(profile, callout->parts[0].kind, callout->parts[0].value,
                                                          actions, sizeof(actions), &clash);
            fprintf(file, "<text x=\"550\" y=\"%d\" font-size=\"13\" font-weight=\"bold\" text-anchor=\"middle\">",
                    callout->y);
            ControlsExport__Text(file, callout->title);
            fprintf(file, "</text>\n<text x=\"550\" y=\"%d\" font-size=\"11\" text-anchor=\"middle\"%s>",
                    callout->y + line, clash ? " fill=\"#d01010\"" : (bound ? "" : " fill=\"#909090\""));
            ControlsExport__Text(file, bound ? actions : "(free)");
            fprintf(file, "</text>\n");
            continue;
        }
        // Left column: right-aligned against its line; right column: left-aligned
        const bool  right  = callout->side == 1;
        const int   x      = right ? 850 : 250;
        const char *anchor = right ? "start" : "end";
        fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"13\" font-weight=\"bold\" text-anchor=\"%s\">", x,
                callout->y, anchor);
        ControlsExport__Text(file, callout->title);
        fprintf(file, "</text>\n");
        for (int p = 0; p < callout->part_count; p++) {
            const PadPart *part = &callout->parts[p];
            char           actions[256];
            bool           clash = false;
            const bool     bound =
                ControlsExport__PadActions(profile, part->kind, part->value, actions, sizeof(actions), &clash);
            fprintf(file, "<text x=\"%d\" y=\"%d\" font-size=\"11\" text-anchor=\"%s\"%s>", x,
                    callout->y + ((p + 1) * line), anchor,
                    clash ? " fill=\"#d01010\"" : (bound ? "" : " fill=\"#909090\""));
            if (part->label[0] != '\0') {
                ControlsExport__Text(file, part->label);
                fprintf(file, ": ");
            }
            ControlsExport__Text(file, bound ? actions : "(free)");
            fprintf(file, "</text>\n");
        }
    }

    // Anything bound to the extra buttons some pads have
    char extras[512] = "";
    for (int b = 15; b <= 20; b++) {
        char actions[256];
        bool clash;
        if (ControlsExport__PadActions(profile, INPUT_PAD_BUTTON, b, actions, sizeof(actions), &clash)) {
            char               name[48];
            const ControlInput input = {INPUT_PAD_BUTTON, b, 0, false};
            const size_t       used  = strlen(extras);
            Controls_InputName(&input, name, sizeof(name));
            snprintf(extras + used, sizeof(extras) - used, "%s%s: %s", used > 0 ? "; " : "", name + 4, actions);
        }
    }
    fprintf(file,
            "<text x=\"20\" y=\"%d\" font-size=\"12\">Buttons are numbered as Windows numbers them and named by where "
            "they are (the four on the right are a second D-pad), whatever the pad prints on them.</text>\n",
            height - 54);
    fprintf(file,
            "<text x=\"20\" y=\"%d\" font-size=\"12\">Red: clashes with another action. (free): nothing on it yet. "
            "Paddles, touchpad and other extra buttons: ",
            height - 34);
    ControlsExport__Text(file, extras[0] != '\0' ? extras : "nothing bound");
    fprintf(file, "</text>\n</svg>\n");
    fclose(file);
    return !dark || ControlsExport__MakeDark(path);
}
