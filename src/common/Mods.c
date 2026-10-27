#include "Mods.h"
#include "AbyssConfiguration.h"
#include "Logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static Mod  mods[MODS_MAX];
static int  mod_count = 0;
static char started_signature[1024]; // the mods on when the game started
// The mods the files come from: those on when the game started, in order. Options > Mods changes mods[] (what's saved
// for next time), not these: the tables are read once, so a change only takes effect on a restart
static char active[MODS_MAX][64];
static int  active_count = 0;
static bool started      = false;
static char folder_path[4096];

const char *Mods_Folder(void) {
    snprintf(folder_path, sizeof(folder_path), "%s/mods", AbyssConfiguration_GetConfigFolder());
    return folder_path;
}

static void Mods__Trim(char *text) {
    char *start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
}

// A mod's mod.ini (a folder without one is a mod too, named after its folder)
static void Mods__ReadInfo(Mod *mod) {
    snprintf(mod->name, sizeof(mod->name), "%s", mod->folder);
    char path[4300];
    snprintf(path, sizeof(path), "%s/%s/mod.ini", Mods_Folder(), mod->folder);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        *eq = '\0';
        char key[64];
        snprintf(key, sizeof(key), "%.63s", line);
        Mods__Trim(key);
        char *value = eq + 1;
        Mods__Trim(value);
        if (strcmp(key, "Name") == 0 && value[0] != '\0') {
            snprintf(mod->name, sizeof(mod->name), "%s", value);
        } else if (strcmp(key, "Author") == 0) {
            snprintf(mod->author, sizeof(mod->author), "%s", value);
        } else if (strcmp(key, "Version") == 0) {
            snprintf(mod->version, sizeof(mod->version), "%s", value);
        } else if (strcmp(key, "Description") == 0) {
            snprintf(mod->description, sizeof(mod->description), "%s", value);
        }
    }
    fclose(file);
}

static int Mods__Find(const char *folder) {
    for (int m = 0; m < mod_count; m++) {
        if (strcmp(mods[m].folder, folder) == 0) {
            return m;
        }
    }
    return -1;
}

static void Mods__Add(const char *folder) {
    if (mod_count >= MODS_MAX || folder[0] == '.' || strlen(folder) >= sizeof(mods[0].folder) ||
        Mods__Find(folder) >= 0) {
        if (strlen(folder) >= sizeof(mods[0].folder)) {
            LOG_WARN("Mod folder name too long (63 letters at most): '%s'", folder);
        }
        return;
    }
    Mod *mod = &mods[mod_count++];
    memset(mod, 0, sizeof(Mod));
    snprintf(mod->folder, sizeof(mod->folder), "%s", folder);
    Mods__ReadInfo(mod);
}

void Mods_Load(void) {
    mod_count = 0;
    // The order first ("+folder" / "-folder"), then any folder it doesn't name yet (off, at the end)
    char path[4300];
    snprintf(path, sizeof(path), "%s/load_order.txt", Mods_Folder());
    FILE *order = fopen(path, "r");
    if (order != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), order) != NULL) {
            Mods__Trim(line);
            if ((line[0] != '+' && line[0] != '-') || line[1] == '\0') {
                continue;
            }
            char check[4400];
            snprintf(check, sizeof(check), "%s/%s", Mods_Folder(), line + 1);
#ifdef _WIN32
            const bool there = _access(check, 0) == 0;
#else
            struct stat info;
            const bool  there = stat(check, &info) == 0 && S_ISDIR(info.st_mode);
#endif
            if (there) {
                Mods__Add(line + 1);
                if (mod_count > 0 && strcmp(mods[mod_count - 1].folder, line + 1) == 0) {
                    mods[mod_count - 1].enabled = line[0] == '+';
                }
            }
        }
        fclose(order);
    }
#ifdef _WIN32
    char pattern[4300];
    snprintf(pattern, sizeof(pattern), "%s/*", Mods_Folder());
    struct _finddata_t found;
    const intptr_t     handle = _findfirst(pattern, &found);
    if (handle != -1) {
        do {
            if ((found.attrib & _A_SUBDIR) != 0) {
                Mods__Add(found.name);
            }
        } while (_findnext(handle, &found) == 0);
        _findclose(handle);
    }
#else
    DIR *dir = opendir(Mods_Folder());
    for (struct dirent *entry = dir != NULL ? readdir(dir) : NULL; entry != NULL; entry = readdir(dir)) {
        char        check[4400];
        struct stat info;
        snprintf(check, sizeof(check), "%s/%s", Mods_Folder(), entry->d_name);
        if (stat(check, &info) == 0 && S_ISDIR(info.st_mode)) {
            Mods__Add(entry->d_name);
        }
    }
    if (dir != NULL) {
        closedir(dir);
    }
#endif
    if (started) {
        return; // (Options going back to what's saved: the files keep coming from the mods the game started with)
    }
    started = true;
    Mods_Signature(started_signature, sizeof(started_signature));
    active_count = 0;
    for (int m = 0; m < mod_count; m++) {
        if (mods[m].enabled) {
            snprintf(active[active_count++], sizeof(active[0]), "%s", mods[m].folder);
            LOG_INFO("Mod on: %s (%s%s%s)", mods[m].name, mods[m].folder, mods[m].version[0] != '\0' ? ", " : "",
                     mods[m].version);
        }
    }
}

int Mods_Count(void) { return mod_count; }

const Mod *Mods_Get(const int index) { return index >= 0 && index < mod_count ? &mods[index] : NULL; }

void Mods_SetEnabled(const int index, const bool enabled) {
    if (index >= 0 && index < mod_count) {
        mods[index].enabled = enabled;
    }
}

void Mods_Move(const int index, const int step) {
    const int to = index + step;
    if (index < 0 || index >= mod_count || to < 0 || to >= mod_count) {
        return;
    }
    const Mod held = mods[index];
    mods[index]    = mods[to];
    mods[to]       = held;
}

void Mods_Save(void) {
#ifdef _WIN32
    _mkdir(Mods_Folder());
#else
    mkdir(Mods_Folder(), 0755);
#endif
    char path[4300];
    snprintf(path, sizeof(path), "%s/load_order.txt", Mods_Folder());
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        LOG_WARN("Could not save the mods' order to '%s'", path);
        return;
    }
    fprintf(file, "# The mods, first first: + on, - off (Options > Mods writes this)\n");
    for (int m = 0; m < mod_count; m++) {
        fprintf(file, "%c%s\n", mods[m].enabled ? '+' : '-', mods[m].folder);
    }
    fclose(file);
}

bool Mods_Changed(void) {
    char now[1024];
    Mods_Signature(now, sizeof(now));
    return strcmp(now, started_signature) != 0;
}

bool Mods_FindFile(const char *path, char *out, const int size) {
    for (int m = 0; m < active_count; m++) {
        snprintf(out, (size_t)size, "%s/%s/%s", Mods_Folder(), active[m], path);
        FILE *probe = fopen(out, "rb");
        if (probe != NULL) {
            fclose(probe);
            return true;
        }
    }
    return false;
}

void Mods_ActiveSignature(char *out, const int size) { snprintf(out, (size_t)size, "%s", started_signature); }

void Mods_Signature(char *out, const int size) {
    int at = 0;
    out[0] = '\0';
    for (int m = 0; m < mod_count && at >= 0 && at < size; m++) {
        if (mods[m].enabled) {
            at +=
                snprintf(out + at, (size_t)(size - at), "%s%s@%s", at > 0 ? ";" : "", mods[m].folder, mods[m].version);
        }
    }
}
