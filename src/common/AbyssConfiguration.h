#ifndef ABYSS_CONFIG_H
#define ABYSS_CONFIG_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void        AbyssConfiguration_LoadSingleton(const char *file_path);
void        AbyssConfiguration_DestroySingleton(void);
void        AbyssConfiguration_AddMpq(const char *mpq_file);
const char *AbyssConfiguration_GetLocale(void);
size_t      AbyssConfiguration_GetMpqCount(void);
const char *AbyssConfiguration_GetMpqFileName(size_t index);
float       AbyssConfiguration_GetInitialScale(void);
void        AbyssConfiguration_GetWindowSize(int *width, int *height); // Resolution, or 800x600 * InitialScale
const char *AbyssConfiguration_GetScaleQuality(void);
bool        AbyssConfiguration_GetFullScreen(void);
void        AbyssConfiguration_SetFullScreen(bool fullscreen);
float       AbyssConfiguration_GetMasterVolume(void);
float       AbyssConfiguration_GetMusicVolume(void);
float       AbyssConfiguration_GetSfxVolume(void);
float       AbyssConfiguration_GetUiVolume(void);
bool        AbyssConfiguration_GetSkipIntroMovies(void);
const char *AbyssConfiguration_GetConfigFolder(void); // folder holding abyss.ini (no trailing separator)

// Options screen: change settings and write them back into abyss.ini (everything else in it, comments too, stays)
void AbyssConfiguration_SetWindowSize(int width, int height); // 0, 0: 800x600 times InitialScale
void AbyssConfiguration_SetScaleQuality(const char *quality); // nearest, linear, best
void AbyssConfiguration_SetVolumes(float master, float music, float sfx, float ui);
bool AbyssConfiguration_Save(void);

#endif // ABYSS_CONFIG_H
