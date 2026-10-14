#include "MapHistory.h"
#include "../common/Logging.h"
#include <stdlib.h>
#include <string.h>

#define MAX_CHANGES 1000 // the oldest are forgotten after this many

typedef enum EditKind { EDIT_CELL, EDIT_ENTITY_ADDED, EDIT_ENTITY_REMOVED } EditKind;

typedef struct MapEdit {
    EditKind kind;
    // EDIT_CELL
    MapLayerKind layer_kind;
    int          layer;
    int          tx;
    int          ty;
    DS1Cell      before;
    DS1Cell      after;
    // EDIT_ENTITY_*: where it is (was) in the list, and what it is
    int       index;
    MapEntity entity;
} MapEdit;

struct MapHistory {
    MapEdit *edits;
    int      edit_count;
    int      edit_capacity;
    int     *change_ends; // per change: one past its last edit
    int      change_count;
    int      change_capacity;
    int      position; // changes in effect
    bool     open;     // between Begin and End
    int      open_start;
};

MapHistory *MapHistory_Create(void) {
    MapHistory *history = calloc(1, sizeof(MapHistory));
    FAIL_IF_NULL(history);
    return history;
}

void MapHistory_Destroy(MapHistory **history) {
    if (history == NULL || *history == NULL) {
        return;
    }
    free((*history)->edits);
    free((*history)->change_ends);
    free(*history);
    *history = NULL;
}

void MapHistory_Clear(MapHistory *history) {
    history->edit_count   = 0;
    history->change_count = 0;
    history->position     = 0;
    history->open         = false;
}

static int MapHistory__ChangeStart(const MapHistory *history, const int change) {
    return change > 0 ? history->change_ends[change - 1] : 0;
}

// Drops the oldest change (when there are too many)
static void MapHistory__DropOldest(MapHistory *history) {
    const int edits = history->change_ends[0];
    memmove(history->edits, history->edits + edits, sizeof(MapEdit) * (size_t)(history->edit_count - edits));
    history->edit_count -= edits;
    memmove(history->change_ends, history->change_ends + 1, sizeof(int) * (size_t)(history->change_count - 1));
    history->change_count--;
    history->position--;
    for (int c = 0; c < history->change_count; c++) {
        history->change_ends[c] -= edits;
    }
}

void MapHistory_Begin(MapHistory *history) {
    if (history->open) {
        MapHistory_End(history);
    }
    // A new change after undoing: what could have been redone goes
    history->change_count = history->position;
    history->edit_count   = MapHistory__ChangeStart(history, history->position);
    history->open         = true;
    history->open_start   = history->edit_count;
}

void MapHistory_End(MapHistory *history) {
    if (!history->open) {
        return;
    }
    history->open = false;
    if (history->edit_count == history->open_start) {
        return; // nothing changed
    }
    if (history->change_count == history->change_capacity) {
        history->change_capacity = history->change_capacity > 0 ? history->change_capacity * 2 : 64;
        int *grown               = realloc(history->change_ends, sizeof(int) * (size_t)history->change_capacity);
        FAIL_IF_NULL(grown);
        history->change_ends = grown;
    }
    history->change_ends[history->change_count++] = history->edit_count;
    history->position                             = history->change_count;
    if (history->change_count > MAX_CHANGES) {
        MapHistory__DropOldest(history);
    }
}

static MapEdit *MapHistory__Push(MapHistory *history) {
    if (history->edit_count == history->edit_capacity) {
        history->edit_capacity = history->edit_capacity > 0 ? history->edit_capacity * 2 : 256;
        MapEdit *grown         = realloc(history->edits, sizeof(MapEdit) * (size_t)history->edit_capacity);
        FAIL_IF_NULL(grown);
        history->edits = grown;
    }
    MapEdit *edit = &history->edits[history->edit_count++];
    memset(edit, 0, sizeof(MapEdit));
    return edit;
}

