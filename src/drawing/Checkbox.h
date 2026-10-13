#ifndef ABYSS_CHECKBOX_H
#define ABYSS_CHECKBOX_H

#include <stdbool.h>

// A box you tick, with its text to the right; clicking the box or the text toggles it.
typedef struct Checkbox Checkbox;

Checkbox *Checkbox_Create(int x, int y, const char *text);
void      Checkbox_Destroy(Checkbox **checkbox);
void      Checkbox_SetChecked(Checkbox *checkbox, bool checked);
bool      Checkbox_IsChecked(const Checkbox *checkbox);
void      Checkbox_SetEnabled(Checkbox *checkbox, bool enabled);
bool      Checkbox_Update(Checkbox *checkbox); // true when it was toggled
void      Checkbox_Draw(const Checkbox *checkbox);

#endif // ABYSS_CHECKBOX_H
