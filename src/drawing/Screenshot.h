#ifndef ABYSS_SCREENSHOT_H
#define ABYSS_SCREENSHOT_H

#include <stdbool.h>

// Saves what's been drawn so far this frame as a picture in the "screenshots" folder next to abyss.ini
// (Abyss_2026-10-13_21-30-05.bmp). Returns false if it couldn't.
bool Screenshot_Save(char *saved_path, int size);

#endif // ABYSS_SCREENSHOT_H
