#ifndef ABYSS_LEVEL_TYPES_H
#define ABYSS_LEVEL_TYPES_H

// The game's area tile sets (LvlTypes.txt): "Act 1 - Town" uses Floor.dt1, Fence.dt1, ... New maps start from one.

#define LEVEL_TYPE_MAX_FILES 32

typedef struct LevelType {
    char  name[48];
    int   act; // 0..4
    int   file_count;
    char *files[LEVEL_TYPE_MAX_FILES]; // "data\global\tiles\Act1\Town\Floor.dt1"
} LevelType;

int              LevelTypes_GetCount(void); // loads on first use
const LevelType *LevelTypes_Get(int index);
int              LevelTypes_Find(const char *name); // -1 if missing
void             LevelTypes_Release(void);

#endif // ABYSS_LEVEL_TYPES_H
