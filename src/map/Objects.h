#ifndef ABYSS_OBJECTS_H
#define ABYSS_OBJECTS_H

#include <stdbool.h>

// The game's objects (objects.txt): chests, doors, shrines, waypoints, torches, ... and which object each DS1 preset
// places (the game keeps that table in its program, not in the MPQs; ours is in ObjectPresets.inc).

#define OBJECT_MODE_COUNT 8 // nu, op, on, s1..s5
#define OBJECT_PARMS      8 // objects.txt Parm0..Parm7

typedef struct ObjectInfo {
    int         row;      // in objects.txt
    char        token[8]; // lower case, as the Bestiary lists it
    const char *name;
    int         size_x; // footprint in sub-tiles
    int         size_y;
    bool        selectable; // can be clicked (chests, doors, shrines, waypoints)
    bool        is_door;
    bool        has_mode[OBJECT_MODE_COUNT];
    bool        collision[OBJECT_MODE_COUNT];   // blocks walking while in that mode
    int         frame_count[OBJECT_MODE_COUNT]; // frames the animation uses, from start_frame on (the files of all
    int         start_frame[OBJECT_MODE_COUNT]; // modes often hold the same frames: closed, opening..., open)
    int  operate_fn; // what clicking it does (objects.txt OperateFn): 2 shrine, 4 chest, 8 door, 23 waypoint, ...
    int  parm[OBJECT_PARMS]; // what OperateFn uses (a shrine's Parm0: 1 health, 2 mana, 3 any other)
    bool lockable;           // can be locked (chests): a key opens it
    int  trap_prob;          // percent chance it's trapped
    const char *idle_mode;   // how it starts: "on" for torches and fires (lit), otherwise "nu"
    int         automap; // its mark on the automap (objects.txt AutoMap): a piece of the act's MaxiMap.dc6, 0 = none
} ObjectInfo;

extern const char *const object_mode_tokens[OBJECT_MODE_COUNT];

// The objects.txt row a DS1 preset (type 2 object, act 0..4) places, -1 if none
int  Objects_PresetRow(int act, int preset_id);
bool Objects_Get(int row, ObjectInfo *info);
int  Objects_FindToken(const char *token); // first row using that graphics token, -1 if none
int  Objects_ModeIndex(const char *mode);  // "nu" -> 0, ... -1 if unknown
// Which of a figure's frames an animation mode shows: [start, start + count)
void Objects_FrameRange(const ObjectInfo *info, int mode, int figure_frames, int *start, int *count);
void Objects_Release(void);

#endif // ABYSS_OBJECTS_H
