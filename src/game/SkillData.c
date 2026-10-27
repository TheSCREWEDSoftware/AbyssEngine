#include "SkillData.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "Skills.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static SkillRow *rows      = NULL;
static int       row_count = -1;

// ---------------------------------------------------------------------------------------------------------------------
// Loading

static SkillKind SkillData__Kind(const SkillRow *s) {
    if (s->passive) {
        return SKILL_KIND_PASSIVE;
    }
    if (s->aura) {
        return SKILL_KIND_AURA;
    }
    switch (s->do_func) {
    case 27:
        return SKILL_KIND_TELEPORT;
    case 22:
        return SKILL_KIND_NOVA; // Frost Nova, Nova, Poison Nova, Howl
    case 28:                    // Meteor, Blizzard, Eruption
    case 80:                    // Fist of the Heavens
    case 55:                    // Corpse Explosion
    case 123:                   // Volcano: comes up where you point and spews fire for a while
        return SKILL_KIND_AREA;
    case 18: // the armors, Quickness, Fade, Venom
    case 23: // Blaze, Energy Shield
    case 25: // Enchant
    case 29: // Thunder Storm
    case 77: // Leap: through the air to the spot, knocking back what's round where you land
        return SKILL_KIND_OTHER;
    case 54: // Blade Shield: blades round you cut what comes close
    case 124:
    case 116: // the Druid's shapes: Werewolf, Werebear (a buff that changes how you look)
        return SKILL_KIND_BUFF;
    case 68: // the warcries: Shout, Battle Orders and Battle Command last; Battle Cry and War Cry hit the monsters
        return s->aura_stat[0][0] != '\0' && strcmp(s->id, "Battle Cry") != 0 ? SKILL_KIND_BUFF : SKILL_KIND_NOVA;
    case 30: // the curses
    case 61: // Confuse
    case 6:  // Inner Sight, Slow Missiles
    case 47: // Cloak of Shadows
        return SKILL_KIND_CURSE;
    default:
        break;
    }
    if (s->summon[0] != '\0' && s->do_func != 60 && s->do_func != 62) { // not the bone walls
        return SKILL_KIND_SUMMON;
    }
    if (strcmp(s->range, "h2h") == 0) {
        return SKILL_KIND_MELEE;
    }
    if (s->missile[0] != '\0' || s->missile_a[0] != '\0') {
        return SKILL_KIND_MISSILE;
    }
    return SKILL_KIND_OTHER;
}

static int SkillData__Element(const char *type, bool *magic) {
    *magic = false;
    if (strcmp(type, "fire") == 0) {
        return ELEMENT_FIRE;
    }
    if (strcmp(type, "cold") == 0) {
        return ELEMENT_COLD;
    }
    if (strcmp(type, "ltng") == 0) {
        return ELEMENT_LIGHTNING;
    }
    if (strcmp(type, "pois") == 0) {
        return ELEMENT_POISON;
    }
    if (strcmp(type, "mag") == 0) {
        *magic = true;
        return ELEMENT_PHYSICAL;
    }
    return -1;
}

