#ifndef ABYSS_DCC_H
#define ABYSS_DCC_H

#include <stdbool.h>
#include <stdint.h>

// DCC: the compressed animation format used for monsters, characters, objects and missiles.
// A file holds N directions x M frames. Every frame is decoded to an 8-bit palette-indexed bitmap; index 0 is
// transparent. (x, y) is the frame's top-left corner relative to the animation's anchor point (the feet).
typedef struct DCCFrame {
    int      width;
    int      height;
    int      x;
    int      y;
    uint8_t *pixels; // width * height palette indices, NULL for an empty frame
} DCCFrame;

typedef struct DCC DCC;

DCC *DCC_Load(const char *path); // NULL when the file is missing or malformed
// Some composite layers (e.g. Mephisto) are stored as DC6; load one into the same frame structure.
DCC            *DCC_LoadFromDC6(const char *path);
void            DCC_Destroy(DCC **dcc);
int             DCC_GetDirectionCount(const DCC *dcc);
int             DCC_GetFramesPerDirection(const DCC *dcc);
const DCCFrame *DCC_GetFrame(const DCC *dcc, int direction, int frame);

#endif // ABYSS_DCC_H
