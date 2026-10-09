#ifndef ABYSS_DATA_TABLE_H
#define ABYSS_DATA_TABLE_H

#include <stdbool.h>
#include <stdint.h>

// Reader for the game's tab separated "excel" tables (/data/global/excel/*.txt).
// The first row holds the column names; cells are looked up by row and column index.
typedef struct DataTable DataTable;

DataTable  *DataTable_Load(const char *path);
void        DataTable_Destroy(DataTable **table);
int         DataTable_GetRowCount(const DataTable *table);
int         DataTable_GetColumnIndex(const DataTable *table, const char *column_name); // -1 when missing
const char *DataTable_GetString(const DataTable *table, int row, int column);          // "" when missing
int         DataTable_GetInt(const DataTable *table, int row, int column);             // 0 when missing/empty
bool        DataTable_HasValue(const DataTable *table, int row, int column);

#endif // ABYSS_DATA_TABLE_H