static void SkillData__Load(void) {
    if (row_count >= 0) {
        return;
    }
    row_count       = 0;
    DataTable *data = DataTable_Load(EXCEL_SKILLS);
    if (data == NULL) {
        LOG_WARN("No skills.txt: skills can't be cast");
        return;
    }
    const int count = DataTable_GetRowCount(data);
    rows            = calloc(count > 0 ? (size_t)count : 1, sizeof(SkillRow));
    FAIL_IF_NULL(rows);
#define COL(name)        DataTable_GetColumnIndex(data, name)
#define STR(field, name) snprintf(field, sizeof(field), "%s", DataTable_GetString(data, r, COL(name)))
#define INT(name)        DataTable_GetInt(data, r, COL(name))
    for (int r = 0; r < count; r++) {
        const char *id = DataTable_GetString(data, r, COL("skill"));
        if (id[0] == '\0') {
            continue;
        }
        SkillRow *s = &rows[row_count++];
        s->row      = r;
        STR(s->id, "skill");
        s->do_func    = INT("srvdofunc");
        s->start_func = INT("srvstfunc");
        STR(s->missile, "srvmissile");
        STR(s->missile_a, "srvmissilea");
        STR(s->summon, "summon");
        STR(s->state, "aurastate");
        STR(s->target_state, "auratargetstate");
        STR(s->anim, "anim");
        STR(s->range, "range");
        STR(s->cast_sound, "stsound");
        STR(s->do_sound, "dosound");
        s->aura    = INT("aura") != 0;
        s->passive = INT("passive") != 0;
        s->left_ok = INT("leftskill") != 0;
        for (int p = 0; p < 8; p++) {
            char name[8];
            snprintf(name, sizeof(name), "Param%d", p + 1);
            s->params[p] = INT(name);
        }
        s->mana         = INT("mana");
        s->level_mana   = INT("lvlmana");
        s->mana_shift   = INT("manashift");
        s->min_mana     = INT("minmana");
        s->to_hit       = INT("ToHit");
        s->level_to_hit = INT("LevToHit");
        s->hit_shift    = INT("HitShift"); // blank is 0 (the poisons: amounts per frame)
        s->min_damage   = INT("MinDam");
        s->max_damage   = INT("MaxDam");
        s->element_min  = INT("EMin");
        s->element_max  = INT("EMax");
        for (int l = 0; l < 5; l++) {
            char name[16];
            snprintf(name, sizeof(name), "MinLevDam%d", l + 1);
            s->min_level_damage[l] = INT(name);
            snprintf(name, sizeof(name), "MaxLevDam%d", l + 1);
            s->max_level_damage[l] = INT(name);
            snprintf(name, sizeof(name), "EMinLev%d", l + 1);
            s->element_min_level[l] = INT(name);
            snprintf(name, sizeof(name), "EMaxLev%d", l + 1);
            s->element_max_level[l] = INT(name);
        }
        STR(s->damage_synergy, "DmgSymPerCalc");
        STR(s->element_synergy, "EDmgSymPerCalc");
        s->element        = SkillData__Element(DataTable_GetString(data, r, COL("EType")), &s->magic);
        s->element_length = INT("ELen");
        for (int l = 0; l < 3; l++) {
            char name[16];
            snprintf(name, sizeof(name), "ELevLen%d", l + 1);
            s->element_level_length[l] = INT(name);
        }
        STR(s->aura_length, "auralencalc");
        STR(s->aura_range, "aurarangecalc");
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            char name[24];
            snprintf(name, sizeof(name), "aurastat%d", a + 1);
            STR(s->aura_stat[a], name);
            snprintf(name, sizeof(name), "aurastatcalc%d", a + 1);
            STR(s->aura_calc[a], name);
        }
        for (int p = 0; p < SKILL_PASSIVE_STATS; p++) {
            char name[24];
            snprintf(name, sizeof(name), "passivestat%d", p + 1);
            STR(s->passive_stat[p], name);
            snprintf(name, sizeof(name), "passivecalc%d", p + 1);
            STR(s->passive_calc[p], name);
        }
        STR(s->pet_max, "petmax");
        for (int c = 0; c < 4; c++) {
            char name[8];
            snprintf(name, sizeof(name), "calc%d", c + 1);
            STR(s->calc[c], name);
        }
        s->kind = SkillData__Kind(s);
    }
#undef COL
#undef STR
#undef INT
    DataTable_Destroy(&data);
}

const SkillRow *SkillData_Get(const char *id) {
    SkillData__Load();
    for (int i = 0; i < row_count && id != NULL; i++) {
        if (strcasecmp(rows[i].id, id) == 0) {
            return &rows[i];
        }
    }
    return NULL;
}

const SkillRow *SkillData_ByRow(const int row) {
    SkillData__Load();
    for (int i = 0; i < row_count; i++) {
        if (rows[i].row == row) {
            return &rows[i];
        }
    }
    return NULL;
}

const SkillRow *SkillData_ForCharacter(const Character *character, const int skill) {
    if (skill >= SKILL_FROM_ITEM) {
        return SkillData_ByRow(skill - SKILL_FROM_ITEM);
    }
    const SkillClass *skills = Skills_ForClass(character->class_id);
    return skill >= 0 && skill < skills->count ? SkillData_Get(skills->skills[skill].id) : NULL;
}

