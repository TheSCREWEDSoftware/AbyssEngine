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
    int  defense;       // armor class
    int  attack_rating; // first attack's to-hit
    int  experience;    // given for killing it
    int  velocity;      // walk speed
    bool npc;           // town NPCs: never fight unless combat is forced on
    char treasure[48];  // what it drops (TreasureClassEx.txt), for the difficulty
    // Resistances in percent (100 or more: immune): 0 physical, 1 fire, 2 cold, 3 lightning, 4 poison (Character.h's
    // Element order)
    int resist[5];
    // Its attack's elemental damage (El1, when it's for the first attack): element (0: none), chance in percent, range
    int element;
    int element_chance;
    int element_min;
    int element_max;
} MonsterStats;

bool MonsterStats_Get(const char *monstats_id, MonsterStats *stats); // false if unknown
// monstats Id for a DS1 monster preset (act 0..4, index into that act's MonPreset rows); NULL for spawn markers
const char *MonsterStats_PresetMonster(int act, int index);
void        MonsterStats_Release(void);
// Which difficulty's numbers MonsterStats_Get gives: 0 Normal (Single Player), 1 Nightmare, 2 Hell (a multiplayer
// game's setting)
void MonsterStats_SetDifficulty(int difficulty);

#endif // ABYSS_MONSTER_STATS_H
