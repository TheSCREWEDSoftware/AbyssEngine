#ifndef ABYSS_PICTURE_H
#define ABYSS_PICTURE_H

#include <stdbool.h>

// The picture settings, live: each frame is drawn to an 800x600 picture first, then put on the window, stretched with
// the Scaling setting and adjusted with Brightness, Contrast and Gamma on the way (blending on the graphics card, so
// changes show straight away, also while playing). Without render targets it draws straight to the window as before.
void Picture_Initialize(void);
void Picture_Finalize(void);
void Picture_BeginFrame(void); // before anything's drawn
void Picture_EndFrame(void);   // after everything (cursor included), before SDL_RenderPresent

#endif // ABYSS_PICTURE_H
