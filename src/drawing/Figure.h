#ifndef ABYSS_FIGURE_H
#define ABYSS_FIGURE_H

#include "../types/COF.h"
#include <stdbool.h>
#include <stdint.h>

// An animated, layered figure (monster, character or object) for one animation mode, built from a COF file and
// its DCC layers:  data/global/<base>/<token>/cof/<token><mode><weapon>.cof
//                  data/global/<base>/<token>/<layer>/<token><layer><armor><mode><weapon>.dcc
typedef struct FigureSpec {
    const char *base;                          // "monsters", "chars" or "objects"
    char        token[8];                      // e.g. "zm"
    char        weapon_class[4];               // e.g. "hth"
    char        armor[COF_COMPONENT_COUNT][4]; // per layer, e.g. "lit"; empty = don't draw that layer
    uint16_t    tint[COF_COMPONENT_COUNT];     // per layer: an item's colour, 1 + shift x 32 + colour (0: none)
} FigureSpec;

typedef struct Figure Figure;

bool    Figure_ModeExists(const FigureSpec *spec, const char *mode);
Figure *Figure_Create(const FigureSpec *spec, const char *mode, const char *palette_name); // NULL if no COF
void    Figure_Destroy(Figure **figure);
int     Figure_GetDirectionCount(const Figure *figure);
int     Figure_GetFramesPerDirection(const Figure *figure);
int     Figure_GetFrameMilliseconds(const Figure *figure);
// Bounding box of all frames, relative to the anchor point (the figure's feet).
void Figure_GetBounds(const Figure *figure, int *xmin, int *ymin, int *xmax, int *ymax);
// Draws one frame with its anchor at (x, y).
void Figure_Draw(Figure *figure, int direction, int frame, int x, int y, float scale);
// See-through drawing (255 = opaque). Figures are shared, so set it back after drawing.
void Figure_SetAlpha(Figure *figure, uint8_t alpha);

#endif // ABYSS_FIGURE_H
