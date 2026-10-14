#ifndef ABYSS_SLIDER_H
#define ABYSS_SLIDER_H

#include <stdbool.h>

// A horizontal slider for a whole number in a range (volumes 0-100): click or drag along it, or use the wheel over it.
typedef struct Slider Slider;

Slider *Slider_Create(int x, int y, int width, int minimum, int maximum);
void    Slider_Destroy(Slider **slider);
void    Slider_SetValue(Slider *slider, int value);
int     Slider_GetValue(const Slider *slider);
// Every slider shows its minimum and maximum under its ends and has a box to type the value in, right of it.
// Shown as value / divisor with that many decimals (divisor 100, 2 decimals: 125 shows as "1.25"; typing "1.5" sets
// 150), then the suffix ("%", "x").
void Slider_SetFormat(Slider *slider, int divisor, int decimals, const char *suffix);
void Slider_SetEndNames(Slider *slider, const char *minimum, const char *maximum); // words at the ends instead
void Slider_SetTypable(Slider *slider, bool typable);                              // no box (a slider of named steps)
bool Slider_IsTyping(const Slider *slider);                                        // its box has the keyboard
bool Slider_Update(Slider *slider);                                                // true when the value changed
void Slider_Draw(const Slider *slider);

#endif // ABYSS_SLIDER_H
