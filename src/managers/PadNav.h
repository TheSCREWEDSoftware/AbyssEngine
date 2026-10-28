#ifndef ABYSS_PAD_NAV_H
#define ABYSS_PAD_NAV_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// A controller in the menus and panels. Every screen keeps working the way it does with the mouse: the controller
// moves the pointer and clicks for you.
//
//   Console style (the default; like Diablo III and the remaster on a controller): the D-pad or left stick jumps from
//   one button, box, tab or list row to the next in that direction, one inventory or stash cell at a time inside a
//   grid, a slider's value left and right; what's picked is framed in gold.
//   Cursor style: the left stick moves the pointer freely.
//   Both: the right stick moves the pointer freely; A clicks (held, it drags), X is the right button, B goes back
//   (Esc), LB / RB the previous / next tab, Start the menu (Esc). A box to type in gets a keyboard on the screen.
//
// In play (Single Player and the others, with no panel under the pointer) the controller goes to the controls'
// bindings instead (Options › Controls), and the right stick aims.

typedef enum PadNavKind {
    PAD_NAV_TARGET, // a button, a box, a tab, a row: jumped to as a whole
    PAD_NAV_SLIDER, // left / right change its value
    PAD_NAV_LIST,   // a list: up / down move a row (scrolling at its ends)
    PAD_NAV_GRID,   // an item grid: every direction moves one cell
    PAD_NAV_TEXT,   // a box to type in: A brings up the keyboard
} PadNavKind;

// Once a frame, after the events and before the scene
void PadNav_Update(void);
// After the scene is drawn: the gold frame round what's picked, and the keyboard
void PadNav_Draw(void);

// The widgets (and the panels that draw their own) say where they are, each frame they're on screen
void PadNav_Register(const SDL_Rect *rect);
void PadNav_RegisterKind(const SDL_Rect *rect, PadNavKind kind, int step_w, int step_h);
// A list a row at a time; at its top or bottom it scrolls while there's more that way (then leaves it)
void PadNav_RegisterList(const SDL_Rect *rect, int row_h, bool more_above, bool more_below);

// The game world has the pointer this frame (no panel or menu under it): the controller goes to the bindings
void PadNav_SetGameplay(bool gameplay);
// This frame the controller isn't the menus' (Options waiting for a button to bind)
void PadNav_Suspend(void);

bool PadNav_Active(void);      // the controller is what's being used
bool PadNav_FreeAim(void);     // the right stick moved the pointer lately (in play: don't aim for it)
int  PadNav_TakeTabStep(void); // LB (-1) / RB (+1) pressed this frame in a menu, 0 none; taken (once a frame)
int PadNav_SliderStep(const SDL_Rect *rect); // left (-1) / right (+1) this frame on the slider registered there, 0 none

#endif // ABYSS_PAD_NAV_H
