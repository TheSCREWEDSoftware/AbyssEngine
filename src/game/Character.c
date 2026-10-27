#include "Character.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "Affixes.h"
#include "Inventory.h"
#include "Skills.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

#define EXCEL_EXPERIENCE  "/data/global/excel/experience.txt"
#define CHARACTER_MAGIC   "AbyssCharacter"
#define CHARACTER_VERSION 2 // 2: skills and items
#define CHARACTER_EXT     ".abc"

static const struct {
    const char *name;
    const char *token;
    bool        expansion;
} classes[CHARACTER_CLASS_COUNT] = {
    {"Amazon",      "am", false},
    {"Sorceress",   "so", false},
    {"Necromancer", "ne", false},
    {"Paladin",     "pa", false},
    {"Barbarian",   "ba", false},
    {"Druid",       "dz", true },
    {"Assassin",    "ai", true },
};

static const char *stat_names[STAT_COUNT] = {"Strength", "Dexterity", "Vitality", "Energy"};

// What charstats.txt (and the class's starting weapon in weapons.txt) says about a class
typedef struct ClassInfo {
    int stats[STAT_COUNT];
    int stamina;
    int hp_add;
    int to_hit_factor;
    int life_per_level; // in fourths
    int stamina_per_level;
    int mana_per_level;
    int life_per_vitality;
    int stamina_per_vitality;
    int mana_per_energy;
    int stat_per_level;
    int mana_regen; // seconds
    int run_drain;
} ClassInfo;

static ClassInfo class_info[CHARACTER_CLASS_COUNT];
static uint32_t  experience_table[CHARACTER_MAX_LEVEL + 1]; // [level] = needed to be that level
static bool      tables_loaded = false;
static Character current;

// ---------------------------------------------------------------------------------------------------------------------
// Tables

static void Character__DefaultClass(ClassInfo *info) {
    // Used if the tables can't be read: roughly an Amazon
    memset(info, 0, sizeof(ClassInfo));
    const int stats[STAT_COUNT] = {20, 25, 20, 15};
    memcpy(info->stats, stats, sizeof(stats));
    info->stamina = 84, info->hp_add = 30, info->life_per_level = 8, info->stamina_per_level = 4;
    info->mana_per_level = 6, info->life_per_vitality = 12, info->stamina_per_vitality = 4, info->mana_per_energy = 6;
    info->stat_per_level = 5, info->mana_regen = 120, info->run_drain = 20;
}

static void Character__LoadTables(void) {
    if (tables_loaded) {
        return;
    }
    tables_loaded = true;
    for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
        Character__DefaultClass(&class_info[c]);
    }

    DataTable *stats = DataTable_Load(EXCEL_CHARSTATS);
    if (stats != NULL) {
#define COL(n) DataTable_GetColumnIndex(stats, n)
        const int c_class = COL("class");
        for (int row = 0; row < DataTable_GetRowCount(stats); row++) {
            int class_id = -1;
            for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
                if (strcmp(DataTable_GetString(stats, row, c_class), classes[c].name) == 0) {
                    class_id = c;
                }
            }
            if (class_id < 0) {
                continue;
            }
            ClassInfo *info             = &class_info[class_id];
            info->stats[STAT_STRENGTH]  = DataTable_GetInt(stats, row, COL("str"));
            info->stats[STAT_DEXTERITY] = DataTable_GetInt(stats, row, COL("dex"));
            info->stats[STAT_VITALITY]  = DataTable_GetInt(stats, row, COL("vit"));
            info->stats[STAT_ENERGY]    = DataTable_GetInt(stats, row, COL("int"));
            info->stamina               = DataTable_GetInt(stats, row, COL("stamina"));
            info->hp_add                = DataTable_GetInt(stats, row, COL("hpadd"));
            info->to_hit_factor         = DataTable_GetInt(stats, row, COL("ToHitFactor"));
            info->life_per_level        = DataTable_GetInt(stats, row, COL("LifePerLevel"));
            info->stamina_per_level     = DataTable_GetInt(stats, row, COL("StaminaPerLevel"));
            info->mana_per_level        = DataTable_GetInt(stats, row, COL("ManaPerLevel"));
            info->life_per_vitality     = DataTable_GetInt(stats, row, COL("LifePerVitality"));
            info->stamina_per_vitality  = DataTable_GetInt(stats, row, COL("StaminaPerVitality"));
            info->mana_per_energy       = DataTable_GetInt(stats, row, COL("ManaPerMagic"));
            info->stat_per_level        = DataTable_GetInt(stats, row, COL("StatPerLevel"));
            info->mana_regen            = DataTable_GetInt(stats, row, COL("ManaRegen"));
            info->run_drain             = DataTable_GetInt(stats, row, COL("RunDrain"));
            info->stat_per_level        = info->stat_per_level > 0 ? info->stat_per_level : 5;
            info->mana_regen            = info->mana_regen > 0 ? info->mana_regen : 120;
        }
