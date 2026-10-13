#ifndef ABYSS_SCENE_OPTIONS_H
#define ABYSS_SCENE_OPTIONS_H

#include "Scene.h"

// Options (from the main menu): Video | Audio | Gameplay | Controls. Video and audio are saved in abyss.ini,
// gameplay and controls in options.ini (see game/Settings.h). OK saves, Cancel puts everything back.
DESCRIBE_SCENE_CALLBACKS(Options);

void *Options_Create(void);
void  Options_Render(void *scene_ref);
void  Options_Update(void *scene_ref, uint64_t delta);
void  Options_Free(void **scene_ref);

#endif // ABYSS_SCENE_OPTIONS_H
