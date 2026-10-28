#include "Objects.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ObjectPreset {
    short act;
    short id;
    short object_id;
} ObjectPreset;

static const ObjectPreset object_presets[] = {
#include "ObjectPresets.inc"
};

#define PRESET_COUNT (int)(sizeof(object_presets) / sizeof(object_presets[0]))

const char *const object_mode_tokens[OBJECT_MODE_COUNT] = {"nu", "op", "on", "s1", "s2", "s3", "s4", "s5"};

static DataTable *objects = NULL;
static bool       loaded  = false;
static int        c_id;
static int        c_name;
static int        c_token;
static int        c_size_x;
static int        c_size_y;
static int        c_selectable;
static int        c_is_door;
static int        c_mode[OBJECT_MODE_COUNT];
static int        c_collision[OBJECT_MODE_COUNT];
static int        c_frames[OBJECT_MODE_COUNT];
static int        c_start[OBJECT_MODE_COUNT];
static int        c_cycle_on;
static int        c_operate;
static int        c_lockable;
static int        c_trap;
static int        c_parm[OBJECT_PARMS];
static int        c_automap;
static int        c_description;

// The sound an object makes all the time (sounds.txt object_*_loop), from what objects.txt's description says it is:
// torches and braziers burn, fires crackle, rivers and fountains run, a pool of blood bubbles, flies buzz
static const char *Objects__LoopSound(const char *description) {
    char text[128];
    snprintf(text, sizeof(text), "%s", description != NULL ? description : "");
    for (char *c = text; *c != '\0'; c++) {
        *c = (char)tolower((unsigned char)*c);
    }
    if (strstr(text, "torch") != NULL) {
        return "object_fire_loop_torch";
    }
    if (strstr(text, "brazier") != NULL) {
        return strstr(text, "hell") != NULL ? "object_fire_loop_hellbrazier" : "object_fire_loop_brazier";
    }
    if (strstr(text, "bonfire") != NULL || strncmp(text, "fire ", 5) == 0 || strstr(text, "burning") != NULL) {
        return "object_fire_loop_house";
    }
    if (strstr(text, "river") != NULL || strstr(text, "waterfall") != NULL) {
        return "object_river";
    }
    if (strstr(text, "fountain") != NULL && strstr(text, "well") == NULL) {
        return "object_fountain";
    }
    if (strstr(text, "bubbl") != NULL) {
        return "object_bubbles";
    }
    if (strstr(text, "flies") != NULL) {
        return "object_flies_loop";
    }
    return NULL;
}

static void Objects__Load(void) {
    if (loaded) {
        return;
    }
    loaded  = true;
    objects = DataTable_Load(EXCEL_OBJECTS);
    if (objects == NULL) {
        return;
    }
    c_id          = DataTable_GetColumnIndex(objects, "Id");
    c_name        = DataTable_GetColumnIndex(objects, "Name");
    c_description = DataTable_GetColumnIndex(objects, "description - not loaded");
    c_token       = DataTable_GetColumnIndex(objects, "Token");
    c_size_x      = DataTable_GetColumnIndex(objects, "SizeX");
    c_size_y      = DataTable_GetColumnIndex(objects, "SizeY");
    c_selectable  = DataTable_GetColumnIndex(objects, "Selectable0");
    c_is_door     = DataTable_GetColumnIndex(objects, "IsDoor");
    c_cycle_on    = DataTable_GetColumnIndex(objects, "CycleAnim2");
    c_operate     = DataTable_GetColumnIndex(objects, "OperateFn");
    c_lockable    = DataTable_GetColumnIndex(objects, "Lockable");
    c_trap        = DataTable_GetColumnIndex(objects, "TrapProb");
    for (int p = 0; p < OBJECT_PARMS; p++) {
        char column[8];
        snprintf(column, sizeof(column), "Parm%d", p);
        c_parm[p] = DataTable_GetColumnIndex(objects, column);
    }
    c_automap = DataTable_GetColumnIndex(objects, "AutoMap");
    for (int m = 0; m < OBJECT_MODE_COUNT; m++) {
        char column[24];
        snprintf(column, sizeof(column), "Mode%d", m);
        c_mode[m] = DataTable_GetColumnIndex(objects, column);
        snprintf(column, sizeof(column), "HasCollision%d", m);
        c_collision[m] = DataTable_GetColumnIndex(objects, column);
        snprintf(column, sizeof(column), "FrameCnt%d", m);
        c_frames[m] = DataTable_GetColumnIndex(objects, column);
        snprintf(column, sizeof(column), "Start%d", m);
        c_start[m] = DataTable_GetColumnIndex(objects, column);
    }
}

