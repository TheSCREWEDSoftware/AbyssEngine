#ifndef ABYSS_SCENE_TESTING_GROUNDS_H
#define ABYSS_SCENE_TESTING_GROUNDS_H

#include "Scene.h"

// Testing Grounds (debug): walk a character around a map (the default arena, your own "testing_grounds" map, or
// the map being edited), drop monsters into it and, with combat on, fight them. Implemented in SceneRoam.c together
// with Free Roam.
DESCRIBE_SCENE_CALLBACKS(TestingGrounds);

void *TestingGrounds_Create(void);
void  TestingGrounds_Render(void *scene_ref);
void  TestingGrounds_Update(void *scene_ref, uint64_t delta);
void  TestingGrounds_Free(void **scene_ref);

// The arena used when there's no saved "testing_grounds" map (also "New > Testing Grounds" in the editor).
struct Map *TestingGrounds_CreateDefaultMap(void);

#endif // ABYSS_SCENE_TESTING_GROUNDS_H
