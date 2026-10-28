#include "SkillReport.h"
#include "Character.h"
#include "SkillData.h"
#include "Skills.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// What the game's skill screen says at level 1 with no other skills (no synergies): damage and mana
typedef struct SkillReportCheck {
    const char *skill;
    int         class_id;
    int         low, high;
    float       mana;
} SkillReportCheck;

static const SkillReportCheck skill_report_checks[] = {
    {"Fire Bolt",     CHARACTER_SORCERESS,   3,  6,  2.5f },
    {"Ice Bolt",      CHARACTER_SORCERESS,   3,  5,  3.0f },
    {"Charged Bolt",  CHARACTER_SORCERESS,   2,  4,  3.0f },
    {"Frost Nova",    CHARACTER_SORCERESS,   2,  4,  9.0f },
    {"Fire Ball",     CHARACTER_SORCERESS,   6,  14, 5.0f },
    {"Lightning",     CHARACTER_SORCERESS,   1,  40, 8.0f },
    {"Glacial Spike", CHARACTER_SORCERESS,   16, 24, 10.0f},
    {"Teeth",         CHARACTER_NECROMANCER, 2,  4,  3.0f },
    {"Bone Spear",    CHARACTER_NECROMANCER, 16, 24, 7.0f },
};

static void SkillReport__Line(FILE *out, const Character *character, const SkillRow *s, const int level) {
    int low    = 0;
    int high   = 0;
    int e_low  = 0;
    int e_high = 0;
    int frames = 0;
    SkillData_PhysicalDamage(character, s, level, &low, &high);
    SkillData_ElementalDamage(character, s, level, &e_low, &e_high, &frames);
    fprintf(out, "    %2d: mana %5.1f  to hit %+4d%%  damage %d-%d  element %d-%d (%d frames)", level,
            SkillData_ManaCost(s, level), SkillData_ToHit(s, level), low, high, e_low, e_high, frames);
    for (int c = 0; c < 4; c++) {
        if (s->calc[c][0] != '\0') {
            fprintf(out, "  calc%d %d", c + 1, SkillData_Eval(character, s, level, s->calc[c]));
        }
    }
    for (int a = 0; a < SKILL_AURA_STATS; a++) {
        if (s->aura_stat[a][0] != '\0') {
            fprintf(out, "  %s %d", s->aura_stat[a], SkillData_Eval(character, s, level, s->aura_calc[a]));
        }
    }
    for (int p = 0; p < SKILL_PASSIVE_STATS; p++) {
        if (s->passive_stat[p][0] != '\0') {
            fprintf(out, "  %s %d", s->passive_stat[p], SkillData_Eval(character, s, level, s->passive_calc[p]));
        }
    }
    fprintf(out, "\n");
}

int SkillReport_Write(const char *path) {
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        printf("Can't write %s\n", path);
        return 1;
    }
    static const int levels[] = {1, 5, 10, 15, 20};
    for (int class_id = 0; class_id < CHARACTER_CLASS_COUNT; class_id++) {
        Character character;
        Character_Create(&character, "Report", (CharacterClass)class_id, true, false);
        const SkillClass *skills = Skills_ForClass((CharacterClass)class_id);
        fprintf(out, "== %s\n", Character_ClassName((CharacterClass)class_id));
        for (int k = 0; k < skills->count; k++) {
            const SkillRow *s = SkillData_Get(skills->skills[k].id);
            if (s == NULL) {
                fprintf(out, "  %s: not in skills.txt\n", skills->skills[k].id);
                continue;
            }
            fprintf(out, "  %s (%s, do %d)\n", skills->skills[k].name, s->id, s->do_func);
            for (size_t l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
                SkillReport__Line(out, &character, s, levels[l]);
            }
        }
    }
    // The check against the game's own numbers
    int wrong = 0;
    fprintf(out, "\n== Checked against the game (level 1, no synergies)\n");
    for (size_t c = 0; c < sizeof(skill_report_checks) / sizeof(skill_report_checks[0]); c++) {
        const SkillReportCheck *check = &skill_report_checks[c];
        const SkillRow         *s     = SkillData_Get(check->skill);
        Character               character;
        Character_Create(&character, "Report", (CharacterClass)check->class_id, true, false);
        memset(character.skill_levels, 0, sizeof(character.skill_levels)); // (no Fire Bolt to add to Fire Ball)
        int low    = 0;
        int high   = 0;
        int frames = 0;
        if (s != NULL) {
            SkillData_ElementalDamage(&character, s, 1, &low, &high, &frames);
            if (high == 0) {
                SkillData_PhysicalDamage(&character, s, 1, &low, &high);
            }
        }
        const float mana  = s != NULL ? SkillData_ManaCost(s, 1) : -1.0f;
        const bool  ok    = s != NULL && low == check->low && high == check->high && fabsf(mana - check->mana) < 0.01f;
        wrong            += ok ? 0 : 1;
        fprintf(out, "  %-14s %s: %d-%d mana %.1f (the game: %d-%d mana %.1f)\n", check->skill, ok ? "ok   " : "WRONG",
                low, high, mana, check->low, check->high, check->mana);
    }
    fclose(out);
    printf("Skill report written to %s: %d of the checked numbers differ from the game's\n", path, wrong);
    return wrong;
}
