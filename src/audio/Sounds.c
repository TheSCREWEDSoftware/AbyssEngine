#include "Sounds.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../managers/AudioManager.h"
#include "SoundEffect.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_SOUNDS    "/data/global/excel/sounds.txt"
#define EXCEL_MONSOUNDS "/data/global/excel/MonSounds.txt"
#define SFX_FOLDER      "/data/global/sfx/"

static DataTable    *sounds    = NULL;
static DataTable    *monsounds = NULL;
static DataTable    *monstats  = NULL;
static bool          loaded    = false;
static int          *by_name   = NULL; // row numbers sorted by name
static int           row_count = 0;
static SoundEffect **effects   = NULL; // per row, loaded on first use
static bool         *missing   = NULL; // per row: the file isn't there (no d2sfx.mpq) or can't be read
static int           c_sound   = -1;
static int           c_file    = -1;
static int           c_volume  = -1;
static int           c_group   = -1;
static int           load_rate = 0;

static int Sounds__Compare(const char *a, const char *b) {
    for (; *a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int Sounds__SortRows(const void *a, const void *b) {
    return Sounds__Compare(DataTable_GetString(sounds, *(const int *)a, c_sound),
                           DataTable_GetString(sounds, *(const int *)b, c_sound));
}

static void Sounds__Load(void) {
    if (loaded) {
        return;
    }
    loaded    = true;
    sounds    = DataTable_Load(EXCEL_SOUNDS);
    monsounds = DataTable_Load(EXCEL_MONSOUNDS);
    monstats  = DataTable_Load(EXCEL_MONSTATS);
    if (sounds == NULL) {
        return;
    }
    c_sound   = DataTable_GetColumnIndex(sounds, "Sound");
    c_file    = DataTable_GetColumnIndex(sounds, "FileName");
    c_volume  = DataTable_GetColumnIndex(sounds, "Volume");
    c_group   = DataTable_GetColumnIndex(sounds, "Group Size");
    row_count = DataTable_GetRowCount(sounds);
    by_name   = malloc(sizeof(int) * (size_t)(row_count > 0 ? row_count : 1));
    effects   = calloc((size_t)(row_count > 0 ? row_count : 1), sizeof(SoundEffect *));
    missing   = calloc((size_t)(row_count > 0 ? row_count : 1), sizeof(bool));
    FAIL_IF_NULL(by_name);
    FAIL_IF_NULL(effects);
    FAIL_IF_NULL(missing);
    for (int i = 0; i < row_count; i++) {
        by_name[i] = i;
    }
    qsort(by_name, (size_t)row_count, sizeof(int), Sounds__SortRows);
}

static int Sounds__FindRow(const char *name) {
    Sounds__Load();
    if (sounds == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    int low  = 0;
    int high = row_count - 1;
    while (low <= high) {
        const int middle = (low + high) / 2;
        const int order  = Sounds__Compare(DataTable_GetString(sounds, by_name[middle], c_sound), name);
        if (order == 0) {
            return by_name[middle];
        }
        if (order < 0) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return -1;
}

static SoundEffect *Sounds__Effect(const int row, const int rate) {
    if (row < 0 || row >= row_count || missing[row]) {
        return NULL;
    }
    if (effects[row] == NULL) {
        char        path[512];
        const char *file = DataTable_GetString(sounds, row, c_file);
        if (file[0] == '\0') {
            missing[row] = true;
            return NULL;
        }
        snprintf(path, sizeof(path), SFX_FOLDER "%s", file);
        for (char *c = path; *c != '\0'; c++) {
            if (*c == '\\') {
                *c = '/';
            }
        }
        effects[row] = SoundEffect_Load(path, rate);
        if (effects[row] == NULL) {
            // Speech is in the language's folder (d2speech.mpq, d2xtalk.mpq): data/local/sfx
            char local[512];
            snprintf(local, sizeof(local), "/data/local/sfx/%s", path + strlen(SFX_FOLDER));
            effects[row] = SoundEffect_Load(local, rate);
        }
        if (effects[row] == NULL) {
            missing[row] = true;
        }
    }
    return effects[row];
}

bool Sounds_Exists(const char *name) { return Sounds__FindRow(name) >= 0; }

int Sounds_PlaySpeech(const char *name) {
    int row = Sounds__FindRow(name);
    if (row < 0) {
        return -1;
    }
    const int rate = AudioManager_GetOutputRate();
    if (rate <= 0) {
        return 0;
    }
    load_rate                  = load_rate == 0 ? rate : load_rate;
    const int group            = DataTable_GetInt(sounds, row, c_group);
    row                       += group > 1 ? rand() % group : 0;
    row                        = row < row_count ? row : row_count - 1;
    const SoundEffect *effect  = Sounds__Effect(row, rate);
    if (effect == NULL) {
        return 0; // (no speech MPQ)
    }
    const int volume = DataTable_HasValue(sounds, row, c_volume) ? DataTable_GetInt(sounds, row, c_volume) : 255;
    AudioManager_PlaySpeech(effect, (float)volume / 255.0f);
    return (int)((long)effect->frame_count * 1000 / rate);
}

void Sounds_StopSpeech(void) { AudioManager_StopSpeech(); }

bool Sounds_Play(const char *name, const SoundKind kind) {
    int row = Sounds__FindRow(name);
    if (row < 0) {
        return false;
    }
    const int rate = AudioManager_GetOutputRate();
    if (rate <= 0) {
        return true; // no audio device: nothing to play, but the sound exists
    }
    if (load_rate != rate) {
        // The device changed rate (never in practice): decoded sounds are for the old one
        for (int i = 0; i < row_count; i++) {
            SoundEffect_Destroy(&effects[i]);
        }
        load_rate = rate;
    }
    // A group: this row and the next (size - 1) rows are variants of the same sound
    const int group = DataTable_GetInt(sounds, row, c_group);
    if (group > 1) {
        row += rand() % group;
        if (row >= row_count) {
            row = row_count - 1;
        }
    }
    const SoundEffect *effect = Sounds__Effect(row, rate);
    if (effect == NULL) {
        return true;
    }
    const int volume = DataTable_HasValue(sounds, row, c_volume) ? DataTable_GetInt(sounds, row, c_volume) : 255;
    AudioManager_PlaySound(effect, (float)volume / 255.0f, kind == SOUND_KIND_UI);
    return true;
}

const char *Sounds_MonsterSound(const char *monstats_id, const MonsterSound which) {
    Sounds__Load();
    if (monstats == NULL || monsounds == NULL || monstats_id == NULL) {
        return NULL;
    }
    const int   c_id       = DataTable_GetColumnIndex(monstats, "Id");
    const int   c_monsound = DataTable_GetColumnIndex(monstats, "MonSound");
    const char *set        = NULL;
    for (int row = 0; row < DataTable_GetRowCount(monstats); row++) {
        if (Sounds__Compare(DataTable_GetString(monstats, row, c_id), monstats_id) == 0) {
            set = DataTable_GetString(monstats, row, c_monsound);
            break;
        }
    }
    if (set == NULL || set[0] == '\0') {
        return NULL;
    }
    static const char *columns[] = {"Attack1", "HitSound", "DeathSound"};
    const int          c_set     = DataTable_GetColumnIndex(monsounds, "Id");
    const int          c_value   = DataTable_GetColumnIndex(monsounds, columns[which]);
    for (int row = 0; row < DataTable_GetRowCount(monsounds); row++) {
        if (Sounds__Compare(DataTable_GetString(monsounds, row, c_set), set) == 0) {
            const char *value = DataTable_GetString(monsounds, row, c_value);
            if (value[0] == '\0' && which == MONSTER_SOUND_ATTACK) {
                // Armed monsters (skeletons, ...) have no voice for attacking, just their weapon's swing
                value = DataTable_GetString(monsounds, row, DataTable_GetColumnIndex(monsounds, "Weapon1"));
            }
            return value[0] != '\0' ? value : NULL;
        }
    }
    return NULL;
}

void Sounds_Release(void) {
    AudioManager_StopSounds();
    for (int i = 0; i < row_count; i++) {
        SoundEffect_Destroy(&effects[i]);
    }
    free(effects);
    free(missing);
    free(by_name);
    effects   = NULL;
    missing   = NULL;
    by_name   = NULL;
    row_count = 0;
    load_rate = 0;
    DataTable_Destroy(&sounds);
    DataTable_Destroy(&monsounds);
    DataTable_Destroy(&monstats);
    loaded = false;
}
