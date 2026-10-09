#include "DataTable.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataTableRow {
    char **cells;
    int    cell_count;
} DataTableRow;

struct DataTable {
    char         *buffer;
    DataTableRow  header;
    DataTableRow *rows;
    int           row_count;
};

static void DataTable__SplitRow(char *line, DataTableRow *row) {
    int capacity    = 16;
    row->cells      = malloc(sizeof(char *) * capacity);
    row->cell_count = 0;
    FAIL_IF_NULL(row->cells);

    char *cell = line;
    while (true) {
        if (row->cell_count == capacity) {
            capacity   *= 2;
            row->cells  = realloc(row->cells, sizeof(char *) * capacity);
            FAIL_IF_NULL(row->cells);
        }
        row->cells[row->cell_count++] = cell;

        char *tab = strchr(cell, '\t');
        if (tab == NULL) {
            break;
        }
        *tab = '\0';
        cell = tab + 1;
    }
}

DataTable *DataTable_Load(const char *path) {
    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);

    DataTable *table = malloc(sizeof(DataTable));
    FAIL_IF_NULL(table);
    memset(table, 0, sizeof(DataTable));

    table->buffer = malloc(size + 1);
    FAIL_IF_NULL(table->buffer);
    MpqStream_Read(stream, table->buffer, 0, size);
    table->buffer[size] = '\0';
    MpqStream_Destroy(&stream);

    int capacity = 64;
    table->rows  = malloc(sizeof(DataTableRow) * capacity);
    FAIL_IF_NULL(table->rows);

    bool  is_header = true;
    char *line      = table->buffer;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        const size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        if (*line != '\0') {
            if (is_header) {
                DataTable__SplitRow(line, &table->header);
                is_header = false;
            } else {
                if (table->row_count == capacity) {
                    capacity    *= 2;
                    table->rows  = realloc(table->rows, sizeof(DataTableRow) * capacity);
                    FAIL_IF_NULL(table->rows);
                }
                DataTable__SplitRow(line, &table->rows[table->row_count++]);
            }
        }

        line = next;
    }

    LOG_DEBUG("Loaded data table '%s' (%d rows, %d columns)", path, table->row_count, table->header.cell_count);
    return table;
}

void DataTable_Destroy(DataTable **table) {
    if (table == NULL || *table == NULL) {
        return;
    }

    for (int i = 0; i < (*table)->row_count; i++) {
        free((*table)->rows[i].cells);
    }
    free((*table)->rows);
    free((*table)->header.cells);
    free((*table)->buffer);
    free(*table);
    *table = NULL;
}

int DataTable_GetRowCount(const DataTable *table) { return table->row_count; }

// Column names differ in case between game versions (e.g. "2Handed"), so compare case-insensitively.
static bool DataTable__NameEquals(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }

    return *a == *b;
}

int DataTable_GetColumnIndex(const DataTable *table, const char *column_name) {
    for (int i = 0; i < table->header.cell_count; i++) {
        if (DataTable__NameEquals(table->header.cells[i], column_name)) {
            return i;
        }
    }

    return -1;
}

const char *DataTable_GetString(const DataTable *table, const int row, const int column) {
    if (row < 0 || row >= table->row_count || column < 0 || column >= table->rows[row].cell_count) {
        return "";
    }

    return table->rows[row].cells[column];
}

int DataTable_GetInt(const DataTable *table, const int row, const int column) {
    return (int)strtol(DataTable_GetString(table, row, column), NULL, 10);
}

bool DataTable_HasValue(const DataTable *table, const int row, const int column) {
    return DataTable_GetString(table, row, column)[0] != '\0';
}
