#include "StringTable.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_TABLE_HEADER_SIZE     21
#define STRING_TABLE_HASH_ENTRY_SIZE 17
#define STRING_TABLE_MAX_FILES       3

typedef struct StringEntry {
    const char *key;
    const char *value;
} StringEntry;

typedef struct StringFile {
    uint8_t     *buffer;
    StringEntry *entries; // sorted by key
    uint32_t     entry_count;
} StringFile;

typedef struct StringTables {
    StringFile files[STRING_TABLE_MAX_FILES]; // highest priority first
    int        file_count;
} StringTables;

static StringTables *string_tables = NULL;

static uint16_t StringTable__ReadU16(const uint8_t *data) { return (uint16_t)(data[0] | (data[1] << 8)); }

static uint32_t StringTable__ReadU32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int StringTable__CompareEntries(const void *a, const void *b) {
    return strcmp(((const StringEntry *)a)->key, ((const StringEntry *)b)->key);
}

static bool StringTable__LoadFile(const char *path, StringFile *file) {
    if (!FileManager_FileExists(path)) {
        return false;
    }

    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    if (size < STRING_TABLE_HEADER_SIZE) {
        MpqStream_Destroy(&stream);
        return false;
    }

    file->buffer = malloc(size + 1);
    FAIL_IF_NULL(file->buffer);
    MpqStream_Read(stream, file->buffer, 0, size);
    file->buffer[size] = '\0';
    MpqStream_Destroy(&stream);

    const uint16_t element_count   = StringTable__ReadU16(file->buffer + 2);
    const uint32_t hash_table_size = StringTable__ReadU32(file->buffer + 4);
    const uint32_t hash_start      = STRING_TABLE_HEADER_SIZE + ((uint32_t)element_count * 2);

    if (hash_start + ((uint64_t)hash_table_size * STRING_TABLE_HASH_ENTRY_SIZE) > size) {
        LOG_WARN("String table '%s' is truncated, ignoring it.", path);
        free(file->buffer);
        file->buffer = NULL;
        return false;
    }

    file->entries     = calloc(hash_table_size > 0 ? hash_table_size : 1, sizeof(StringEntry));
    file->entry_count = 0;
    FAIL_IF_NULL(file->entries);

    for (uint32_t i = 0; i < hash_table_size; i++) {
        const uint8_t *entry      = file->buffer + hash_start + (i * STRING_TABLE_HASH_ENTRY_SIZE);
        const uint8_t  active     = entry[0];
        const uint32_t key_offset = StringTable__ReadU32(entry + 7);
        const uint32_t str_offset = StringTable__ReadU32(entry + 11);

        if (active == 0 || key_offset >= size || str_offset >= size) {
            continue;
        }

        file->entries[file->entry_count].key   = (const char *)file->buffer + key_offset;
        file->entries[file->entry_count].value = (const char *)file->buffer + str_offset;
        file->entry_count++;
    }

    qsort(file->entries, file->entry_count, sizeof(StringEntry), StringTable__CompareEntries);
    LOG_DEBUG("Loaded %u strings from '%s'", file->entry_count, path);
    return true;
}

void StringTable_InitializeSingleton(const char *language) {
    assert(string_tables == NULL);

    string_tables = malloc(sizeof(StringTables));
    FAIL_IF_NULL(string_tables);
    memset(string_tables, 0, sizeof(StringTables));

    static const char *file_names[STRING_TABLE_MAX_FILES] = {"patchstring", "expansionstring", "string"};

    for (int i = 0; i < STRING_TABLE_MAX_FILES; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/data/local/lng/%s/%s.tbl", language, file_names[i]);

        if (StringTable__LoadFile(path, &string_tables->files[string_tables->file_count])) {
            string_tables->file_count++;
        }
    }
}

void StringTable_DestroySingleton(void) {
    if (string_tables == NULL) {
        return;
    }

    for (int i = 0; i < string_tables->file_count; i++) {
        free(string_tables->files[i].entries);
        free(string_tables->files[i].buffer);
    }

    free(string_tables);
    string_tables = NULL;
}

const char *StringTable_Find(const char *key) {
    if (string_tables == NULL || key == NULL || key[0] == '\0') {
        return NULL;
    }

    const StringEntry needle = {.key = key, .value = NULL};
    for (int i = 0; i < string_tables->file_count; i++) {
        const StringFile  *file = &string_tables->files[i];
        const StringEntry *found =
            bsearch(&needle, file->entries, file->entry_count, sizeof(StringEntry), StringTable__CompareEntries);
        if (found != NULL) {
            return found->value;
        }
    }

    return NULL;
}

const char *StringTable_Get(const char *key) {
    const char *value = StringTable_Find(key);
    return value != NULL ? value : key;
}
