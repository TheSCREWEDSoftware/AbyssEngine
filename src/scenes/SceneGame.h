#ifndef ABYSS_SCENE_GAME_H
#define ABYSS_SCENE_GAME_H

#include "Scene.h"

// Single Player: the game itself, with the character from Character_Current(). Starts in the town of the act the
// character was last in; levels, experience and stats are kept and saved. Shares its engine with Free Roam
// (SceneRoam.c).
DESCRIBE_SCENE_CALLBACKS(Game);

void *Game_Create(void);
void  Game_Render(void *scene_ref);
void  Game_Update(void *scene_ref, uint64_t delta);
void  Game_Free(void **scene_ref);

#endif // ABYSS_SCENE_GAME_H
