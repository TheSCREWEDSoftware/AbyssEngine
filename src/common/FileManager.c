#include "FileManager.h"
#include "../util/Crypto.h"
#include "AbyssConfiguration.h"
#include "Logging.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

typedef struct FileEntry {
    uint64_t    hash;
    struct MPQ *MPQ;
} FileEntry;

typedef struct FileManager {
    struct FileEntry *files;
    uint32_t          file_count;
    struct MPQ      **mpqs;
    uint32_t          mpq_count;
    char             *list_buffer; // concatenated (listfile) contents, built on first use
    const char      **list_files;
    int               list_count;
} FileManager;

FileManager *file_manager = NULL;

char *fix_path(const char *path) {
    assert(path != NULL);

    char *result = strdup(path);
    for (char *ch = result; *ch != '\0'; ch++) {
        *ch = (char)tolower((int)*ch);
        if (*ch == '/') {
            *ch = '\\';
        }
    }

    if (result[0] == '\\') {
        const int len = (int)strlen(result);
        for (int i = 0; i < len - 1; i++) {
            result[i] = result[i + 1];
        }
        result[len - 1] = '\0';
    }

    return result;
}

void FileManager_CreateSingleton(void) {
    assert(file_manager == NULL);

    file_manager = malloc(sizeof(FileManager));
    FAIL_IF_NULL(file_manager);

    memset(file_manager, 0, sizeof(FileManager));

    file_manager->mpqs  = calloc(0, sizeof(struct MPQ *));
    file_manager->files = calloc(0, sizeof(FileEntry));

    FAIL_IF_NULL(file_manager->mpqs);
    FAIL_IF_NULL(file_manager->files);

    for (size_t mpq_idx = 0; mpq_idx < AbyssConfiguration_GetMpqCount(); mpq_idx++) {
        FileManager_AddMpq(AbyssConfiguration_GetMpqFileName(mpq_idx));
    }
}

void FileManager_DestroySingleton(void) {
    assert(file_manager != NULL);

    free(file_manager->files);
    free(file_manager->list_files);
    free(file_manager->list_buffer);

    for (uint32_t mpq_idx = 0; mpq_idx < file_manager->mpq_count; mpq_idx++) {
        MPQ_Destroy(file_manager->mpqs[mpq_idx]);
    }

    free(file_manager->mpqs);
    free(file_manager);

    file_manager = NULL;
}

// Finds a file on disk. Windows and macOS (by default) ignore letter case, Linux does not: there the MPQs can be
// "d2data.mpq" while abyss.ini says "d2data.MPQ", so the file name is matched case-insensitively in its folder.
static bool FileManager__FindFile(const char *path, char *resolved, const size_t resolved_len) {
    snprintf(resolved, resolved_len, "%s", path);

    FILE *probe = fopen(path, "rb");
    if (probe != NULL) {
        fclose(probe);
        return true;
    }

#ifndef _WIN32
    const char *slash = strrchr(path, '/');
    char        folder[4096];
    snprintf(folder, sizeof(folder), "%.*s", slash != NULL ? (int)(slash - path) : 1, slash != NULL ? path : ".");
    const char *name = slash != NULL ? slash + 1 : path;

    DIR *dir = opendir(folder[0] != '\0' ? folder : "/");
    if (dir == NULL) {
        return false;
    }

    bool           found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        if (strcasecmp(entry->d_name, name) == 0) {
            const int written = snprintf(resolved, resolved_len, "%s/%.255s", folder, entry->d_name);
            found             = written > 0 && (size_t)written < resolved_len;
            if (found) {
                LOG_INFO("Using '%s' for '%s'", resolved, path);
            }
        }
    }
    closedir(dir);
    return found;
#else
    return false;
#endif
}

void FileManager_AddMpq(const char *mpq_path) {
    assert(file_manager != NULL);
    assert(mpq_path != NULL);

    char resolved[4096];
    if (!FileManager__FindFile(mpq_path, resolved, sizeof(resolved))) {
        LOG_WARN("Skipping MPQ '%s': file not found.", mpq_path);
        return;
    }

    file_manager->mpqs = realloc(file_manager->mpqs, sizeof(struct MPQ *) * ++file_manager->mpq_count);
    file_manager->mpqs[file_manager->mpq_count - 1] = MPQ_Load(resolved);
}

bool FileManager_FileExists(const char *file_path) {
    assert(file_manager != NULL);
    assert(file_path != NULL);

    char *path_fixed = fix_path(file_path);
    bool  result     = false;

    for (uint32_t mpq_idx = 0; mpq_idx < file_manager->mpq_count; mpq_idx++) {
        if (MPQ_FileExists(file_manager->mpqs[mpq_idx], path_fixed)) {
            result = true;
            break;
        }
    }

    free(path_fixed);
    return result;
}

static void (*file_read_hook)(void) = NULL;

void FileManager_SetReadHook(void (*hook)(void)) { file_read_hook = hook; }

