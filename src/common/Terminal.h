#ifndef ABYSS_TERMINAL_H
#define ABYSS_TERMINAL_H

#include <stdbool.h>

// The terminal for text mode (AbyssEngine --text): keys straight away (no Enter, no echo), the screen kept clear
// for the game, its size. Put back as it was by Terminal_End (and on exit).

bool Terminal_Begin(void);
void Terminal_End(void);
int  Terminal_ReadKey(void); // the next key pressed, -1 if none; arrow keys come as 'w', 'a', 's', 'd'
void Terminal_Size(int *cols, int *rows);
void Terminal_Write(const char *text, int length);

#endif // ABYSS_TERMINAL_H