void MapHistory_SetCell(MapHistory *history, Map *map, const MapLayerKind kind, const int layer, const int tx,
                        const int ty, const MapTileChoice *choice) {
    const DS1Cell *cell = Map_GetCell(map, kind, layer, tx, ty);
    if (cell == NULL) {
        return;
    }
    const bool alone = !history->open;
    if (alone) {
        MapHistory_Begin(history);
    }
    const DS1Cell before = *cell;
    Map_SetCell(map, kind, layer, tx, ty, choice);
    MapEdit *edit    = MapHistory__Push(history);
    edit->kind       = EDIT_CELL;
    edit->layer_kind = kind;
    edit->layer      = layer;
    edit->tx         = tx;
    edit->ty         = ty;
    edit->before     = before;
    edit->after      = *Map_GetCell(map, kind, layer, tx, ty);
    if (alone) {
        MapHistory_End(history);
    }
}

void MapHistory_AddEntity(MapHistory *history, Map *map, const char *base, const char *id, const char *name,
                          const float x, const float y) {
    const bool alone = !history->open;
    if (alone) {
        MapHistory_Begin(history);
    }
    MapEntity *entity = Map_AddEntity(map, base, id, name, x, y);
    MapEdit   *edit   = MapHistory__Push(history);
    edit->kind        = EDIT_ENTITY_ADDED;
    edit->index       = map->entity_count - 1;
    edit->entity      = *entity;
    if (alone) {
        MapHistory_End(history);
    }
}

void MapHistory_RemoveEntity(MapHistory *history, Map *map, const int index) {
    if (index < 0 || index >= map->entity_count) {
        return;
    }
    const bool alone = !history->open;
    if (alone) {
        MapHistory_Begin(history);
    }
    MapEdit *edit = MapHistory__Push(history);
    edit->kind    = EDIT_ENTITY_REMOVED;
    edit->index   = index;
    edit->entity  = map->entities[index];
    Map_RemoveEntity(map, index);
    if (alone) {
        MapHistory_End(history);
    }
}

// Puts an entity back at its place in the list
static void MapHistory__InsertEntity(Map *map, const int index, const MapEntity *entity) {
    Map_AddEntity(map, entity->base, entity->id, entity->name, entity->x, entity->y); // grows the list
    const int at = index < map->entity_count - 1 ? index : map->entity_count - 1;
    memmove(&map->entities[at + 1], &map->entities[at], sizeof(MapEntity) * (size_t)(map->entity_count - 1 - at));
    map->entities[at] = *entity;
}

bool MapHistory_Undo(MapHistory *history, Map *map) {
    MapHistory_End(history);
    if (history->position == 0) {
        return false;
    }
    const int change = --history->position;
    for (int e = history->change_ends[change] - 1; e >= MapHistory__ChangeStart(history, change); e--) {
        const MapEdit *edit = &history->edits[e];
        switch (edit->kind) {
        case EDIT_CELL:
            Map_SetCellRaw(map, edit->layer_kind, edit->layer, edit->tx, edit->ty, &edit->before);
            break;
        case EDIT_ENTITY_ADDED:
            Map_RemoveEntity(map, edit->index);
            break;
        case EDIT_ENTITY_REMOVED:
            MapHistory__InsertEntity(map, edit->index, &edit->entity);
            break;
        }
    }
    return true;
}

bool MapHistory_Redo(MapHistory *history, Map *map) {
    MapHistory_End(history);
    if (history->position == history->change_count) {
        return false;
    }
    const int change = history->position++;
    for (int e = MapHistory__ChangeStart(history, change); e < history->change_ends[change]; e++) {
        const MapEdit *edit = &history->edits[e];
        switch (edit->kind) {
        case EDIT_CELL:
            Map_SetCellRaw(map, edit->layer_kind, edit->layer, edit->tx, edit->ty, &edit->after);
            break;
        case EDIT_ENTITY_ADDED:
            MapHistory__InsertEntity(map, edit->index, &edit->entity);
            break;
        case EDIT_ENTITY_REMOVED:
            Map_RemoveEntity(map, edit->index);
            break;
        }
    }
    return true;
}

int MapHistory_GetPosition(const MapHistory *history) { return history->position; }
int MapHistory_GetCount(const MapHistory *history) { return history->change_count; }