#undef COL
    }
    DataTable_Destroy(&stats);

    // experience.txt: the row for level L holds what's needed to reach level L + 1
    for (int level = 0; level <= CHARACTER_MAX_LEVEL; level++) {
        experience_table[level] = level <= 1 ? 0 : (uint32_t)(500.0 * level * level * level / 8.0); // fallback
    }
    DataTable *table = DataTable_Load(EXCEL_EXPERIENCE);
    if (table != NULL) {
        const int c_level  = DataTable_GetColumnIndex(table, "Level");
        const int c_amazon = DataTable_GetColumnIndex(table, "Amazon");
        for (int row = 0; row < DataTable_GetRowCount(table); row++) {
            const char *level_text = DataTable_GetString(table, row, c_level);
            if (!isdigit((unsigned char)level_text[0])) {
                continue; // "MaxLvl"
            }
            const int level = atoi(level_text) + 1;
            if (level >= 1 && level <= CHARACTER_MAX_LEVEL) {
                experience_table[level] = (uint32_t)strtoul(DataTable_GetString(table, row, c_amazon), NULL, 10);
            }
        }
        DataTable_Destroy(&table);
    }
    experience_table[1] = 0;
}

void Character_Release(void) { tables_loaded = false; }

// ---------------------------------------------------------------------------------------------------------------------
// Classes and numbers

const char *Character_ClassName(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT ? classes[class_id].name : "?";
}

const char *Character_ClassToken(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT ? classes[class_id].token : "am";
}

bool Character_ClassIsExpansion(const CharacterClass class_id) {
    return class_id < CHARACTER_CLASS_COUNT && classes[class_id].expansion;
}

const char *Character_StatName(const CharacterStat stat) { return stat < STAT_COUNT ? stat_names[stat] : "?"; }

static const ClassInfo *Character__Info(const Character *character) {
    Character__LoadTables();
    return &class_info[character->class_id < CHARACTER_CLASS_COUNT ? character->class_id : 0];
}

void Character_Create(Character *character, const char *name, const CharacterClass class_id, const bool expansion,
                      const bool hardcore) {
    memset(character, 0, sizeof(Character));
    snprintf(character->name, sizeof(character->name), "%s", name);
    character->class_id   = class_id < CHARACTER_CLASS_COUNT ? class_id : CHARACTER_AMAZON;
    character->expansion  = expansion || Character_ClassIsExpansion(class_id);
    character->hardcore   = hardcore;
    character->level      = 1;
    const ClassInfo *info = Character__Info(character);
    memcpy(character->stats, info->stats, sizeof(character->stats));
    character->life        = Character_MaxLife(character);
    character->mana        = Character_MaxMana(character);
    character->stamina     = Character_MaxStamina(character);
    character->left_skill  = SKILL_ATTACK;
    character->right_skill = SKILL_ATTACK;
    Inventory_GiveStartingItems(character);
}

// Starting value, plus so much (in fourths) per point above the class's start and per level after the first
static int Character__Grow(const int start, const int points, const int per_point, const int levels,
                           const int per_level) {
    return start + ((points * per_point) / 4) + ((levels * per_level) / 4);
}

