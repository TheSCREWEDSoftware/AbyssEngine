#ifndef ABYSS_SCENE_FREE_ROAM_H
#define ABYSS_SCENE_FREE_ROAM_H

#include "Scene.h"

// Free Roam (debug): start in the Rogue Encampment and teleport between the game's fixed outdoor areas (the five
// towns, Tristram, the Monastery Gate and cloisters) with their NPCs in place. Switch character, and turn combat
// between you and the NPCs/monsters on or off. Shares its engine with the Testing Grounds (SceneRoam.c).
DESCRIBE_SCENE_CALLBACKS(FreeRoam);

void *FreeRoam_Create(void);
void  FreeRoam_Render(void *scene_ref);
void  FreeRoam_Update(void *scene_ref, uint64_t delta);
void  FreeRoam_Free(void **scene_ref);

#endif // ABYSS_SCENE_FREE_ROAM_H
