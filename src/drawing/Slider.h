#ifndef ABYSS_SLIDER_H
#define ABYSS_SLIDER_H

#include <stdbool.h>

// A horizontal slider for a whole number in a range (volumes 0-100): click or drag along it, or use the wheel over it.
typedef struct Slider Slider;

Slider *Slider_Create(int x, int y, int width, int minimum, int maximum);
void    Slider_Destroy(Slider **slider);
void    Slider_SetValue(Slider *slider, int value);
int     Slider_GetValue(const Slider *slider);
bool    Slider_Update(Slider *slider); // true when the value changed
void    Slider_Draw(const Slider *slider);

#endif // ABYSS_SLIDER_H
