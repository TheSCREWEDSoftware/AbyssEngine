#ifndef ABYSS_SKILLS_H
#define ABYSS_SKILLS_H

#include "Character.h"
#include <stdbool.h>

// The classes' skills (skills.txt, with the tree layout and texts from skilldesc.txt), the same for the Catalog and the
// game: each class's three trees, where each skill sits, its icon, required level, prerequisites and mana cost.
//
// The game's rules for spending points: a skill can be raised while the character's level is at least its required
// level plus the points already in it, its prerequisites have a point each, and it's below 20.

#define SKILL_PAGES          3
#define SKILL_TAB_LINES      3
#define SKILLS_PER_CLASS_MAX 40
#define SKILL_MAX_LEVEL      20
#define SKILL_TEXT_MAX       160
#define SKILL_ATTACK         -1 // the basic attack every class has (left or right)

typedef struct SkillDef {
    char id[64]; // skills.txt "skill" (what reqskill columns and save files name)
    char name[64];
    char short_desc[SKILL_TEXT_MAX];
    char long_desc[SKILL_TEXT_MAX * 3];
    int  prerequisites[3]; // indices in the class's list, -1 = none
    int  page;             // 1..3
    int  row;              // 1..6
    int  column;           // 1..3
    int  icon_frame;       // in the class's skill icons; the next frame is its dimmed look
    int  required_level;
    int  mana_cost; // at level 1
    bool passive;
    bool left_ok; // can be the left skill too (skills.txt leftskill)
} SkillDef;

typedef struct SkillClass {
    SkillDef skills[SKILLS_PER_CLASS_MAX];
    int      count;
    char     tab_lines[SKILL_PAGES][SKILL_TAB_LINES][32]; // each tree's name, as the tabs show it
    char     tab_names[SKILL_PAGES][64];                  // and on one line
    char     icon_path[96];                               // data/global/ui/spells/<xx>skillicon.dc6
    char     tree_path[96];                               // data/global/ui/spells/skltree_<x>_back.dc6
    char     code[4];                                     // skills.txt charclass ("ama")
} SkillClass;

const SkillClass *Skills_ForClass(CharacterClass class_id);
int               Skills_Find(const SkillClass *skills, const char *id); // index, -1 if none

// Spending points (Character's skill_levels are indexed like the class's list)
bool Skills_CanRaise(const Character *character, int skill, const char **why); // why not, for the player
bool Skills_Raise(Character *character, int skill);
// Can be put on the left or right mouse button: learned, not passive (left: only those the game allows there)
bool Skills_CanAssign(const Character *character, int skill, bool left);

void Skills_Release(void);

#endif // ABYSS_SKILLS_H
