#ifndef ABYSS_MONSTER_STATS_H
#define ABYSS_MONSTER_STATS_H

#include <stdbool.h>

// Combat numbers for monsters and NPCs (monstats.txt percentages of MonLvl.txt base values, normal difficulty), and the
// per-act preset table the game's DS1 maps use to place their NPCs and bosses (MonPreset.txt, SuperUniques.txt).

typedef struct MonsterStats {
    int  level;
    int  hp_min;
    int  hp_max;
    int  damage_min; // first attack
    int  damage_max;
    int  velocity; // walk speed
    bool npc;      // town NPCs: never fight unless combat is forced on
} MonsterStats;

bool MonsterStats_Get(const char *monstats_id, MonsterStats *stats); // false if unknown
// monstats Id for a DS1 monster preset (act 0..4, index into that act's MonPreset rows); NULL for spawn markers
const char *MonsterStats_PresetMonster(int act, int index);
void        MonsterStats_Release(void);

#endif // ABYSS_MONSTER_STATS_H
