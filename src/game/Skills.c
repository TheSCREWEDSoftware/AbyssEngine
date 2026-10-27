#include "Skills.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "Affixes.h"
#include "SkillData.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Per class (in CharacterClass order): skills.txt's class code, the icon file's prefix, the tree picture's letter, and
// the StrSklTree<n> strings that name each tree on its tab (one per line)
static const struct {
    const char *code;
    const char *icon_prefix;
    char        tree_letter;
    int         tab_lines[SKILL_PAGES][SKILL_TAB_LINES];
} class_skills[CHARACTER_CLASS_COUNT] = {
    {"ama", "am", 'a', {{10, 11}, {8, 9}, {6, 7}}   }, // Amazon
    {"sor", "so", 's', {{25, 5}, {24, 5}, {23, 5}}  }, // Sorceress
    {"nec", "ne", 'n', {{19}, {17, 18, 5}, {16, 5}} }, // Necromancer
    {"pal", "pa", 'p', {{15, 4}, {14, 13}, {12, 13}}}, // Paladin
    {"bar", "ba", 'b', {{15, 4}, {21, 22}, {20}}    }, // Barbarian
    {"dru", "dr", 'd', {{26}, {27, 28}, {29}}       }, // Druid
    {"ass", "as", 'i', {{30}, {31, 32}, {33, 34}}   }, // Assassin
};

static SkillClass loaded_classes[CHARACTER_CLASS_COUNT];
static bool       loaded = false;

// ---------------------------------------------------------------------------------------------------------------------
// Text

// Only what the game's fonts draw: no colour codes
static void Skills__Sanitize(char *out, const size_t out_len, const char *in) {
    size_t o = 0;
    for (const unsigned char *ch = (const unsigned char *)in; *ch != '\0' && o + 1 < out_len; ch++) {
        if (*ch == 0xFF && ch[1] == 'c' && ch[2] != '\0') { // \xFFc<n> colour code
            ch += 2;
            continue;
        }
        if (*ch == '\n' || (*ch >= 0x20 && *ch < 0x7F)) {
            out[o++] = (char)*ch;
        }
    }
    out[o] = '\0';
}

static void Skills__String(char *out, const size_t out_len, const char *key, const char *fallback) {
    const char *value = StringTable_Find(key);
    Skills__Sanitize(out, out_len, value != NULL ? value : fallback);
}

