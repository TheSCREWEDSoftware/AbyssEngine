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

#endif // ABYSS_FILEMAN_H
