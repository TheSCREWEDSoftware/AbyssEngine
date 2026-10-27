#ifndef ABYSS_STATEOVERLAYS_H
#define ABYSS_STATEOVERLAYS_H

#include <stdbool.h>

// What a state (states.txt: a buff, an aura, a curse) looks like on whoever has it: up to four overlays
// (overlay1..4, overlay.txt): animations from data/global/overlays, in front of the figure or behind it (PreDraw),
// glowing (Trans) or not.

#define STATE_OVERLAYS_MAX 4

typedef struct StateOverlay {
    char file[40]; // data/global/overlays/<file>.dcc
    int  x;        // offset on screen
    int  y;
    bool behind;
    bool glow;
} StateOverlay;

// The overlays of the state named (states.txt "state"); the count (0: none, or no such state)
int StateOverlays_For(const char *state, StateOverlay *out, int max);

void StateOverlays_Release(void);

#endif // ABYSS_STATEOVERLAYS_H