int Objects_PresetRow(const int act, const int preset_id) {
    Objects__Load();
    if (objects == NULL) {
        return -1;
    }
    int object_id = -1;
    for (int p = 0; p < PRESET_COUNT; p++) {
        if (object_presets[p].act == act + 1 && object_presets[p].id == preset_id) {
            object_id = object_presets[p].object_id;
            break;
        }
    }
    if (object_id < 0) {
        return -1;
    }
    // objects.txt rows are in Id order, but look it up rather than trust that
    if (object_id < DataTable_GetRowCount(objects) && DataTable_GetInt(objects, object_id, c_id) == object_id) {
        return object_id;
    }
    for (int row = 0; row < DataTable_GetRowCount(objects); row++) {
        if (DataTable_GetInt(objects, row, c_id) == object_id) {
            return row;
        }
    }
    return -1;
}

bool Objects_Get(const int row, ObjectInfo *info) {
    Objects__Load();
    memset(info, 0, sizeof(ObjectInfo));
    if (objects == NULL || row < 0 || row >= DataTable_GetRowCount(objects)) {
        return false;
    }
    info->row          = row;
    const char *token  = DataTable_GetString(objects, row, c_token);
    size_t      length = 0;
    for (; token[length] != '\0' && length + 1 < sizeof(info->token); length++) {
        info->token[length] = (char)tolower((unsigned char)token[length]);
    }
    info->token[length] = '\0';
    info->name          = DataTable_GetString(objects, row, c_name);
    info->loop_sound    = Objects__LoopSound(DataTable_GetString(objects, row, c_description));
    info->size_x        = DataTable_GetInt(objects, row, c_size_x);
    info->size_y        = DataTable_GetInt(objects, row, c_size_y);
    info->selectable    = DataTable_GetInt(objects, row, c_selectable) != 0;
    info->is_door       = DataTable_GetInt(objects, row, c_is_door) != 0;
    info->operate_fn    = DataTable_GetInt(objects, row, c_operate);
    info->lockable      = c_lockable >= 0 && DataTable_GetInt(objects, row, c_lockable) != 0;
    info->trap_prob     = c_trap >= 0 ? DataTable_GetInt(objects, row, c_trap) : 0;
    for (int p = 0; p < OBJECT_PARMS; p++) {
        info->parm[p] = c_parm[p] >= 0 ? DataTable_GetInt(objects, row, c_parm[p]) : 0;
    }
    info->automap = c_automap >= 0 ? DataTable_GetInt(objects, row, c_automap) : 0;
    for (int m = 0; m < OBJECT_MODE_COUNT; m++) {
        info->has_mode[m]    = DataTable_GetInt(objects, row, c_mode[m]) != 0;
        info->collision[m]   = DataTable_GetInt(objects, row, c_collision[m]) != 0;
        info->frame_count[m] = DataTable_GetInt(objects, row, c_frames[m]);
        info->start_frame[m] = DataTable_GetInt(objects, row, c_start[m]);
    }
    // Torches, braziers and fires burn from the start: they can't be clicked and their "on" animation loops
    const bool lit  = !info->selectable && info->has_mode[2] && DataTable_GetInt(objects, row, c_cycle_on) != 0;
    info->idle_mode = lit ? "on" : (info->has_mode[0] ? "nu" : (info->has_mode[2] ? "on" : "nu"));
    return true;
}

int Objects_FindToken(const char *token) {
    Objects__Load();
    if (objects == NULL || token == NULL) {
        return -1;
    }
    for (int row = 0; row < DataTable_GetRowCount(objects); row++) {
        const char *t = DataTable_GetString(objects, row, c_token);
        size_t      i = 0;
        while (t[i] != '\0' && token[i] != '\0' && tolower((unsigned char)t[i]) == tolower((unsigned char)token[i])) {
            i++;
        }
        if (t[i] == '\0' && token[i] == '\0') {
            return row;
        }
    }
    return -1;
}

int Objects_ModeIndex(const char *mode) {
    for (int m = 0; mode != NULL && m < OBJECT_MODE_COUNT; m++) {
        if (tolower((unsigned char)mode[0]) == object_mode_tokens[m][0] &&
            tolower((unsigned char)mode[1]) == object_mode_tokens[m][1]) {
            return m;
        }
    }
    return -1;
}

void Objects_FrameRange(const ObjectInfo *info, const int mode, const int figure_frames, int *start, int *count) {
    *start = 0;
    *count = figure_frames;
    if (info == NULL || mode < 0 || mode >= OBJECT_MODE_COUNT || figure_frames <= 0) {
        return;
    }
    int first = info->start_frame[mode];
    int n     = info->frame_count[mode];
    first     = first < 0 ? 0 : (first >= figure_frames ? figure_frames - 1 : first);
    if (n <= 0 || first + n > figure_frames) {
        n = figure_frames - first;
    }
    *start = first;
    *count = n > 0 ? n : 1;
}

void Objects_Release(void) {
    DataTable_Destroy(&objects);
    loaded = false;
}
