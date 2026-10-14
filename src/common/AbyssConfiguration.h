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
float       AbyssConfiguration_GetSpeechVolume(void); // NPCs and quests, once speech plays
void        AbyssConfiguration_SetSpeechVolume(float volume);
bool        AbyssConfiguration_GetSkipIntroMovies(void);
const char *AbyssConfiguration_GetConfigFolder(void); // folder holding abyss.ini (no trailing separator)

// Options screen: change settings and write them back into abyss.ini (everything else in it, comments too, stays)
void AbyssConfiguration_SetWindowSize(int width, int height); // 0, 0: 800x600 times InitialScale
void AbyssConfiguration_SetScaleQuality(const char *quality); // nearest, linear, best
void AbyssConfiguration_SetVolumes(float master, float music, float sfx, float ui);
bool AbyssConfiguration_Save(void);

// Frames drawn a second at most (0: no limit; 25: the game's own). Multiplayer never goes above 60.
int  AbyssConfiguration_GetFrameRate(bool multiplayer);
void AbyssConfiguration_SetFrameRate(int rate);
// Set while playing with others (Multiplayer), which keeps the frame rate at 60 or below
void AbyssConfiguration_SetOnline(bool online);
bool AbyssConfiguration_IsOnline(void);
// The picture: 0..100 each, 50 = the game's own (applied to each frame, see drawing/Picture.h)
void AbyssConfiguration_GetPicture(int *brightness, int *contrast, int *gamma);
void AbyssConfiguration_SetPicture(int brightness, int contrast, int gamma);
// The mouse: pointer speed in percent across and up/down (one speed when match_axes), and the wheel's speed
void AbyssConfiguration_GetMouse(int *speed_x, int *speed_y, bool *match_axes, int *wheel_speed);
void AbyssConfiguration_SetMouse(int speed_x, int speed_y, bool match_axes, int wheel_speed);

#endif // ABYSS_CONFIG_H
