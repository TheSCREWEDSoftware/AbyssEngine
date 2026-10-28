#ifndef ABYSS_SKILLDATA_H
#define ABYSS_SKILLDATA_H

#include "Character.h"
#include <stdbool.h>

// What a skill does, from skills.txt: its damage and mana cost by level, the missile it fires, the monster it summons,
// the stats its aura or buff gives, and the game's little formulas ("calcs": "ln12" is par1 + par2 per level after the
// first, "skill('Fire Ball'.blvl)" the points in another skill, ...) worked out for a character.

#define SKILL_AURA_STATS    6
#define SKILL_PASSIVE_STATS 5
#define SKILL_CALC_MAX      200

typedef enum SkillKind {
    SKILL_KIND_PASSIVE,  // always on (masteries, Dodge, ...)
    SKILL_KIND_MISSILE,  // fires one or more missiles at the target (Fire Bolt, Multiple Shot, Teeth, ...)
    SKILL_KIND_NOVA,     // a ring of missiles round the caster (Frost Nova, Nova, Howl, ...)
    SKILL_KIND_AREA,     // lands on the spot clicked (Meteor, Blizzard, Fist of the Heavens, ...)
    SKILL_KIND_MELEE,    // a blow with something added (Bash, Jab, Power Strike, ...)
    SKILL_KIND_AURA,     // the Paladin's: on while it's the right skill, for you and your party nearby
    SKILL_KIND_BUFF,     // on you for a while (Frozen Armor, Shout, Battle Orders, ...)
    SKILL_KIND_CURSE,    // on the monsters around the spot clicked (Amplify Damage, Weaken, ...)
    SKILL_KIND_SUMMON,   // calls monsters that fight for you (Raise Skeleton, golems, Valkyrie, ...)
    SKILL_KIND_TELEPORT, // Teleport
    SKILL_KIND_OTHER,    // not done yet
} SkillKind;

typedef struct SkillRow {
    char id[64]; // skills.txt "skill"
    int  row;
    int  do_func; // srvdofunc
    int  start_func;
    char missile[40];      // srvmissile (fired at the start)
    char missile_a[40];    // srvmissilea (fired by the skill's function)
    char summon[40];       // monstats Id
    char state[32];        // aurastate
    char target_state[32]; // auratargetstate (a curse's, on the monsters)
    char anim[4];          // the player's animation: SC, A1, TH, ...
    char range[8];         // "rng", "h2h", "none", "loc"
    char cast_sound[40];   // stsound
    char do_sound[40];     // dosound
    bool aura;
    bool passive;
    bool left_ok;
    int  params[8];
    int  mana, level_mana, mana_shift, min_mana;
    int  to_hit, level_to_hit;
    int  hit_shift;
    int  min_damage, min_level_damage[5], max_damage, max_level_damage[5];
    char damage_synergy[SKILL_CALC_MAX];
    int  element; // Element, -1 none
    bool magic;   // "mag": unresistable
    int  element_min, element_min_level[5], element_max, element_max_level[5];
    char element_synergy[SKILL_CALC_MAX];
    int  element_length, element_level_length[3]; // frames (25 a second)
    char aura_length[SKILL_CALC_MAX];
    char aura_range[SKILL_CALC_MAX];
    char aura_stat[SKILL_AURA_STATS][40];
    char aura_calc[SKILL_AURA_STATS][SKILL_CALC_MAX];
    char passive_stat[SKILL_PASSIVE_STATS][40];
    char passive_calc[SKILL_PASSIVE_STATS][SKILL_CALC_MAX];
    char pet_max[SKILL_CALC_MAX];
    char calc[4][SKILL_CALC_MAX];
    bool progressive; // a charge-up attack (Tiger Strike, Fists of Fire, ...): each blow that lands adds a charge
    int  progressive_damage; // prgdam: what its charges do (1 damage, 2 life and mana stolen, 3 Phoenix, 4 elemental)
    int  delay;              // frames before a skill with a delay can be used again (Meteor, Blizzard, Fire Wall, ...)
    bool finishing;          // a finishing move (Dragon Talon, Dragon Claw, ...): lets the charges go
    SkillKind kind;
} SkillRow;

const SkillRow *SkillData_Get(const char *id); // NULL if not in skills.txt
const SkillRow *SkillData_ByRow(int row);      // by its skills.txt row (what multiplayer sends), NULL if none
const SkillRow *SkillData_ForCharacter(const Character *character, int skill); // by the class's skill index

// The points in a skill (what "blvl" means), and the level it works at ("lvl": the same, until items give +skills)
int SkillData_Level(const Character *character, const char *id);

// A calc worked out for the character with the skill at that level
int SkillData_Eval(const Character *character, const SkillRow *skill, int level, const char *calc);

float SkillData_ManaCost(const SkillRow *skill, int level); // can be a fraction (Fire Bolt: 2.5)
void SkillData_PhysicalDamage(const Character *character, const SkillRow *skill, int level, int *minimum, int *maximum);
// Elemental damage with the synergies; frames: how long cold slows / poison lasts (25 a second)
void SkillData_ElementalDamage(const Character *character, const SkillRow *skill, int level, int *minimum, int *maximum,
                               int *frames);
int  SkillData_ToHit(const SkillRow *skill, int level); // attack rating bonus, percent

void SkillData_Release(void);

#endif // ABYSS_SKILLDATA_H
