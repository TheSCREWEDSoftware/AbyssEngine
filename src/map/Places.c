#include "Places.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "LevelBuilder.h"
#include "Map.h"
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

static int Places__CompareLevel(const void *a, const void *b) {
    return ((const Place *)a)->level_id - ((const Place *)b)->level_id;
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

    // The randomly built areas
    const int level_rows = DataTable_GetRowCount(levels);
    Place    *grown      = realloc(places, sizeof(Place) * (size_t)(place_count + level_rows + 1));
    FAIL_IF_NULL(grown);
    places = grown;
    for (int l = 0; l < level_rows; l++) {
        const int level_id = DataTable_GetInt(levels, l, c_id);
        if (level_id <= 0 || !DataTable_HasValue(levels, l, c_id) || !LevelBuilder_CanBuild(level_id)) {
            continue;
        }
        Place *place = &places[place_count++];
        memset(place, 0, sizeof(Place));
        place->level_id  = level_id;
        place->generated = true;
        place->act       = DataTable_GetInt(levels, l, c_act);
        snprintf(place->name, sizeof(place->name), "%s", DataTable_GetString(levels, l, c_name));
    }
    qsort(places, (size_t)place_count, sizeof(Place), Places__CompareLevel);

    // Areas sharing a name within an act (the seven Tal Rasha's Tombs) get a number
    int *numbers = calloc((size_t)place_count + 1, sizeof(int));
    FAIL_IF_NULL(numbers);
    for (int i = 0; i < place_count; i++) {
        int before = 0;
        int total  = 0;
        for (int j = 0; j < place_count; j++) {
            if (places[j].act == places[i].act && strcmp(places[j].name, places[i].name) == 0) {
                before += j < i ? 1 : 0;
                total++;
            }
        }
        numbers[i] = total > 1 ? before + 1 : 0;
    }
    for (int i = 0; i < place_count; i++) {
        if (numbers[i] > 0) {
            const size_t len = strnlen(places[i].name, sizeof(places[i].name) - 5);
            snprintf(places[i].name + len, sizeof(places[i].name) - len, " %d", numbers[i]);
        }
    }
    free(numbers);

    DataTable_Destroy(&preset);
    DataTable_Destroy(&levels);
}

int Places_GetCount(void) {
    if (place_count < 0) {
        Places__Load();
    }
    return place_count;
}

Map *Places_LoadMap(const Place *place) {
    if (place == NULL) {
        return NULL;
    }
    Map *map = place->generated ? LevelBuilder_Build(place->level_id, 0) : Map_LoadDS1(place->files[0]);
    if (map != NULL) {
        snprintf(map->name, sizeof(map->name), "%s", place->name);
    }
    return map;
}

const Place *Places_Get(const int index) { return (index >= 0 && index < Places_GetCount()) ? &places[index] : NULL; }

void Places_Release(void) {
    free(places);
    places      = NULL;
    place_count = -1;
}