int Character_Stat(const Character *character, const CharacterStat stat) {
    static const ItemModType mods[STAT_COUNT] = {MOD_STRENGTH, MOD_DEXTERITY, MOD_VITALITY, MOD_ENERGY};
    return stat < STAT_COUNT ? character->stats[stat] + Affixes_Total(character, mods[stat]) : 0;
}

int Character_MaxLife(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stats[STAT_VITALITY] + info->hp_add,
                           Character_Stat(character, STAT_VITALITY) - info->stats[STAT_VITALITY],
                           info->life_per_vitality, character->level - 1, info->life_per_level) +
           Affixes_Total(character, MOD_LIFE);
}

int Character_MaxMana(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stats[STAT_ENERGY], Character_Stat(character, STAT_ENERGY) - info->stats[STAT_ENERGY],
                           info->mana_per_energy, character->level - 1, info->mana_per_level) +
           Affixes_Total(character, MOD_MANA);
}

int Character_MaxStamina(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    return Character__Grow(info->stamina, Character_Stat(character, STAT_VITALITY) - info->stats[STAT_VITALITY],
                           info->stamina_per_vitality, character->level - 1, info->stamina_per_level) +
           Affixes_Total(character, MOD_STAMINA);
}

int Character_ItemDefense(const Item *item) {
    return item->defense + (item->defense * Affixes_ItemTotal(item, MOD_DEFENSE_PCT) / 100);
}

// Dexterity's share, and what's worn (each item with its own enhanced defense, and flat defense from any)
int Character_Defense(const Character *character) {
    int defense = Character_Stat(character, STAT_DEXTERITY) / 4;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        defense += character->equipped[s].code[0] != '\0' ? Character_ItemDefense(&character->equipped[s]) : 0;
    }
    return defense + Affixes_Total(character, MOD_DEFENSE);
}

int Character_AttackRating(const Character *character) {
    const ClassInfo *info = Character__Info(character);
    int              ar   = (Character_Stat(character, STAT_DEXTERITY) * 5) - 35 + info->to_hit_factor +
             Affixes_Total(character, MOD_ATTACK_RATING);
    ar += ar * Affixes_Total(character, MOD_ATTACK_RATING_PCT) / 100;
    return ar > 1 ? ar : 1;
}

// The weapon item held (the weapon hand, or the other hand's when that's empty)
static const Item *Character__WeaponItem(const Character *character) {
    for (int s = EQUIP_RIGHT_ARM; s <= EQUIP_LEFT_ARM; s++) {
        const ItemBase *base = character->equipped[s].code[0] != '\0' ? Items_Find(character->equipped[s].code) : NULL;
        if (base != NULL && base->kind == ITEM_KIND_WEAPON) {
            return &character->equipped[s];
        }
    }
    return NULL;
}

void Character_Damage(const Character *character, int *minimum, int *maximum) {
    // The weapon held (two-handed ones, like staves, have only their two-hand damage), or bare hands' 1-2
    const ItemBase *weapon     = Inventory_Weapon(character);
    const bool      two_hand   = weapon != NULL && (weapon->max_damage <= 0 || weapon->two_handed);
    int             weapon_min = weapon == NULL ? 1 : (two_hand ? weapon->two_hand_min : weapon->min_damage);
    int             weapon_max = weapon == NULL ? 2 : (two_hand ? weapon->two_hand_max : weapon->max_damage);
    if (weapon != NULL && weapon_max <= 0) { // two-handed with one-hand damage too (swords): the one-hand numbers
        weapon_min = weapon->min_damage;
        weapon_max = weapon->max_damage;
    }
    // The weapon's own enhanced damage and added damage
    const Item *held        = Character__WeaponItem(character);
    const int   weapon_pct  = held != NULL ? Affixes_ItemTotal(held, MOD_DAMAGE_PCT) : 0;
    weapon_min             += weapon_min * weapon_pct / 100;
    weapon_max             += weapon_max * weapon_pct / 100;
    // Strength (and for some weapons dexterity) adds a percentage to the weapon's damage, and so does enhanced damage
    // on other things worn
    const int str_bonus = weapon != NULL && weapon->str_bonus > 0 ? weapon->str_bonus : 100;
    const int dex_bonus = weapon != NULL ? weapon->dex_bonus : 0;
    const int bonus     = ((Character_Stat(character, STAT_STRENGTH) * str_bonus) / 100) +
                      ((Character_Stat(character, STAT_DEXTERITY) * dex_bonus) / 100) +
                      (Affixes_Total(character, MOD_DAMAGE_PCT) - weapon_pct);
    // Added damage (fire, cold, lightning and poison are their own: Character_ElementDamage)
    *minimum = weapon_min + ((weapon_min * bonus) / 100) + Affixes_Total(character, MOD_DAMAGE_MIN);
    *maximum = weapon_max + ((weapon_max * bonus) / 100) + Affixes_Total(character, MOD_DAMAGE_MAX);
    *minimum = *minimum > 0 ? *minimum : 1;
    *maximum = *maximum > *minimum ? *maximum : *minimum;
}

