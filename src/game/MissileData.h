#ifndef ABYSS_MISSILEDATA_H
#define ABYSS_MISSILEDATA_H

#include <stdbool.h>

// Missiles.txt: how each missile flies (speed, how long it lasts), what it looks like (its DCC in
// data/global/missiles, frames, directions, drawn glowing or not) and what it turns into (the explosion when it hits).

typedef struct MissileInfo {
    char  name[40];
    float speed;     // sub-tiles a second
    float max_speed; // sub-tiles a second
    float accel;     // sub-tiles a second, each second
    float seconds;   // how long it flies before it's gone
    char  cel_file[40];
    int   frames;           // AnimLen
    float frame_rate;       // frames a second
    int   directions;       // NumDirections
    bool  loop;             // keeps animating while it flies
    bool  glow;             // Trans: drawn added onto what's under it
    int   light;            // light radius
    int   size;             // how wide it is for hitting things (sub-tiles)
    bool  pierce;           // goes through what it hits (with the Pierce skill)
    bool  explosion;        // an explosion: plays once where it is, doesn't fly
    int   hit_func;         // pSrvHitFunc: 1 and 14 burst over an area (Fire Ball, Meteor)
    char  explode_into[40]; // ExplosionMissile
    char  sub_missile[40];  // SubMissile1 (the Frozen Orb's shards, ...)
    char  hit_missile[40];  // HitSubMissile1
    char  travel_sound[40];
    char  hit_sound[40];
} MissileInfo;

const MissileInfo *MissileData_Get(const char *name); // NULL if Missiles.txt has none by that name
void               MissileData_Release(void);

#endif // ABYSS_MISSILEDATA_H
