#ifndef ABYSS_MAP_HISTORY_H
#define ABYSS_MAP_HISTORY_H

#include "Map.h"
#include <stdbool.h>

// The Map Editor's undo and redo. Edits go through here so they're remembered; they're grouped into changes (one
// stroke of the brush, one Fill, one unit placed), and a change is what Undo (Ctrl+Z) takes back and Redo (Ctrl+Y)
// does again. Making a new change after undoing drops what could have been redone.
typedef struct MapHistory MapHistory;

MapHistory *MapHistory_Create(void);
void        MapHistory_Destroy(MapHistory **history);
void        MapHistory_Clear(MapHistory *history); // a new or opened map: nothing to undo

// A change: Begin, any number of edits, End (an empty change is forgotten)
void MapHistory_Begin(MapHistory *history);
void MapHistory_End(MapHistory *history);

// Edits (outside Begin / End each is a change of its own)
void MapHistory_SetCell(MapHistory *history, Map *map, MapLayerKind kind, int layer, int tx, int ty,
                        const MapTileChoice *choice); // NULL clears
void MapHistory_AddEntity(MapHistory *history, Map *map, const char *base, const char *id, const char *name, float x,
                          float y);
void MapHistory_RemoveEntity(MapHistory *history, Map *map, int index);
void MapHistory_TurnEntity(MapHistory *history, Map *map, int index, int direction); // 0..15

bool MapHistory_Undo(MapHistory *history, Map *map); // false: nothing to undo
bool MapHistory_Redo(MapHistory *history, Map *map); // false: nothing to redo

int MapHistory_GetPosition(const MapHistory *history); // changes in effect ("4" of "4 / 11")
int MapHistory_GetCount(const MapHistory *history);    // changes there are, redoable ones included

#endif // ABYSS_MAP_HISTORY_H
