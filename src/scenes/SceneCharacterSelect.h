#ifndef ABYSS_SCENE_CHARACTER_SELECT_H
#define ABYSS_SCENE_CHARACTER_SELECT_H

#include "Scene.h"

// Single Player: the saved characters. Create a new one, delete one, or pick one and play.
DESCRIBE_SCENE_CALLBACKS(CharacterSelect);

void *CharacterSelect_Create(void);
void  CharacterSelect_Render(void *scene_ref);
void  CharacterSelect_Update(void *scene_ref, uint64_t delta);
void  CharacterSelect_Free(void **scene_ref);

#endif // ABYSS_SCENE_CHARACTER_SELECT_H
