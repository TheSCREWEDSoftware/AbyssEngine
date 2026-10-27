#ifndef ABYSS_SCENE_CREDITS_H
#define ABYSS_SCENE_CREDITS_H

#include "Scene.h"

// Main menu › Credits: the game's own credits (data/local/UI/<language>/ExpansionCredits.txt, or credits.txt for the
// original game) rolling up over its credits background, headings ("*Game Design:") in gold. Any key or click goes
// back to the main menu.

DESCRIBE_SCENE_CALLBACKS(Credits);

void *Credits_Create(void);
void  Credits_Render(void *scene_ref);
void  Credits_Update(void *scene_ref, uint64_t delta);
void  Credits_Free(void **scene_ref);

#endif // ABYSS_SCENE_CREDITS_H
