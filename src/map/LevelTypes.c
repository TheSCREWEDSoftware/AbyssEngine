#include "LevelTypes.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXCEL_LVLTYPES "/data/global/excel/LvlTypes.txt"

static LevelType *level_types      = NULL;
static int        level_type_count = -1;

static void LevelTypes__Load(void) {
    level_type_count = 0;
    DataTable *table = DataTable_Load(EXCEL_LVLTYPES);
    if (table == NULL) {
        LOG_WARN("Could not load %s", EXCEL_LVLTYPES);
        return;
    }

    const int rows = DataTable_GetRowCount(table);
    level_types    = calloc(rows > 0 ? (size_t)rows : 1, sizeof(LevelType));
    FAIL_IF_NULL(level_types);

    const int c_name = DataTable_GetColumnIndex(table, "Name");
    const int c_act  = DataTable_GetColumnIndex(table, "Act");
    int       c_files[LEVEL_TYPE_MAX_FILES];
    for (int f = 0; f < LEVEL_TYPE_MAX_FILES; f++) {
        char column[16];
        snprintf(column, sizeof(column), "File %d", f + 1);
        c_files[f] = DataTable_GetColumnIndex(table, column);
    }

    for (int row = 0; row < rows; row++) {
        const char *name = DataTable_GetString(table, row, c_name);
        const int   act  = DataTable_GetInt(table, row, c_act);
        if (name[0] == '\0' || act < 1 || act > 5) {
            continue; // "None", the "Expansion" separator row
        }

        LevelType *type = &level_types[level_type_count];
        memset(type, 0, sizeof(LevelType));
        snprintf(type->name, sizeof(type->name), "%s", name);
        type->act = act - 1;
        for (int f = 0; f < LEVEL_TYPE_MAX_FILES; f++) {
            const char *file = DataTable_GetString(table, row, c_files[f]);
            if (file[0] == '\0' || strcmp(file, "0") == 0) {
                continue;
            }
            char path[256];
            snprintf(path, sizeof(path), "data\\global\\tiles\\%s", file);
            for (char *c = path; *c != '\0'; c++) {
                if (*c == '/') {
                    *c = '\\';
                }
            }
            type->files[type->file_count] = strdup(path);
            FAIL_IF_NULL(type->files[type->file_count]);
            type->file_count++;
        }
        if (type->file_count > 0) {
            level_type_count++;
        }
    }
    DataTable_Destroy(&table);
}

int LevelTypes_GetCount(void) {
    if (level_type_count < 0) {
        LevelTypes__Load();
    }
    return level_type_count;
}

const LevelType *LevelTypes_Get(const int index) {
    return (index >= 0 && index < LevelTypes_GetCount()) ? &level_types[index] : NULL;
}

int LevelTypes_Find(const char *name) {
    for (int i = 0; i < LevelTypes_GetCount(); i++) {
        if (strcmp(level_types[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void LevelTypes_Release(void) {
    for (int i = 0; i < level_type_count; i++) {
        for (int f = 0; f < level_types[i].file_count; f++) {
            free(level_types[i].files[f]);
        }
    }
    free(level_types);
    level_types      = NULL;
    level_type_count = -1;
}
