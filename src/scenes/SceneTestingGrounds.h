#ifndef ABYSS_SCENE_TESTING_GROUNDS_H
#define ABYSS_SCENE_TESTING_GROUNDS_H

#include "Scene.h"
#include <stdbool.h>
#include <stddef.h>

// Testing Grounds (debug): walk a character around a map (the default arena, your own "testing_grounds" map, or
// the map being edited), drop monsters into it and, with combat on, fight them. Implemented in SceneRoam.c together
// with Free Roam.
DESCRIBE_SCENE_CALLBACKS(TestingGrounds);

void *TestingGrounds_Create(void);
void  TestingGrounds_Render(void *scene_ref);
void  TestingGrounds_Update(void *scene_ref, uint64_t delta);
void  TestingGrounds_Free(void **scene_ref);

// The built-in arena: the original of the "testing_grounds" map (also "New > Arena" in the editor). It lives in the
// program, so it can't be changed: the map in the maps folder is the one you edit, and it can be reset to this.
struct Map *TestingGrounds_CreateDefaultMap(void);

// maps/testing_grounds.abm: Testing Grounds plays it, the Map Editor opens and saves it like any other map
void TestingGrounds_MapPath(char *path, size_t size);
void TestingGrounds_EnsureMapFile(void); // writes the original there if it isn't there yet
bool TestingGrounds_ResetMapFile(void);  // writes the original over it ("Reset to original")

#endif // ABYSS_SCENE_TESTING_GROUNDS_H
