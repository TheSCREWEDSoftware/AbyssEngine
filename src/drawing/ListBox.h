#ifndef ABYSS_LIST_BOX_H
#define ABYSS_LIST_BOX_H

#include <stdbool.h>
#include <stdint.h>

// A scrollable, single-selection list of text rows. Scroll with the mouse wheel or the scrollbar (click or drag),
// select with a click.
typedef struct ListBox ListBox;

ListBox *ListBox_Create(const char *font_path, int x, int y, int width, int visible_rows, int row_height);
void     ListBox_Destroy(ListBox **list_box);
void     ListBox_SetPosition(ListBox *list_box, int x, int y);
void     ListBox_SetVisibleRows(ListBox *list_box, int rows); // at most the row count it was created with
bool     ListBox_IsScrollable(const ListBox *list_box);
bool     ListBox_IsAtEnd(const ListBox *list_box);
void     ListBox_ScrollToStart(ListBox *list_box);
void     ListBox_ScrollToEnd(ListBox *list_box);
void     ListBox_SetBackground(ListBox *list_box, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void     ListBox_SetItems(ListBox *list_box, const char *const *items, int item_count);
int      ListBox_GetSelectedIndex(const ListBox *list_box); // -1 when empty
void     ListBox_SetSelectedIndex(ListBox *list_box, int index);

// Processes mouse input. Returns true when the selection changed.
bool ListBox_Update(ListBox *list_box);
void ListBox_Draw(const ListBox *list_box);

#endif // ABYSS_LIST_BOX_H
