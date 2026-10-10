#ifndef ABYSS_SCENE_CHARACTER_CREATE_H
#define ABYSS_SCENE_CHARACTER_CREATE_H

#include "Scene.h"

// Single Player: "Select Hero Class" - pick a class round the campfire, name the character and start playing.
DESCRIBE_SCENE_CALLBACKS(CharacterCreate);

void *CharacterCreate_Create(void);
void  CharacterCreate_Render(void *scene_ref);
void  CharacterCreate_Update(void *scene_ref, uint64_t delta);
void  CharacterCreate_Free(void **scene_ref);

#endif // ABYSS_SCENE_CHARACTER_CREATE_H