// The game keeps multi-line strings bottom line first: put them in reading order
static void Skills__ReverseLines(char *text, const size_t size) {
    char  copy[SKILL_TEXT_MAX * 3];
    char *lines[16];
    int   count = 0;
    snprintf(copy, sizeof(copy), "%s", text);
    for (char *line = strtok(copy, "\n"); line != NULL && count < 16; line = strtok(NULL, "\n")) {
        lines[count++] = line;
    }
    text[0] = '\0';
    for (int i = count - 1; i >= 0; i--) {
        lines[i][0] = (char)toupper((unsigned char)lines[i][0]);
        strncat(text, lines[i], size - strlen(text) - 2);
        if (i > 0) {
            strncat(text, "\n", size - strlen(text) - 1);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

static void Skills__Load(void) {
    if (loaded) {
        return;
    }
    loaded = true;
    memset(loaded_classes, 0, sizeof(loaded_classes));
    for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
        SkillClass *info = &loaded_classes[c];
        snprintf(info->code, sizeof(info->code), "%s", class_skills[c].code);
        snprintf(info->icon_path, sizeof(info->icon_path), "/data/global/ui/spells/%sskillicon.dc6",
                 class_skills[c].icon_prefix);
        snprintf(info->tree_path, sizeof(info->tree_path), "/data/global/ui/spells/skltree_%c_back.dc6",
                 class_skills[c].tree_letter);
        for (int page = 0; page < SKILL_PAGES; page++) {
            for (int line = 0; line < SKILL_TAB_LINES; line++) {
                const int string_index = class_skills[c].tab_lines[page][line];
                if (string_index == 0) {
                    continue;
                }
                char key[32];
                snprintf(key, sizeof(key), "StrSklTree%d", string_index);
                Skills__String(info->tab_lines[page][line], sizeof(info->tab_lines[page][line]), key, "");
                if (line > 0) {
                    strncat(info->tab_names[page], " ",
                            sizeof(info->tab_names[page]) - strlen(info->tab_names[page]) - 1);
                }
                strncat(info->tab_names[page], info->tab_lines[page][line],
                        sizeof(info->tab_names[page]) - strlen(info->tab_names[page]) - 1);
            }
        }
    }

    DataTable *skills = DataTable_Load(EXCEL_SKILLS);
    if (skills == NULL) {
        return;
    }
    // 1.10+ keeps the tree layout and the texts in skilldesc.txt; older data has the layout in skills.txt
    const int  c_desc_ref = DataTable_GetColumnIndex(skills, "skilldesc");
    DataTable *desc =
        (c_desc_ref >= 0 && FileManager_FileExists(EXCEL_SKILLDESC)) ? DataTable_Load(EXCEL_SKILLDESC) : NULL;
    const DataTable *layout = desc != NULL ? desc : skills;

    const int c_skill      = DataTable_GetColumnIndex(skills, "skill");
    const int c_class      = DataTable_GetColumnIndex(skills, "charclass");
    const int c_level      = DataTable_GetColumnIndex(skills, "reqlevel");
    const int c_passive    = DataTable_GetColumnIndex(skills, "passive");
    const int c_left       = DataTable_GetColumnIndex(skills, "leftskill");
    const int c_mana       = DataTable_GetColumnIndex(skills, "mana");
    const int c_mana_shift = DataTable_GetColumnIndex(skills, "manashift");
    const int c_req[3] = {DataTable_GetColumnIndex(skills, "reqskill1"), DataTable_GetColumnIndex(skills, "reqskill2"),
                          DataTable_GetColumnIndex(skills, "reqskill3")};
    const int c_desc_key = desc != NULL ? DataTable_GetColumnIndex(desc, "skilldesc") : -1;
    const int c_page     = DataTable_GetColumnIndex(layout, "SkillPage");
    const int c_row      = DataTable_GetColumnIndex(layout, "SkillRow");
    const int c_column   = DataTable_GetColumnIndex(layout, "SkillColumn");
    const int c_icon     = DataTable_GetColumnIndex(layout, "IconCel");
    const int c_name     = desc != NULL ? DataTable_GetColumnIndex(desc, "str name") : -1;
    const int c_short    = desc != NULL ? DataTable_GetColumnIndex(desc, "str short") : -1;
    const int c_long     = desc != NULL ? DataTable_GetColumnIndex(desc, "str long") : -1;
    const int desc_rows  = desc != NULL ? DataTable_GetRowCount(desc) : 0;

    // Prerequisites by id, resolved once every skill is in
    char prerequisite_ids[CHARACTER_CLASS_COUNT][SKILLS_PER_CLASS_MAX][3][64];
    memset(prerequisite_ids, 0, sizeof(prerequisite_ids));

    for (int row = 0; row < DataTable_GetRowCount(skills); row++) {
        const char *code  = DataTable_GetString(skills, row, c_class);
        int         klass = -1;
        for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
            klass = strcmp(code, class_skills[c].code) == 0 ? c : klass;
        }
        if (klass < 0 || loaded_classes[klass].count >= SKILLS_PER_CLASS_MAX) {
            continue;
        }
        int layout_row = row;
        if (desc != NULL) {
            const char *desc_name = DataTable_GetString(skills, row, c_desc_ref);
            layout_row            = -1;
            for (int d = 0; d < desc_rows && desc_name[0] != '\0'; d++) {
                if (strcmp(DataTable_GetString(desc, d, c_desc_key), desc_name) == 0) {
                    layout_row = d;
                    break;
                }
            }
            if (layout_row < 0) {
                continue;
            }
        }
        const int page = DataTable_GetInt(layout, layout_row, c_page);
        if (page < 1 || page > SKILL_PAGES) {
            continue; // not on a tree (hidden, or only from items)
        }
        SkillClass *info  = &loaded_classes[klass];
        const int   index = info->count++;
        SkillDef   *skill = &info->skills[index];
        snprintf(skill->id, sizeof(skill->id), "%s", DataTable_GetString(skills, row, c_skill));
        skill->page           = page;
        skill->row            = DataTable_GetInt(layout, layout_row, c_row);
        skill->column         = DataTable_GetInt(layout, layout_row, c_column);
        skill->icon_frame     = DataTable_GetInt(layout, layout_row, c_icon);
        skill->required_level = DataTable_GetInt(skills, row, c_level);
        skill->passive        = c_passive >= 0 && DataTable_GetInt(skills, row, c_passive) == 1;
        skill->left_ok        = c_left >= 0 && DataTable_GetInt(skills, row, c_left) == 1;
        if (c_mana >= 0 && c_mana_shift >= 0) { // mana in 256ths, times 2^manashift
            skill->mana_cost =
                (DataTable_GetInt(skills, row, c_mana) << DataTable_GetInt(skills, row, c_mana_shift)) >> 8;
        }
        for (int r = 0; r < 3; r++) {
            snprintf(prerequisite_ids[klass][index][r], 64, "%s", DataTable_GetString(skills, row, c_req[r]));
        }
        if (desc != NULL) {
            Skills__String(skill->name, sizeof(skill->name), DataTable_GetString(desc, layout_row, c_name), skill->id);
            Skills__String(skill->short_desc, sizeof(skill->short_desc), DataTable_GetString(desc, layout_row, c_short),
                           "");
            Skills__String(skill->long_desc, sizeof(skill->long_desc), DataTable_GetString(desc, layout_row, c_long),
                           "");
            Skills__ReverseLines(skill->short_desc, sizeof(skill->short_desc));
            Skills__ReverseLines(skill->long_desc, sizeof(skill->long_desc));
        } else {
            Skills__Sanitize(skill->name, sizeof(skill->name), skill->id);
        }
    }
    for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
        SkillClass *info = &loaded_classes[c];
        for (int s = 0; s < info->count; s++) {
            for (int r = 0; r < 3; r++) {
                info->skills[s].prerequisites[r] =
                    prerequisite_ids[c][s][r][0] != '\0' ? Skills_Find(info, prerequisite_ids[c][s][r]) : -1;
            }
        }
    }
    if (desc != NULL) {
        DataTable_Destroy(&desc);
    }
    DataTable_Destroy(&skills);
}

