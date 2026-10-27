#ifndef ABYSS_FILEMAN_H
#define ABYSS_FILEMAN_H

#include "../types/MPQ.h"
#include "MpqStream.h"
#include <stdbool.h>

void       FileManager_CreateSingleton(void);
void       FileManager_DestroySingleton(void);
void       FileManager_AddMpq(const char *mpq_path);
bool       FileManager_FileExists(const char *file_path);
MpqStream *FileManager_OpenFile(const char *file_path);
// Every file named in the loaded MPQs' (listfile), sorted, without duplicates. Returns the count.
int FileManager_GetFileList(const char *const **files);
// Called whenever a file is opened (the loading screen uses it to keep animating during long loads). NULL to clear.
void FileManager_SetReadHook(void (*hook)(void));
// Lenient (text mode): a file that isn't anywhere opens empty instead of stopping the program, so the game runs from
// an extracted data folder (DataFolder) alone, without the pictures and sounds
void FileManager_SetLenient(bool lenient);

#endif // ABYSS_FILEMAN_H
