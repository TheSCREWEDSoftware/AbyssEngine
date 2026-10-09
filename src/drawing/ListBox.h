#ifndef ABYSS_LIST_BOX_H
#define ABYSS_LIST_BOX_H

#include <stdbool.h>

// A scrollable, single-selection list of text rows. Scroll with the mouse wheel, select with a click.
typedef struct ListBox ListBox;

ListBox *ListBox_Create(const char *font_path, int x, int y, int width, int visible_rows, int row_height);
void     ListBox_Destroy(ListBox **list_box);
void     ListBox_SetPosition(ListBox *list_box, int x, int y);
void     ListBox_SetItems(ListBox *list_box, const char *const *items, int item_count);
int      ListBox_GetSelectedIndex(const ListBox *list_box); // -1 when empty
void     ListBox_SetSelectedIndex(ListBox *list_box, int index);

// Processes mouse input. Returns true when the selection changed.
bool ListBox_Update(ListBox *list_box);
void ListBox_Draw(const ListBox *list_box);

#endif // ABYSS_LIST_BOX_H