void Character_ElementDamage(const Character *character, const Element element, int *minimum, int *maximum) {
    static const ItemModType mods[ELEMENT_COUNT][2] = {
        {MOD_TYPE_COUNT,    MOD_TYPE_COUNT   },
        {MOD_FIRE_MIN,      MOD_FIRE_MAX     },
        {MOD_COLD_MIN,      MOD_COLD_MAX     },
        {MOD_LIGHTNING_MIN, MOD_LIGHTNING_MAX},
        {MOD_POISON,        MOD_POISON       },
    };
    *minimum = 0;
    *maximum = 0;
    if (element <= ELEMENT_PHYSICAL || element >= ELEMENT_COUNT) {
        return;
    }
    *minimum = Affixes_Total(character, mods[element][0]);
    *maximum = Affixes_Total(character, mods[element][1]);
    *maximum = *maximum > *minimum ? *maximum : *minimum;
}

int Character_Resist(const Character *character, const Element element, const int penalty) {
    static const ItemModType mods[ELEMENT_COUNT] = {MOD_TYPE_COUNT, MOD_RES_FIRE, MOD_RES_COLD, MOD_RES_LIGHTNING,
                                                    MOD_RES_POISON};
    if (element <= ELEMENT_PHYSICAL || element >= ELEMENT_COUNT) {
        return 0;
    }
    const int resist = Affixes_Total(character, mods[element]) + Affixes_Total(character, MOD_RES_ALL) - penalty;
    return resist < RESIST_MAX ? resist : RESIST_MAX;
}

const char *Character_WeaponName(const Character *character) {
    const ItemBase *weapon = Inventory_Weapon(character);
    return weapon != NULL ? weapon->name : "Bare hands";
}

int Character_ManaRegenSeconds(const Character *character) { return Character__Info(character)->mana_regen; }

int Character_RunDrain(const Character *character) { return Character__Info(character)->run_drain; }

uint32_t Character_ExperienceFor(const int level) {
    Character__LoadTables();
    if (level <= 1) {
        return 0;
    }
    return experience_table[level > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : level];
}

int Character_AddExperience(Character *character, const uint32_t experience) {
    const uint32_t cap = Character_ExperienceFor(CHARACTER_MAX_LEVEL);
    character->experience =
        (character->experience + experience > cap || character->experience + experience < character->experience)
            ? cap
            : character->experience + experience;
    int gained = 0;
    while (character->level < CHARACTER_MAX_LEVEL &&
           character->experience >= Character_ExperienceFor(character->level + 1)) {
        character->level++;
        character->stat_points  += Character__Info(character)->stat_per_level;
        character->skill_points += 1;
        gained++;
    }
    if (gained > 0) {
        character->life    = Character_MaxLife(character);
        character->mana    = Character_MaxMana(character);
        character->stamina = Character_MaxStamina(character);
    }
    return gained;
}

