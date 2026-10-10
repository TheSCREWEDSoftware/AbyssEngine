#ifndef ABYSS_PLACES_H
#define ABYSS_PLACES_H

// Outdoor areas that are a single fixed map in the game (the towns, Tristram, the Monastery Gate, ...), for Free
// Roam's teleport list. Read from LvlPrest.txt (outdoor, whole-level presets) and Levels.txt (names).
// The randomly built wilderness (Blood Moor, ...) and dungeons are not included.

#define PLACE_MAX_FILES 6

typedef struct Place {
    char name[48]; // "Rogue Encampment"
    int  level_id; // Levels.txt Id
    int  act;      // 0..4
    int  file_count;
    char files[PLACE_MAX_FILES][128]; // variants, e.g. the encampment's exit to the north / east / south
} Place;

int          Places_GetCount(void); // loads on first use
const Place *Places_Get(int index);
void         Places_Release(void);

#endif // ABYSS_PLACES_H
