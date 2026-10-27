#ifndef ABYSS_MISSILESPRITE_H
#define ABYSS_MISSILESPRITE_H

#include <stdbool.h>

// A missile's pictures (data/global/missiles/<CelFile>.dcc) in an act's colours, kept once loaded. Glowing missiles
// (Missiles.txt Trans) are drawn added onto what's under them, like the game's fire and lightning.

typedef struct MissileSprite MissileSprite;

// NULL if no such file. A cel file starting with '/' is a whole path (overlays: data/global/overlays/<file>.dcc)
MissileSprite *MissileSprite_Get(const char *cel_file, const char *palette_name, bool glow);
int            MissileSprite_Directions(const MissileSprite *sprite);
int            MissileSprite_Frames(const MissileSprite *sprite);
// Draws a frame with its anchor at (x, y) on screen
void MissileSprite_Draw(MissileSprite *sprite, int direction, int frame, int x, int y, float scale);
void MissileSprite_ReleaseAll(void);

#endif // ABYSS_MISSILESPRITE_H