int SkillData_Level(const Character *character, const char *id) {
    if (character == NULL) {
        return 0;
    }
    const SkillClass *skills = Skills_ForClass(character->class_id);
    const int         k      = Skills_Find(skills, id);
    if (k < 0) {
        // Names in calcs don't always match case ("BloodGolem")
        for (int i = 0; i < skills->count; i++) {
            if (strcasecmp(skills->skills[i].id, id) == 0) {
                return i < CHARACTER_SKILLS ? character->skill_levels[i] : 0;
            }
        }
        return 0;
    }
    return k < CHARACTER_SKILLS ? character->skill_levels[k] : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Damage by level

// The game's damage brackets: levels 2-8 add the first per-level amount, 9-16 the second, 17-22, 23-28, then 29 on
static int SkillData__ByLevel(const int base, const int *per_level, const int level) {
    static const int first[5] = {2, 9, 17, 23, 29};
    static const int last[5]  = {8, 16, 22, 28, 1000};
    int              value    = base;
    for (int b = 0; b < 5; b++) {
        if (level >= first[b]) {
            const int top  = level < last[b] ? level : last[b];
            value         += per_level[b] * (top - first[b] + 1);
        }
    }
    return value;
}

static int SkillData__Shift(const int value, const int shift) {
    // In 256ths of a point, scaled by 2^shift ("HitShift" 8 = whole points)
    return (int)(((long long)value << shift) / 256);
}

static int SkillData__ElementRaw(const SkillRow *s, const int level, const bool maximum) {
    return maximum ? SkillData__ByLevel(s->element_max, s->element_max_level, level)
                   : SkillData__ByLevel(s->element_min, s->element_min_level, level);
}

float SkillData_ManaCost(const SkillRow *skill, const int level) {
    if (skill == NULL) {
        return 0.0f;
    }
    // In 256ths of a point, scaled by 2^manashift
    const int raw  = skill->mana + (skill->level_mana * (level > 1 ? level - 1 : 0));
    float     cost = (float)SkillData__Shift(raw * 256, skill->mana_shift) / 256.0f;
    cost           = cost < (float)skill->min_mana ? (float)skill->min_mana : cost;
    return cost > 0.0f ? cost : 0.0f;
}

int SkillData_ToHit(const SkillRow *skill, const int level) {
    return skill == NULL ? 0 : skill->to_hit + (skill->level_to_hit * (level > 1 ? level - 1 : 0));
}

void SkillData_PhysicalDamage(const Character *character, const SkillRow *skill, const int level, int *minimum,
                              int *maximum) {
    *minimum = *maximum = 0;
    if (skill == NULL || level <= 0) {
        return;
    }
    const int synergy =
        skill->damage_synergy[0] != '\0' ? SkillData_Eval(character, skill, level, skill->damage_synergy) : 0;
    *minimum = SkillData__Shift(SkillData__ByLevel(skill->min_damage, skill->min_level_damage, level) * 256,
                                skill->hit_shift) /
               256 * (100 + synergy) / 100;
    *maximum = SkillData__Shift(SkillData__ByLevel(skill->max_damage, skill->max_level_damage, level) * 256,
                                skill->hit_shift) /
               256 * (100 + synergy) / 100;
}

void SkillData_ElementalDamage(const Character *character, const SkillRow *skill, const int level, int *minimum,
                               int *maximum, int *frames) {
    *minimum = *maximum = 0;
    *frames             = 0;
    if (skill == NULL || level <= 0 || skill->element < 0) {
        return;
    }
    const int synergy =
        skill->element_synergy[0] != '\0' ? SkillData_Eval(character, skill, level, skill->element_synergy) : 0;
    // Shifted in 256ths, so poison's tiny per-frame amounts add up over its length. Firestorm's numbers are each
    // flame's whole damage (the game shows 9-18 at the first level), whatever its HitShift says
    const int shift  = skill->do_func == 117 ? 8 : skill->hit_shift;
    long long low    = ((long long)SkillData__ElementRaw(skill, level, false) << shift) * (100 + synergy);
    long long high   = ((long long)SkillData__ElementRaw(skill, level, true) << shift) * (100 + synergy);
    int       length = skill->element_length;
    for (int b = 0; b < 3; b++) {
        static const int first[3] = {2, 9, 17};
        static const int last[3]  = {8, 16, 1000};
        if (level >= first[b]) {
            length += skill->element_level_length[b] * ((level < last[b] ? level : last[b]) - first[b] + 1);
        }
    }
    if (skill->element == ELEMENT_POISON && length > 0) {
        // Poison is given per frame: the whole of it over its length
        low  *= length;
        high *= length;
    }
    *minimum = (int)(low / (256 * 100));
    *maximum = (int)(high / (256 * 100));
    *maximum = *maximum < *minimum ? *minimum : *maximum;
    *frames  = length;
}

// ---------------------------------------------------------------------------------------------------------------------
// Calcs: a tiny expression language. Numbers, + - * /, comparisons, a ? b : c, min(a, b), max(a, b), names (lvl, par1,
// ln12, dm34, edmn, ...), skill('Name'.name) for another skill's, stat('name'.accr) (not kept: 0).

typedef struct Calc {
    const char      *at;
    const Character *character;
    const SkillRow  *skill;
    int              level;
} Calc;

static int Calc__Expr(Calc *c);

static void Calc__Space(Calc *c) {
    while (*c->at == ' ' || *c->at == '\t') {
        c->at++;
    }
}

static int Calc__Linear(const SkillRow *s, const int level, const int a, const int b) {
    return s->params[a] + (s->params[b] * (level - 1));
}

static int Calc__Diminishing(const SkillRow *s, const int level, const int a, const int b) {
    // The game's "dm": from par a towards par b, (110 * lvl) / (lvl + 6) percent of the way
    const int low  = s->params[a];
    const int high = s->params[b];
    return low + ((((110 * level) / (level + 6)) * (high - low)) / 100);
}

// A name's value for a skill at a level
static int Calc__Name(const Character *character, const SkillRow *s, const int level, const char *name) {
    if (s == NULL) {
        return 0;
    }
    if (strcmp(name, "lvl") == 0 || strcmp(name, "blvl") == 0) {
        return level;
    }
    if (strcmp(name, "ulvl") == 0) {
        return character != NULL ? character->level : 1;
    }
    if (strncmp(name, "par", 3) == 0 && name[3] >= '1' && name[3] <= '8' && name[4] == '\0') {
        return s->params[name[3] - '1'];
    }
    if ((strncmp(name, "ln", 2) == 0 || strncmp(name, "dm", 2) == 0) && isdigit((unsigned char)name[2]) &&
        isdigit((unsigned char)name[3]) && name[4] == '\0') {
        const int a = name[2] - '1';
        const int b = name[3] - '1';
        if (a < 0 || a > 7 || b < 0 || b > 7) {
            return 0;
        }
        return name[0] == 'l' ? Calc__Linear(s, level, a, b) : Calc__Diminishing(s, level, a, b);
    }
    if (strcmp(name, "par34") == 0) {
        return s->params[2] * 100 + s->params[3]; // never used for anything that matters
    }
    // Elemental damage: edmn / edmx shifted (whole points), enms / exms in 256ths, enma / exma the same as edmn/edmx
    if (strcmp(name, "edmn") == 0 || strcmp(name, "edns") == 0 || strcmp(name, "enma") == 0) {
        return SkillData__Shift(SkillData__ElementRaw(s, level, false) * 256, s->hit_shift) / 256;
    }
    if (strcmp(name, "edmx") == 0 || strcmp(name, "exma") == 0) {
        return SkillData__Shift(SkillData__ElementRaw(s, level, true) * 256, s->hit_shift) / 256;
    }
    if (strcmp(name, "enms") == 0) {
        return SkillData__Shift(SkillData__ElementRaw(s, level, false), s->hit_shift);
    }
    if (strcmp(name, "exms") == 0) {
        return SkillData__Shift(SkillData__ElementRaw(s, level, true), s->hit_shift);
    }
    if (strcmp(name, "toht") == 0) {
        return SkillData_ToHit(s, level);
    }
    return 0;
}

static int Calc__Primary(Calc *c) {
    Calc__Space(c);
    if (*c->at == '(') {
        c->at++;
        const int value = Calc__Expr(c);
        Calc__Space(c);
        if (*c->at == ')') {
            c->at++;
        }
        return value;
    }
    if (isdigit((unsigned char)*c->at)) {
        return (int)strtol(c->at, (char **)&c->at, 10);
    }
    if (isalpha((unsigned char)*c->at)) {
        char name[32];
        int  n = 0;
        while ((isalnum((unsigned char)*c->at) || *c->at == '_') && n < 31) {
            name[n++] = *c->at++;
        }
        name[n] = '\0';
        Calc__Space(c);
        if (*c->at != '(') {
            return Calc__Name(c->character, c->skill, c->level, name);
        }
        c->at++;
        Calc__Space(c);
        if (*c->at == '\'') {
            // skill('Name'.attr) / stat('name'.accr)
            char other[64];
            int  o = 0;
            c->at++;
            while (*c->at != '\0' && *c->at != '\'' && o < 63) {
                other[o++] = *c->at++;
            }
            other[o] = '\0';
            if (*c->at == '\'') {
                c->at++;
            }
            char attr[16] = "";
            int  a        = 0;
            if (*c->at == '.') {
                c->at++;
                while (isalnum((unsigned char)*c->at) && a < 15) {
                    attr[a++] = *c->at++;
                }
                attr[a] = '\0';
            }
            Calc__Space(c);
            if (*c->at == ')') {
                c->at++;
            }
            if (strcmp(name, "skill") != 0) {
                return 0;
            }
            const SkillRow *s     = SkillData_Get(other);
            const int       level = SkillData_Level(c->character, other);
            if (strcmp(attr, "blvl") == 0 || strcmp(attr, "lvl") == 0) {
                return level;
            }
            return level > 0 || strncmp(attr, "par", 3) == 0 ? Calc__Name(c->character, s, level, attr) : 0;
        }
        // min(a, b) / max(a, b)
        const int first = Calc__Expr(c);
        Calc__Space(c);
        int second = first;
        if (*c->at == ',') {
            c->at++;
            second = Calc__Expr(c);
            Calc__Space(c);
        }
        if (*c->at == ')') {
            c->at++;
        }
        if (strcmp(name, "min") == 0) {
            return first < second ? first : second;
        }
        if (strcmp(name, "max") == 0) {
            return first > second ? first : second;
        }
        return first;
    }
    return 0;
}

static int Calc__Unary(Calc *c) {
    Calc__Space(c);
    if (*c->at == '-') {
        c->at++;
        return -Calc__Unary(c);
    }
    return Calc__Primary(c);
}

static int Calc__Term(Calc *c) {
    int value = Calc__Unary(c);
    for (;;) {
        Calc__Space(c);
        if (*c->at == '*') {
            c->at++;
            value *= Calc__Unary(c);
        } else if (*c->at == '/') {
            c->at++;
            const int d = Calc__Unary(c);
            value       = d != 0 ? value / d : 0;
        } else {
            return value;
        }
    }
}

static int Calc__Sum(Calc *c) {
    int value = Calc__Term(c);
    for (;;) {
        Calc__Space(c);
        if (*c->at == '+') {
            c->at++;
            value += Calc__Term(c);
        } else if (*c->at == '-') {
            c->at++;
            value -= Calc__Term(c);
        } else {
            return value;
        }
    }
}

static int Calc__Compare(Calc *c) {
    const int left = Calc__Sum(c);
    Calc__Space(c);
    if (c->at[0] == '<' && c->at[1] == '=') {
        c->at += 2;
        return left <= Calc__Sum(c);
    }
    if (c->at[0] == '>' && c->at[1] == '=') {
        c->at += 2;
        return left >= Calc__Sum(c);
    }
    if (c->at[0] == '=' && c->at[1] == '=') {
        c->at += 2;
        return left == Calc__Sum(c);
    }
    if (c->at[0] == '!' && c->at[1] == '=') {
        c->at += 2;
        return left != Calc__Sum(c);
    }
    if (*c->at == '<') {
        c->at++;
        return left < Calc__Sum(c);
    }
    if (*c->at == '>') {
        c->at++;
        return left > Calc__Sum(c);
    }
    return left;
}

static int Calc__Expr(Calc *c) {
    const int condition = Calc__Compare(c);
    Calc__Space(c);
    if (*c->at != '?') {
        return condition;
    }
    c->at++;
    const int yes = Calc__Expr(c);
    Calc__Space(c);
    if (*c->at == ':') {
        c->at++;
    }
    const int no = Calc__Expr(c);
    return condition ? yes : no;
}

int SkillData_Eval(const Character *character, const SkillRow *skill, const int level, const char *calc) {
    if (calc == NULL || calc[0] == '\0' || skill == NULL) {
        return 0;
    }
    Calc c = {calc, character, skill, level};
    return Calc__Expr(&c);
}

void SkillData_Release(void) {
    free(rows);
    rows      = NULL;
    row_count = -1;
}