bool Character_SpendStatPoint(Character *character, const CharacterStat stat) {
    if (character->stat_points <= 0 || stat >= STAT_COUNT) {
        return false;
    }
    // Keep the same share of life/mana/stamina filled
    const int old_life    = Character_MaxLife(character);
    const int old_mana    = Character_MaxMana(character);
    const int old_stamina = Character_MaxStamina(character);
    character->stats[stat]++;
    character->stat_points--;
    character->life    += Character_MaxLife(character) - old_life;
    character->mana    += Character_MaxMana(character) - old_mana;
    character->stamina += Character_MaxStamina(character) - old_stamina;
    return true;
}

int Character_HitChance(const int attack_rating, const int attacker_level, const int defense,
                        const int defender_level) {
    const double ar     = attack_rating > 1 ? attack_rating : 1;
    const double def    = defense > 0 ? defense : 0;
    const double alvl   = attacker_level > 1 ? attacker_level : 1;
    const double dlvl   = defender_level > 1 ? defender_level : 1;
    const int    chance = (int)(200.0 * (ar / (ar + def)) * (alvl / (alvl + dlvl)));
    return chance < 5 ? 5 : (chance > 95 ? 95 : chance);
}

bool Character_IsValidName(const char *name, const char **reason) {
    const size_t length = strlen(name);
    const char  *why    = NULL;
    int          marks  = 0;
    if (length < 2) {
        why = "Your name must be at least 2 letters long.";
    } else if (length > CHARACTER_NAME_MAX - 1) {
        why = "Your name can be at most 15 letters long.";
    } else {
        for (size_t i = 0; i < length && why == NULL; i++) {
            const char c = name[i];
            if (c == '-' || c == '_') {
                marks++;
                if (i == 0 || i == length - 1) {
                    why = "Your name can't start or end with '-' or '_'.";
                }
            } else if (!isalpha((unsigned char)c)) {
                why = "Your name can only contain letters, and one '-' or '_'.";
            }
        }
        if (why == NULL && marks > 1) {
            why = "Your name can only contain one '-' or '_'.";
        }
    }
    if (reason != NULL) {
        *reason = why;
    }
    return why == NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Saving

static const char *Character__Folder(void) {
    static char folder[4096];
    snprintf(folder, sizeof(folder), "%s/save", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
    _mkdir(folder);
#else
    mkdir(folder, 0755);
#endif
    return folder;
}

static void Character__Path(const char *name, char *path, const size_t size) {
    snprintf(path, size, "%s/%s%s", Character__Folder(), name, CHARACTER_EXT);
}

static bool (*save_hook)(Character *character) = NULL;

// Appends to a text buffer like fprintf (the end is cut if it doesn't fit; the length counts what would have been)
typedef struct CharacterText {
    char  *text;
    size_t size;
    size_t length;
} CharacterText;

static void Character__Print(CharacterText *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const size_t room  = out->length < out->size ? out->size - out->length : 0;
    const int    added = vsnprintf(room > 0 ? out->text + out->length : NULL, room, format, args);
    va_end(args);
    if (added > 0) {
        out->length += (size_t)added;
    }
}

size_t Character_WriteText(const Character *character, char *text, const size_t size) {
    CharacterText  out  = {text, size, 0};
    CharacterText *file = &out;
    if (size > 0) {
        text[0] = '\0';
    }
    Character__Print(file, "%s %d\n", CHARACTER_MAGIC, CHARACTER_VERSION);
    Character__Print(file, "name %s\n", character->name);
    Character__Print(file, "class %s\n", Character_ClassName(character->class_id));
    Character__Print(file, "expansion %d\n", character->expansion ? 1 : 0);
    Character__Print(file, "hardcore %d\n", character->hardcore ? 1 : 0);
    Character__Print(file, "dead %d\n", character->dead ? 1 : 0);
    Character__Print(file, "level %d\n", character->level);
    Character__Print(file, "experience %lu\n", (unsigned long)character->experience);
    for (int s = 0; s < STAT_COUNT; s++) {
        Character__Print(file, "%s %d\n", stat_names[s], character->stats[s]);
    }
    Character__Print(file, "statpoints %d\n", character->stat_points);
    Character__Print(file, "skillpoints %d\n", character->skill_points);
    Character__Print(file, "life %d\n", character->life);
    Character__Print(file, "mana %d\n", character->mana);
    Character__Print(file, "stamina %d\n", character->stamina);
    Character__Print(file, "act %d\n", character->act);
    Character__Print(file, "lastplayed %llu\n", (unsigned long long)character->last_played);
    // Skills by name, so they survive changes to the tables' order
    const SkillClass *skills = Skills_ForClass(character->class_id);
    for (int k = 0; k < skills->count && k < CHARACTER_SKILLS; k++) {
        if (character->skill_levels[k] > 0) {
            Character__Print(file, "skill %s %d\n", skills->skills[k].id, character->skill_levels[k]);
        }
    }
    const int hands[2] = {character->left_skill, character->right_skill};
    for (int h = 0; h < 2; h++) {
        Character__Print(file, "%s %s\n", h == 0 ? "leftskill" : "rightskill",
                         hands[h] >= 0 && hands[h] < skills->count ? skills->skills[hands[h]].id : "attack");
    }
    // Items: "item <where> <code> <defense> <quantity>", where is "inv x y", "equip <slot>" or "belt <slot>"
    Character__Print(file, "gold %d\n", character->gold);
    Character__Print(file, "items %d\n", character->inventory_count);
    for (int i = 0; i < character->inventory_count; i++) {
        const InventoryItem *it = &character->inventory[i];
        // The page last, so builds before pages read the line too (onto the first page)
        char magic[400];
        Affixes_Format(&it->item, magic, sizeof(magic)); // after the page (older builds read up to it)
        Character__Print(file, "item inv %d %d %s %d %d %d%s\n", it->x, it->y, it->item.code, it->item.defense,
                         it->item.quantity, it->page, magic);
    }
    for (int e = 0; e < EQUIP_COUNT; e++) {
        const Item *it = &character->equipped[e];
        if (it->code[0] != '\0') {
            char magic[400];
            Affixes_Format(it, magic, sizeof(magic));
            Character__Print(file, "item equip %d %s %d %d%s\n", e, it->code, it->defense, it->quantity, magic);
        }
    }
    for (int b = 0; b < BELT_MAX; b++) {
        const Item *it = &character->belt[b];
        if (it->code[0] != '\0') {
            char magic[400];
            Affixes_Format(it, magic, sizeof(magic));
            Character__Print(file, "item belt %d %s %d %d%s\n", b, it->code, it->defense, it->quantity, magic);
        }
    }
    if (character->realm[0] != '\0') {
        Character__Print(file, "realm %s\n", character->realm);
    }
    Character__Print(file, "end\n");
    return out.length;
}

bool Character_Save(Character *character) {
    character->last_played = (uint64_t)time(NULL);
    if (save_hook != NULL) {
        return save_hook(character); // a dedicated server keeps it
    }
    char path[4200];
    Character__Path(character->name, path, sizeof(path));
    char *text = malloc(CHARACTER_TEXT_MAX);
    FILE *file = text != NULL ? fopen(path, "w") : NULL;
    if (file == NULL) {
        LOG_WARN("Could not save '%s': %s", path, strerror(errno));
        free(text);
        return false;
    }
    const size_t length = Character_WriteText(character, text, CHARACTER_TEXT_MAX);
    fwrite(text, 1, length < CHARACTER_TEXT_MAX ? length : CHARACTER_TEXT_MAX - 1, file);
    const bool ok = ferror(file) == 0 && length < CHARACTER_TEXT_MAX;
    fclose(file);
    free(text);
    return ok;
}

void Character_SetSaveHook(bool (*hook)(Character *character)) { save_hook = hook; }

// The next line of text into line (without its end); false at the end
static bool Character__Line(const char **at, char *line, const size_t size) {
    if (**at == '\0') {
        return false;
    }
    const char  *end    = *at + strcspn(*at, "\n");
    const size_t length = (size_t)(end - *at) < size - 1 ? (size_t)(end - *at) : size - 1;
    memcpy(line, *at, length);
    line[length] = '\0';
    *at          = *end == '\n' ? end + 1 : end;
    return true;
}

bool Character_ReadText(const char *text, Character *character) {
    const char *at = text;
    memset(character, 0, sizeof(Character));
    char line[256];
    int  version = 0;
    if (!Character__Line(&at, line, sizeof(line)) || sscanf(line, CHARACTER_MAGIC " %d", &version) != 1 ||
        version > CHARACTER_VERSION) {
        return false;
    }
    character->level       = 1;
    character->left_skill  = SKILL_ATTACK;
    character->right_skill = SKILL_ATTACK;
    bool has_items         = false;
    while (Character__Line(&at, line, sizeof(line))) {
        line[strcspn(line, "\r")] = '\0';
        char key[32];
        char value[128] = "";
        if (sscanf(line, "%31s %127[^\n]", key, value) < 1 || strcmp(key, "end") == 0) {
            continue;
        }
        const int number = atoi(value);
        if (strcmp(key, "name") == 0) {
            snprintf(character->name, sizeof(character->name), "%.*s", (int)sizeof(character->name) - 1, value);
        } else if (strcmp(key, "class") == 0) {
            for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
                if (strcmp(value, classes[c].name) == 0) {
                    character->class_id = (CharacterClass)c;
                }
            }
        } else if (strcmp(key, "expansion") == 0) {
            character->expansion = number != 0;
        } else if (strcmp(key, "hardcore") == 0) {
            character->hardcore = number != 0;
        } else if (strcmp(key, "dead") == 0) {
            character->dead = number != 0;
        } else if (strcmp(key, "level") == 0) {
            character->level = number < 1 ? 1 : (number > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : number);
        } else if (strcmp(key, "experience") == 0) {
            character->experience = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "statpoints") == 0) {
            character->stat_points = number;
        } else if (strcmp(key, "skillpoints") == 0) {
            character->skill_points = number;
        } else if (strcmp(key, "life") == 0) {
            character->life = number;
        } else if (strcmp(key, "mana") == 0) {
            character->mana = number;
        } else if (strcmp(key, "stamina") == 0) {
            character->stamina = number;
        } else if (strcmp(key, "act") == 0) {
            character->act = number < 0 ? 0 : (number > 4 ? 4 : number);
        } else if (strcmp(key, "lastplayed") == 0) {
            character->last_played = strtoull(value, NULL, 10);
        } else if (strcmp(key, "skill") == 0) {
            // "skill <id> <points>": the id can have spaces ("Fire Bolt"), the points are after the last one
            char *space = strrchr(value, ' ');
            if (space != NULL) {
                *space       = '\0';
                const int k  = Skills_Find(Skills_ForClass(character->class_id), value);
                const int pt = atoi(space + 1);
                if (k >= 0 && k < CHARACTER_SKILLS) {
                    character->skill_levels[k] = (uint8_t)(pt < 0 ? 0 : (pt > SKILL_MAX_LEVEL ? SKILL_MAX_LEVEL : pt));
                }
            }
        } else if (strcmp(key, "leftskill") == 0 || strcmp(key, "rightskill") == 0) {
            const int k = Skills_Find(Skills_ForClass(character->class_id), value);
            *(strcmp(key, "leftskill") == 0 ? &character->left_skill : &character->right_skill) =
                k >= 0 ? k : SKILL_ATTACK;
        } else if (strcmp(key, "realm") == 0) {
            snprintf(character->realm, sizeof(character->realm), "%.39s", value);
        } else if (strcmp(key, "gold") == 0) {
            character->gold = number > 0 ? number : 0;
        } else if (strcmp(key, "items") == 0) {
            has_items = true;
        } else if (strcmp(key, "item") == 0) {
            char where[8];
            char code[ITEM_CODE_MAX];
            int  a = 0;
            int  b = 0;
            Item item;
            memset(&item, 0, sizeof(item));
            int page = 0;
            int used = 0;
            if (sscanf(value, "inv %d %d %7s %d %d %d%n", &a, &b, code, &item.defense, &item.quantity, &page, &used) >=
                5) {
                snprintf(item.code, sizeof(item.code), "%s", code);
                if (used > 0) {
                    Affixes_Parse(&item, value + used); // magic, after the page
                }
                Inventory_PlaceOnPage(character, &item, a, b, page);
            } else if (sscanf(value, "%7s %d %7s %d %d%n", where, &a, code, &item.defense, &item.quantity, &used) ==
                           5 &&
                       Items_Find(code) != NULL) {
                snprintf(item.code, sizeof(item.code), "%s", code);
                Affixes_Parse(&item, value + used);
                if (strcmp(where, "equip") == 0 && a >= 0 && a < EQUIP_COUNT) {
                    character->equipped[a] = item;
                } else if (strcmp(where, "belt") == 0 && a >= 0 && a < BELT_MAX) {
                    character->belt[a] = item;
                }
            }
        } else {
            for (int s = 0; s < STAT_COUNT; s++) {
                if (strcmp(key, stat_names[s]) == 0) {
                    character->stats[s] = number;
                }
            }
        }
    }
    if (!has_items && character->name[0] != '\0') {
        Inventory_GiveStartingItems(character); // saved before there were items
    }
    return character->name[0] != '\0';
}

