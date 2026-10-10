#ifndef ABYSS_PLACES_H
#define ABYSS_PLACES_H

// The areas Free Roam can take you to, in the game's order (Levels.txt):
//  - the ones that are a single fixed map (the towns, Tristram, the Monastery Gate, ...), read from LvlPrest.txt;
//  - the randomly built ones (Blood Moor, the caves, the Jail, the desert, ...), which LevelBuilder puts together
//    anew on every visit.

#include <stdbool.h>

#define PLACE_MAX_FILES 6

typedef struct Place {
    char name[48];  // "Rogue Encampment"
    int  level_id;  // Levels.txt Id
    int  act;       // 0..4
    bool generated; // built by LevelBuilder; no files
    int  file_count;
    char files[PLACE_MAX_FILES][128]; // variants, e.g. the encampment's exit to the north / east / south
} Place;

int          Places_GetCount(void); // loads on first use
const Place *Places_Get(int index);
// The place's map: its preset, or a fresh random layout (NULL if it can't be loaded)
struct Map *Places_LoadMap(const Place *place);
void        Places_Release(void);

#endif // ABYSS_PLACES_H
