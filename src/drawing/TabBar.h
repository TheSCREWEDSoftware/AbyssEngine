#ifndef ABYSS_TAB_BAR_H
#define ABYSS_TAB_BAR_H

#include <stdbool.h>

// A row of text tabs (one selected) that share a fixed width in proportion to their text.
typedef struct TabBar TabBar;

#define TAB_BAR_HEIGHT 20

TabBar *TabBar_Create(const char *const *names, int count, int selected);
void    TabBar_Destroy(TabBar **tab_bar);
int     TabBar_GetSelected(const TabBar *tab_bar);
// Returns true when a click selected a different tab.
bool TabBar_Update(TabBar *tab_bar, int x, int y, int width);
void TabBar_Draw(const TabBar *tab_bar, int x, int y, int width);

#endif // ABYSS_TAB_BAR_H
