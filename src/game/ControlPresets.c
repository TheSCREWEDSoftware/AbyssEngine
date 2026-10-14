#include "ControlPresets.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "Controls.h"
#include "Settings.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

static void ControlPresets__Folder(char *path, const size_t size) {
    snprintf(path, size, "%s/presets", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

// Only letters, digits, spaces, '-' and '_' make it into a file name
static void ControlPresets__Path(const char *name, char *path, const size_t size) {
    char safe[CONTROL_PRESET_NAME_MAX];
    int  n = 0;
    for (const char *c = name; *c != '\0' && n < CONTROL_PRESET_NAME_MAX - 1; c++) {
        if (isalnum((unsigned char)*c) || *c == ' ' || *c == '-' || *c == '_') {
            safe[n++] = *c;
        }
    }
    safe[n] = '\0';
    char folder[4096];
    ControlPresets__Folder(folder, sizeof(folder));
    snprintf(path, size, "%s/%s.ini", folder, n > 0 ? safe : "preset");
}

static int ControlPresets__Compare(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void ControlPresets__Add(char names[][CONTROL_PRESET_NAME_MAX], int *count, const int max, const char *file) {
    const size_t length = strlen(file);
    if (*count >= max || length <= 4 || strcmp(file + length - 4, ".ini") != 0) {
        return;
    }
    snprintf(names[*count], CONTROL_PRESET_NAME_MAX, "%.*s", (int)(length - 4), file);
    (*count)++;
}

int ControlPresets_List(char names[][CONTROL_PRESET_NAME_MAX], const int max) {
    char folder[4096];
    ControlPresets__Folder(folder, sizeof(folder));
    int count = 0;
#ifdef _WIN32
    char pattern[4200];
    snprintf(pattern, sizeof(pattern), "%s\\*.ini", folder);
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            ControlPresets__Add(names, &count, max, found.cFileName);
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(folder);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            ControlPresets__Add(names, &count, max, entry->d_name);
        }
        closedir(dir);
    }
#endif
    qsort(names, (size_t)count, CONTROL_PRESET_NAME_MAX, ControlPresets__Compare);
    return count;
}

bool ControlPresets_Save(const int profile, const char *name) {
    char path[4200];
    ControlPresets__Path(name, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        LOG_WARN("Could not save the preset %s", path);
        return false;
    }
    fprintf(file,
            "# Abyss Engine controls preset: %s (saved from %s). Options › Controls › Presets loads it.\n"
            "# Each line: action = inputs, separated by commas (the same as options.ini).\n",
            name, Settings_ProfileName(profile));
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        fprintf(file, "%s = ", Controls_ActionId((ControlAction)a));
        for (int b = 0; b < list->count; b++) {
            char input[64];
            Controls_InputName(&list->inputs[b], input, sizeof(input));
            fprintf(file, "%s%s", b > 0 ? ", " : "", input);
        }
        fprintf(file, "\n");
    }
    fclose(file);
    return true;
}

static char *ControlPresets__Trim(char *text) {
    while (isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

bool ControlPresets_Load(const int profile, const char *name) {
    char path[4200];
    ControlPresets__Path(name, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    Controls_SetOwnProfile(profile, true);
    char line[1024];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = ControlPresets__Trim(line);
        if (text[0] == '#' || text[0] == ';' || text[0] == '[' || text[0] == '\0') {
            continue;
        }
        char *equals = strchr(text, '=');
        if (equals != NULL) {
            *equals = '\0';
            Controls_ReadLine(profile, ControlPresets__Trim(text), ControlPresets__Trim(equals + 1));
        }
    }
    fclose(file);
    return true;
}

bool ControlPresets_Delete(const char *name) {
    char path[4200];
    ControlPresets__Path(name, path, sizeof(path));
    return remove(path) == 0;
}