static bool Character__Load(const char *path, Character *character) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    char        *text   = malloc(CHARACTER_TEXT_MAX);
    const size_t length = text != NULL ? fread(text, 1, CHARACTER_TEXT_MAX - 1, file) : 0;
    fclose(file);
    if (text == NULL) {
        return false;
    }
    text[length]    = '\0';
    const bool read = Character_ReadText(text, character);
    free(text);
    return read;
}

bool Character_Exists(const char *name) {
    Character *list  = NULL;
    const int  count = Character_ListSaved(&list);
    bool       found = false;
    for (int i = 0; i < count && !found; i++) {
        // Names are unique regardless of case (they're file names on Windows)
        const char *a = list[i].name;
        const char *b = name;
        for (; *a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b); a++, b++) {
        }
        found = *a == '\0' && *b == '\0';
    }
    free(list);
    return found;
}

bool Character_Delete(const char *name) {
    char path[4200];
    Character__Path(name, path, sizeof(path));
    return remove(path) == 0;
}

static void Character__Add(Character **list, int *count, int *capacity, const char *folder, const char *file_name) {
    const size_t len = strlen(file_name);
    if (len <= strlen(CHARACTER_EXT) || strcmp(file_name + len - strlen(CHARACTER_EXT), CHARACTER_EXT) != 0) {
        return;
    }
    char path[4400];
    snprintf(path, sizeof(path), "%s/%s", folder, file_name);
    Character character;
    if (!Character__Load(path, &character)) {
        return;
    }
    if (*count == *capacity) {
        *capacity        = *capacity > 0 ? *capacity * 2 : 8;
        Character *grown = realloc(*list, sizeof(Character) * (size_t)*capacity);
        FAIL_IF_NULL(grown);
        *list = grown;
    }
    (*list)[(*count)++] = character;
}

static int Character__CompareRecent(const void *a, const void *b) {
    const Character *x = a;
    const Character *y = b;
    return x->last_played < y->last_played ? 1 : (x->last_played > y->last_played ? -1 : strcmp(x->name, y->name));
}

int Character_ListSaved(Character **characters) {
    *characters          = NULL;
    int         count    = 0;
    int         capacity = 0;
    const char *folder   = Character__Folder();
#ifdef _WIN32
    char pattern[4200];
    snprintf(pattern, sizeof(pattern), "%s\\*%s", folder, CHARACTER_EXT);
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            Character__Add(characters, &count, &capacity, folder, found.cFileName);
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(folder);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            Character__Add(characters, &count, &capacity, folder, entry->d_name);
        }
        closedir(dir);
    }
#endif
    if (count > 1) {
        qsort(*characters, (size_t)count, sizeof(Character), Character__CompareRecent);
    }
    return count;
}

Character *Character_Current(void) { return &current; }

void Character_SetCurrent(const Character *character) { current = *character; }
