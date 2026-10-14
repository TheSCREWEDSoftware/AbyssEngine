#ifndef ABYSS_SCENE_OPTIONS_H
#define ABYSS_SCENE_OPTIONS_H

#include "Scene.h"
#include <stdbool.h>

// Options (from the main menu): Video | Audio | Gameplay | Controls. Video and audio are saved in abyss.ini,
// gameplay and controls in options.ini (see game/Settings.h). OK saves, Cancel puts everything back.
DESCRIBE_SCENE_CALLBACKS(Options);

void *Options_Create(void);
void  Options_Render(void *scene_ref);
void  Options_Update(void *scene_ref, uint64_t delta);
void  Options_Free(void **scene_ref);

// The same Options as a panel over another scene (the game's menu): Update returns false once OK or Cancel closed it
typedef struct SceneOptions OptionsPanel;
OptionsPanel               *OptionsPanel_Create(void);
void                        OptionsPanel_Destroy(OptionsPanel **panel);
bool                        OptionsPanel_Update(OptionsPanel *panel);
void                        OptionsPanel_Draw(OptionsPanel *panel);

#endif // ABYSS_SCENE_OPTIONS_H