MpqStream *FileManager_OpenFile(const char *file_path) {
    assert(file_manager != NULL);
    assert(file_path != NULL);

    if (file_read_hook != NULL) {
        file_read_hook();
    }

    char          *path_fixed = fix_path(file_path);
    const uint64_t file_hash  = crypto_hash_file_name(path_fixed);
    FileEntry     *file_entry = NULL;

    for (uint32_t i = 0; i < file_manager->file_count; i++) {
        if (file_manager->files[i].hash != file_hash) {
            continue;
        }
        file_entry = &file_manager->files[i];
        break;
    }

    if (file_entry == NULL) {
        for (uint32_t mpq_idx = 0; mpq_idx < file_manager->mpq_count; mpq_idx++) {
            if (!MPQ_FileExists(file_manager->mpqs[mpq_idx], path_fixed)) {
                continue;
            }

            file_manager->files = realloc(file_manager->files, sizeof(FileEntry) * ++file_manager->file_count);
            file_entry          = &file_manager->files[file_manager->file_count - 1];

            file_entry->hash = file_hash;
            file_entry->MPQ  = file_manager->mpqs[mpq_idx];

            // MPQs are listed in priority order (d2exp before d2data, like the original game): first match wins.
            break;
        }

        if (file_entry == NULL) {
            if (file_manager->mpq_count == 0) {
                LOG_FATAL("No Diablo II MPQs were found in '%s'.\n\nSet BasePath in %s to your Diablo II folder.",
                          AbyssConfiguration_GetBasePath(), AbyssConfiguration_GetIniPath());
            }
            LOG_FATAL("Could not find '%s'", file_path);
        }
    }

    MpqStream *MpqStream = MpqStream_Create(file_entry->MPQ, path_fixed);
    free(path_fixed);
    return MpqStream;
}

static int FileManager__ComparePaths(const void *a, const void *b) {
    const char *x = *(const char *const *)a;
    const char *y = *(const char *const *)b;

    while (*x != '\0' && tolower((unsigned char)*x) == tolower((unsigned char)*y)) {
        x++;
        y++;
    }

    return tolower((unsigned char)*x) - tolower((unsigned char)*y);
}

static void FileManager__BuildFileList(void) {
    size_t total    = 0;
    char **contents = calloc(file_manager->mpq_count > 0 ? file_manager->mpq_count : 1, sizeof(char *));
    FAIL_IF_NULL(contents);

    for (uint32_t i = 0; i < file_manager->mpq_count; i++) {
        if (!MPQ_FileExists(file_manager->mpqs[i], "(listfile)")) {
            continue;
        }

        MpqStream     *stream = MpqStream_Create(file_manager->mpqs[i], "(listfile)");
        const uint32_t size   = MpqStream_GetSize(stream);
        contents[i]           = malloc(size + 1);
        FAIL_IF_NULL(contents[i]);
        MpqStream_Read(stream, contents[i], 0, size);
        contents[i][size] = '\0';
        MpqStream_Destroy(&stream);
        total += size + 1;
    }

    file_manager->list_buffer = malloc(total + 1);
    FAIL_IF_NULL(file_manager->list_buffer);
    file_manager->list_buffer[0] = '\0';

    size_t used = 0;
    for (uint32_t i = 0; i < file_manager->mpq_count; i++) {
        if (contents[i] != NULL) {
            const size_t len = strlen(contents[i]);
            memcpy(file_manager->list_buffer + used, contents[i], len);
            used                              += len;
            file_manager->list_buffer[used++]  = '\n';
            free(contents[i]);
        }
    }
    file_manager->list_buffer[used] = '\0';
    free(contents);

    // Split into lines (in place)
    int capacity             = 1024;
    file_manager->list_files = malloc(sizeof(char *) * capacity);
    file_manager->list_count = 0;
    FAIL_IF_NULL(file_manager->list_files);

    for (char *line = file_manager->list_buffer; line != NULL && *line != '\0';) {
        char *next = strpbrk(line, "\r\n");
        if (next != NULL) {
            *next++ = '\0';
            while (*next == '\r' || *next == '\n') {
                next++;
            }
        }

        if (*line != '\0') {
            if (file_manager->list_count == capacity) {
                capacity                 *= 2;
                file_manager->list_files  = realloc(file_manager->list_files, sizeof(char *) * capacity);
                FAIL_IF_NULL(file_manager->list_files);
            }
            file_manager->list_files[file_manager->list_count++] = line;
        }

        line = next;
    }

    // Sort and drop duplicates (the same file can be listed by several MPQs, in different letter case)
    qsort(file_manager->list_files, file_manager->list_count, sizeof(char *), FileManager__ComparePaths);
    int unique = 0;
    for (int i = 0; i < file_manager->list_count; i++) {
        if (unique == 0 ||
            FileManager__ComparePaths(&file_manager->list_files[unique - 1], &file_manager->list_files[i]) != 0) {
            file_manager->list_files[unique++] = file_manager->list_files[i];
        }
    }
    file_manager->list_count = unique;

    LOG_INFO("File list: %d files known from the MPQ list files", file_manager->list_count);
}

int FileManager_GetFileList(const char *const **files) {
    assert(file_manager != NULL);

    if (file_manager->list_files == NULL) {
        FileManager__BuildFileList();
    }

    *files = file_manager->list_files;
    return file_manager->list_count;
}
