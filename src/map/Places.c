#include "Places.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_LVLPREST "/data/global/excel/LvlPrest.txt"
#define EXCEL_LEVELS   "/data/global/excel/Levels.txt"

// Outdoor presets that are really dungeons or special arenas
static const int excluded_levels[] = {
    124, // Halls of Vaught (Nihlathak)
    136, // Uber Tristram
};

static Place *places      = NULL;
static int    place_count = -1;

static bool Places__Excluded(const int level_id) {
    for (size_t i = 0; i < sizeof(excluded_levels) / sizeof(excluded_levels[0]); i++) {
        if (excluded_levels[i] == level_id) {
            return true;
        }
    }
    return false;
}

static void Places__Load(void) {
    place_count       = 0;
    DataTable *preset = DataTable_Load(EXCEL_LVLPREST);
    DataTable *levels = DataTable_Load(EXCEL_LEVELS);
    if (preset == NULL || levels == NULL) {
        LOG_WARN("Could not load the level tables for Free Roam");
        DataTable_Destroy(&preset);
        DataTable_Destroy(&levels);
        return;
    }

    const int rows = DataTable_GetRowCount(preset);
    places         = calloc(rows > 0 ? (size_t)rows : 1, sizeof(Place));
    FAIL_IF_NULL(places);

    const int c_level    = DataTable_GetColumnIndex(preset, "LevelId");
    const int c_outdoors = DataTable_GetColumnIndex(preset, "Outdoors");
    int       c_files[PLACE_MAX_FILES];
    for (int f = 0; f < PLACE_MAX_FILES; f++) {
        char column[8];
        snprintf(column, sizeof(column), "File%d", f + 1);
        c_files[f] = DataTable_GetColumnIndex(preset, column);
    }
    const int c_id   = DataTable_GetColumnIndex(levels, "Id");
    const int c_name = DataTable_GetColumnIndex(levels, "LevelName");
    const int c_act  = DataTable_GetColumnIndex(levels, "Act");

    for (int row = 0; row < rows; row++) {
        const int level_id = DataTable_GetInt(preset, row, c_level);
        if (level_id <= 0 || DataTable_GetInt(preset, row, c_outdoors) != 1 || Places__Excluded(level_id)) {
            continue;
        }

        Place *place = &places[place_count];
        memset(place, 0, sizeof(Place));
        place->level_id = level_id;
        for (int f = 0; f < PLACE_MAX_FILES; f++) {
            const char *file = DataTable_GetString(preset, row, c_files[f]);
            if (file[0] == '\0' || strcmp(file, "0") == 0) {
                continue;
            }
            char *path = place->files[place->file_count];
            snprintf(path, sizeof(place->files[0]), "data\\global\\tiles\\%s", file);
            for (char *c = path; *c != '\0'; c++) {
                if (*c == '/') {
                    *c = '\\';
                }
            }
            if (FileManager_FileExists(path)) {
                place->file_count++;
            }
        }
        if (place->file_count == 0) {
            continue;
        }

        snprintf(place->name, sizeof(place->name), "Level %d", level_id);
        for (int l = 0; l < DataTable_GetRowCount(levels); l++) {
            if (DataTable_GetInt(levels, l, c_id) == level_id) {
                snprintf(place->name, sizeof(place->name), "%s", DataTable_GetString(levels, l, c_name));
                place->act = DataTable_GetInt(levels, l, c_act);
                break;
            }
        }
        place_count++;
    }

    DataTable_Destroy(&preset);
    DataTable_Destroy(&levels);
}

int Places_GetCount(void) {
    if (place_count < 0) {
        Places__Load();
    }
    return place_count;
}

const Place *Places_Get(const int index) { return (index >= 0 && index < Places_GetCount()) ? &places[index] : NULL; }

void Places_Release(void) {
    free(places);
    places      = NULL;
    place_count = -1;
}
