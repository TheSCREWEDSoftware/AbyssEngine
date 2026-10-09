#include "ListBox.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../managers/InputManager.h"
#include "Label.h"
#include <stdlib.h>
#include <string.h>

#define LIST_BOX_TEXT_PADDING_X  6
#define LIST_BOX_SCROLLBAR_WIDTH 6

struct ListBox {
    const char *font_path;
    Label     **labels;
    int         item_count;
    int         x;
    int         y;
    int         width;
    int         visible_rows;
    int         row_height;
    int         scroll_offset;
    int         selected_index;
    int         hovered_index;
    bool        armed;
    bool        prev_mouse_left;
};

static void ListBox__ClearItems(ListBox *list_box) {
    for (int i = 0; i < list_box->item_count; i++) {
        Label_Destroy(&list_box->labels[i]);
    }

    free(list_box->labels);
    list_box->labels     = NULL;
    list_box->item_count = 0;
}

static void ListBox__ClampScroll(ListBox *list_box) {
    const int max_offset =
        list_box->item_count > list_box->visible_rows ? list_box->item_count - list_box->visible_rows : 0;

    if (list_box->scroll_offset > max_offset) {
        list_box->scroll_offset = max_offset;
    }

    if (list_box->scroll_offset < 0) {
        list_box->scroll_offset = 0;
    }
}

static void ListBox__EnsureVisible(ListBox *list_box, const int index) {
    if (index < list_box->scroll_offset) {
        list_box->scroll_offset = index;
    } else if (index >= list_box->scroll_offset + list_box->visible_rows) {
        list_box->scroll_offset = index - list_box->visible_rows + 1;
    }

    ListBox__ClampScroll(list_box);
}

ListBox *ListBox_Create(const char *font_path, const int x, const int y, const int width, const int visible_rows,
                        const int row_height) {
    ListBox *list_box = malloc(sizeof(ListBox));
    FAIL_IF_NULL(list_box);
    memset(list_box, 0, sizeof(ListBox));

    list_box->font_path      = font_path;
    list_box->x              = x;
    list_box->y              = y;
    list_box->width          = width;
    list_box->visible_rows   = visible_rows;
    list_box->row_height     = row_height;
    list_box->selected_index = -1;
    list_box->hovered_index  = -1;

    return list_box;
}

void ListBox_Destroy(ListBox **list_box) {
    if (list_box == NULL || *list_box == NULL) {
        return;
    }

    ListBox__ClearItems(*list_box);
    free(*list_box);
    *list_box = NULL;
}

void ListBox_SetItems(ListBox *list_box, const char *const *items, const int item_count) {
    ListBox__ClearItems(list_box);

    list_box->labels = calloc(item_count > 0 ? item_count : 1, sizeof(Label *));
    FAIL_IF_NULL(list_box->labels);

    for (int i = 0; i < item_count; i++) {
        list_box->labels[i] = Label_Create(list_box->font_path, PALETTE_STATIC);
        Label_SetText(list_box->labels[i], items[i]);
        Label_SetAlignment(list_box->labels[i], LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    }

    list_box->item_count     = item_count;
    list_box->scroll_offset  = 0;
    list_box->selected_index = item_count > 0 ? 0 : -1;
    list_box->hovered_index  = -1;
}

int ListBox_GetSelectedIndex(const ListBox *list_box) { return list_box->selected_index; }

void ListBox_SetSelectedIndex(ListBox *list_box, const int index) {
    if (index < 0 || index >= list_box->item_count) {
        return;
    }

    list_box->selected_index = index;
    ListBox__EnsureVisible(list_box, index);
}

bool ListBox_Update(ListBox *list_box) {
    int  mouse_x;
    int  mouse_y;
    bool mouse_left;

    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool was_mouse_left = list_box->prev_mouse_left;
    list_box->prev_mouse_left = mouse_left;

    const int  height  = list_box->visible_rows * list_box->row_height;
    const bool hovered = mouse_x >= list_box->x && mouse_x < list_box->x + list_box->width && mouse_y >= list_box->y &&
                         mouse_y < list_box->y + height;

    // Always consume the wheel so scrolling elsewhere doesn't pile up and jump the list later.
    const int wheel = InputManager_ConsumeMouseWheel();
    if (hovered && wheel != 0) {
        list_box->scroll_offset -= wheel * 3;
        ListBox__ClampScroll(list_box);
    }

    list_box->hovered_index = -1;
    if (hovered) {
        const int index = list_box->scroll_offset + ((mouse_y - list_box->y) / list_box->row_height);
        if (index < list_box->item_count) {
            list_box->hovered_index = index;
        }
    }

    if (mouse_left && !was_mouse_left) {
        list_box->armed = hovered;
    }

    if (!mouse_left && was_mouse_left) {
        const bool clicked = list_box->armed && list_box->hovered_index >= 0;
        list_box->armed    = false;

        if (clicked && list_box->hovered_index != list_box->selected_index) {
            list_box->selected_index = list_box->hovered_index;
            return true;
        }
    }

    return false;
}

void ListBox_Draw(const ListBox *list_box) {
    const int height = list_box->visible_rows * list_box->row_height;

    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    const SDL_Rect background = {list_box->x, list_box->y, list_box->width, height};
    SDL_RenderFillRect(sdl_renderer, &background);

    for (int row = 0; row < list_box->visible_rows; row++) {
        const int index = list_box->scroll_offset + row;
        if (index >= list_box->item_count) {
            break;
        }

        const int      row_y    = list_box->y + (row * list_box->row_height);
        const SDL_Rect row_rect = {list_box->x, row_y, list_box->width - LIST_BOX_SCROLLBAR_WIDTH - 2,
                                   list_box->row_height};

        if (index == list_box->selected_index) {
            SDL_SetRenderDrawColor(sdl_renderer, 120, 95, 40, 200);
            SDL_RenderFillRect(sdl_renderer, &row_rect);
        } else if (index == list_box->hovered_index) {
            SDL_SetRenderDrawColor(sdl_renderer, 80, 70, 50, 140);
            SDL_RenderFillRect(sdl_renderer, &row_rect);
        }

        Label *label = list_box->labels[index];
        if (index == list_box->selected_index) {
            Label_SetColor(label, 0xFF, 0xE8, 0xA0, 0xFF);
        } else {
            Label_SetColor(label, 0xC7, 0xB3, 0x77, 0xFF);
        }
        Label_Draw(label, list_box->x + LIST_BOX_TEXT_PADDING_X, row_y + (list_box->row_height / 2));
    }

    // Scrollbar
    if (list_box->item_count > list_box->visible_rows) {
        const int track_x = list_box->x + list_box->width - LIST_BOX_SCROLLBAR_WIDTH;
        int       thumb_h = (height * list_box->visible_rows) / list_box->item_count;
        if (thumb_h < 12) {
            thumb_h = 12;
        }
        const int max_offset = list_box->item_count - list_box->visible_rows;
        const int thumb_y    = list_box->y + ((height - thumb_h) * list_box->scroll_offset) / max_offset;

        const SDL_Rect track = {track_x, list_box->y, LIST_BOX_SCROLLBAR_WIDTH, height};
        const SDL_Rect thumb = {track_x, thumb_y, LIST_BOX_SCROLLBAR_WIDTH, thumb_h};
        SDL_SetRenderDrawColor(sdl_renderer, 40, 35, 30, 220);
        SDL_RenderFillRect(sdl_renderer, &track);
        SDL_SetRenderDrawColor(sdl_renderer, 150, 125, 70, 255);
        SDL_RenderFillRect(sdl_renderer, &thumb);
    }
}
