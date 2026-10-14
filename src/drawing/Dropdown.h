#ifndef ABYSS_DROPDOWN_H
#define ABYSS_DROPDOWN_H

#include <stdbool.h>
#include <stdint.h>

// A box showing one of a list of choices; click it to open the list, click a choice to pick it (or outside to close).
// While open its list covers what's below: draw it last (Dropdown_DrawList), and skip other widgets' input
// (Dropdown_IsOpen).
typedef struct Dropdown Dropdown;

Dropdown *Dropdown_Create(int x, int y, int width, const char *const *choices, int count);
void      Dropdown_Destroy(Dropdown **dropdown);
void      Dropdown_SetChoices(Dropdown *dropdown, const char *const *choices, int count);
void      Dropdown_SetSelected(Dropdown *dropdown, int index);
void      Dropdown_SetMark(Dropdown *dropdown, int index, uint8_t r, uint8_t g, uint8_t b); // a colour stripe by it
int       Dropdown_GetSelected(const Dropdown *dropdown);
bool      Dropdown_IsOpen(const Dropdown *dropdown);
bool      Dropdown_Update(Dropdown *dropdown); // true when another choice was picked
void      Dropdown_Draw(const Dropdown *dropdown);
void      Dropdown_DrawList(const Dropdown *dropdown); // the open list, on top of everything

#endif // ABYSS_DROPDOWN_H
