#ifndef ABYSS_SCENE_CINEMATICS_H
#define ABYSS_SCENE_CINEMATICS_H

#include "Scene.h"

// Main menu › Cinematics: the game's films (data/local/video/*.bik: the intro, the act openings and the endings, the
// expansion's own). Click one to watch it (Esc skips); ones the game's files here don't have are greyed out.

DESCRIBE_SCENE_CALLBACKS(Cinematics);

void *Cinematics_Create(void);
void  Cinematics_Render(void *scene_ref);
void  Cinematics_Update(void *scene_ref, uint64_t delta);
void  Cinematics_Free(void **scene_ref);

#endif // ABYSS_SCENE_CINEMATICS_H
