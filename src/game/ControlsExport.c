#include "ControlsExport.h"
#include "Controls.h"
#include "Settings.h"
#include <SDL2/SDL.h>
#include <stdio.h>
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

bool ControlsExport_Markdown(const int profile, const char *path, const char *svg_name) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "# Abyss Engine controls (%s)\n\n", Settings_ProfileName(profile));
    if (svg_name != NULL) {
        fprintf(file, "![Keyboard](%s)\n\n", svg_name);
    }
    fprintf(file, "Actions marked \\* aren't in the engine yet.\n");
    for (int g = 0; g < GROUP_COUNT; g++) {
        fprintf(file, "\n## %s\n\n| Action | Inputs | Note |\n|---|---|---|\n", Controls_GroupName((ControlGroup)g));
        for (int a = 0; a < ACTION_COUNT; a++) {
            if ((int)Controls_ActionGroup((ControlAction)a) != g) {
                continue;
            }
            const ControlBindings *list        = Controls_GetBindings(profile, (ControlAction)a);
            char                   inputs[512] = "";
            char                   notes[256]  = "";
            for (int b = 0; b < list->count; b++) {
                char name[64];
                Controls_InputName(&list->inputs[b], name, sizeof(name));
                const size_t used = strlen(inputs);
                snprintf(inputs + used, sizeof(inputs) - used, "%s`%s`", b > 0 ? ", " : "", name);
                const ControlConflict conflict = Controls_Conflict(profile, (ControlAction)a, &list->inputs[b]);
                if (conflict != CONFLICT_NONE && notes[0] == '\0') {
                    snprintf(notes, sizeof(notes), "%s: %s", name, ControlsExport__Conflict(conflict));
                }
            }
            fprintf(file, "| %s%s | %s | %s |\n", Controls_ActionName((ControlAction)a),
                    Controls_ActionWorks((ControlAction)a) ? "" : " \\*", inputs[0] != '\0' ? inputs : "-", notes);
        }
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

bool ControlsExport_Svg(const int profile, const char *path) {
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
            "font-family=\"Segoe UI, Helvetica, Arial, sans-serif\">\n",
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
    return true;
}
