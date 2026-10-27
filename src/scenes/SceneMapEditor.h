#ifndef ABYSS_SCENE_MAP_EDITOR_H
#define ABYSS_SCENE_MAP_EDITOR_H

#include "Scene.h"

// Map Editor (debug): open the game's own areas (DS1) or start a blank map from an area's tile set, paint floor,
// wall and shadow tiles, place monsters/NPCs/objects, save and load your own maps (.abm), and play-test them in
// the Testing Grounds.
DESCRIBE_SCENE_CALLBACKS(MapEditor);

void *MapEditor_Create(void);
void  MapEditor_Render(void *scene_ref);
void  MapEditor_Update(void *scene_ref, uint64_t delta);
void  MapEditor_Free(void **scene_ref);

// AbyssEngine --editor [map]: the editor on its own (no menus; leaving it quits), opening a map first if one's named:
// one of yours ("mymap" or a path to an .abm) or one of the game's (an MPQ path, "act1/town/townN1.ds1")
void MapEditor_SetStandalone(const char *map);

#endif // ABYSS_SCENE_MAP_EDITOR_H