void Skills_Release(void) { loaded = false; }

const SkillClass *Skills_ForClass(const CharacterClass class_id) {
    Skills__Load();
    return &loaded_classes[class_id < CHARACTER_CLASS_COUNT ? class_id : 0];
}

int Skills_Find(const SkillClass *skills, const char *id) {
    for (int s = 0; s < skills->count; s++) {
        if (strcmp(skills->skills[s].id, id) == 0) {
            return s;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Spending points

bool Skills_CanRaise(const Character *character, const int skill, const char **why) {
    const SkillClass *skills = Skills_ForClass(character->class_id);
    static char       reason[128];
    const char       *result = NULL;
    if (skill < 0 || skill >= skills->count) {
        result = "There's no such skill.";
    } else {
        const SkillDef *def   = &skills->skills[skill];
        const int       level = character->skill_levels[skill];
        if (level >= SKILL_MAX_LEVEL) {
            result = "That skill is as high as it goes.";
        } else if (character->skill_points <= 0) {
            result = "You have no skill points left.";
        } else if (character->level < def->required_level + level) {
            snprintf(reason, sizeof(reason), "You need to be level %d.", def->required_level + level);
            result = reason;
        } else {
            for (int r = 0; r < 3 && result == NULL; r++) {
                const int need = def->prerequisites[r];
                if (need >= 0 && character->skill_levels[need] == 0) {
                    snprintf(reason, sizeof(reason), "Learn %s first.", skills->skills[need].name);
                    result = reason;
                }
            }
        }
    }
    if (why != NULL) {
        *why = result;
    }
    return result == NULL;
}

bool Skills_Raise(Character *character, const int skill) {
    if (!Skills_CanRaise(character, skill, NULL)) {
        return false;
    }
    character->skill_levels[skill]++;
    character->skill_points--;
    return true;
}

bool Skills_CanAssign(const Character *character, const int skill, const bool left) {
    if (skill == SKILL_ATTACK) {
        return true;
    }
    if (skill >= SKILL_FROM_ITEM) {
        const SkillRow *row = SkillData_ByRow(skill - SKILL_FROM_ITEM);
        return row != NULL && !row->passive && (!left || row->left_ok) &&
               Affixes_ItemSkillLevel(character, skill - SKILL_FROM_ITEM, NULL) > 0;
    }
    const SkillClass *skills = Skills_ForClass(character->class_id);
    if (skill < 0 || skill >= skills->count ||
        (character->skill_levels[skill] == 0 && Affixes_SkillBonus(character, skill) <= 0)) {
        return false; // (a skill an item gives counts as learned)
    }
    const SkillDef *def = &skills->skills[skill];
    return !def->passive && (!left || def->left_ok);
}
