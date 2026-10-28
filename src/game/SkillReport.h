#ifndef ABYSS_SKILL_REPORT_H
#define ABYSS_SKILL_REPORT_H

// --skill-report: every class's skills, their numbers at levels 1, 5, 10, 15 and 20 for a new character (mana, to hit,
// damage, element and how long it lasts, the calcs, what auras and passives give), written to a file; and a check of
// the well known first-level numbers (no synergies) against the game's own. Returns the number of numbers that differ.
int SkillReport_Write(const char *path);

#endif // ABYSS_SKILL_REPORT_H
