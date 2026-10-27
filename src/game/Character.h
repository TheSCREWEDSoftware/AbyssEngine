#ifndef ABYSS_CHARACTER_H
#define ABYSS_CHARACTER_H

#include "Items.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A Single Player character: class, level, experience and stats, saved in our own text format (one file per
// character in the "save" folder next to abyss.ini). Not compatible with the original game's .d2s saves.
//
// Numbers follow the game's tables (normal difficulty): starting stats, life, mana and stamina per point and per level
// from charstats.txt, the experience needed for each level from experience.txt.

#define CHARACTER_NAME_MAX  16 // 15 letters
#define CHARACTER_MAX_LEVEL 99
#define CHARACTER_SKILLS    40 // per class (Skills.h's SKILLS_PER_CLASS_MAX)
#define INVENTORY_W         10 // the inventory grid, in cells
#define INVENTORY_H         4
#define INVENTORY_PAGE      (INVENTORY_W * INVENTORY_H)
#define INVENTORY_MAX       1024 // items on all the pages together (a page holds 40 at most)
#define INVENTORY_PAGES_MAX 255
#define CHARACTER_QUESTS    32 // (Quests.h QUEST_COUNT fits)
#define STASH_W             6  // the stash in town (the expansion's big one), in cells
#define STASH_H             8
#define STASH_PAGE          (STASH_W * STASH_H)
#define STASH_MAX           512 // items on all the stash's pages together (a page holds 48 at most)
#define BELT_MAX            16  // four rows of four, with the biggest belt
#define CUBE_W              3   // the Horadric Cube's inside, in cells
#define CUBE_H              4
#define CUBE_MAX            (CUBE_W * CUBE_H)

// What a hireling wears (their inventory screen): helm, body armor, weapon, shield (only the act 3 sorcerers)
typedef enum HirelingSlot { HIRE_HEAD, HIRE_TORSO, HIRE_WEAPON, HIRE_SHIELD, HIRE_SLOTS } HirelingSlot;

typedef enum CharacterClass {
    CHARACTER_AMAZON,
    CHARACTER_SORCERESS,
    CHARACTER_NECROMANCER,
    CHARACTER_PALADIN,
    CHARACTER_BARBARIAN,
    CHARACTER_DRUID,
    CHARACTER_ASSASSIN,
    CHARACTER_CLASS_COUNT
} CharacterClass;

typedef enum CharacterStat { STAT_STRENGTH, STAT_DEXTERITY, STAT_VITALITY, STAT_ENERGY, STAT_COUNT } CharacterStat;

// Kinds of damage, and the resistances against them
typedef enum Element {
    ELEMENT_PHYSICAL,
    ELEMENT_FIRE,
    ELEMENT_COLD,
    ELEMENT_LIGHTNING,
    ELEMENT_POISON,
    ELEMENT_COUNT
} Element;

#define RESIST_MAX 75 // the most a resistance can be, like the game

// What skills add right now (passives, buffs, the aura on): worked out by the game each moment, never saved
typedef enum CharacterBonus {
    BONUS_DEFENSE_PCT,
    BONUS_DAMAGE_PCT,
    BONUS_ATTACK_PCT,
    BONUS_ATTACK, // flat attack rating
    BONUS_LIFE_PCT,
    BONUS_MANA_PCT,
    BONUS_STAMINA_PCT,
    BONUS_SPEED_PCT,    // walking and running
    BONUS_ATTACK_SPEED, // percent
    BONUS_RESIST_FIRE,  // the four resistances, in Element order from fire
    BONUS_RESIST_COLD,
    BONUS_RESIST_LIGHTNING,
    BONUS_RESIST_POISON,
    BONUS_MAX_RESIST_FIRE,
    BONUS_MAX_RESIST_COLD,
    BONUS_MAX_RESIST_LIGHTNING,
    BONUS_MAX_RESIST_POISON,
    BONUS_MANA_REGEN_PCT,
    BONUS_BLOCK,
    BONUS_FIRE_MIN, // added to each blow
    BONUS_FIRE_MAX,
    BONUS_COLD_MIN,
    BONUS_COLD_MAX,
    BONUS_LIGHTNING_MIN,
    BONUS_LIGHTNING_MAX,
    BONUS_POISON_MIN,
    BONUS_POISON_MAX,
    BONUS_STRENGTH,
    BONUS_DEXTERITY,
    BONUS_THORNS_PCT,     // melee damage taken, given back
    BONUS_CRITICAL,       // chance of double damage
    BONUS_DODGE,          // chance to dodge a blow
    BONUS_AVOID,          // chance to avoid a missile
    BONUS_PIERCE,         // chance a missile goes on through
    BONUS_FIRE_SKILL_PCT, // the masteries: more damage from those skills
    BONUS_LIGHTNING_SKILL_PCT,
    BONUS_COLD_PIERCE, // lowers monsters' cold resistance
    BONUS_ALL_SKILLS,  // +N to the skills you have points in (the Skill Shrine)
    BONUS_COUNT
} CharacterBonus;

// One item a character has (a base item from Items.h, maybe with magic: Affixes.h). code[0] == '\0': none.
typedef struct Item {
    char    code[ITEM_CODE_MAX];
    int     defense;  // armor: rolled between the base's minimum and maximum
    int     quantity; // stacks (books, javelins), gold's amount
    uint8_t quality;  // ItemQuality
    // Its names, as table row + 1 (0: none): magic: the prefix (MagicPrefix.txt) and suffix (MagicSuffix.txt); rare:
    // the two made-up names (RarePrefix.txt, RareSuffix.txt); unique: name1 is its UniqueItems.txt row
    int16_t name1;
    int16_t name2;
    uint8_t mod_count;
    ItemMod mods[ITEM_MODS_MAX];
    // Sockets: how many it has, and the gems, runes and jewels in them (their codes; their magic is in mods)
    uint8_t sockets;
    uint8_t socketed_count;
    char    socketed[ITEM_SOCKETS_MAX][ITEM_CODE_MAX];
    // Durability (weapons and armor): it wears down with use, at 0 it's broken (does nothing until repaired). max 0:
    // it doesn't wear down
    int16_t durability;
    int16_t max_durability;
    bool    unidentified; // magic found on the ground: its magic is unknown (and does nothing) until identified
} Item;

static inline bool Item_IsBroken(const Item *item) { return item->max_durability > 0 && item->durability <= 0; }
// Whether what's worn counts: not broken, and its magic only once it's identified
static inline bool Item_Works(const Item *item) { return item->code[0] != '\0' && !Item_IsBroken(item); }

typedef struct InventoryItem {
    Item item;
    int  x; // top left cell
    int  y;
    int  page; // 0.. (Options > Gameplay > Inventory pages; a multiplayer game's own setting)
} InventoryItem;

typedef struct Character {
    char           name[CHARACTER_NAME_MAX];
    CharacterClass class_id;
    bool           expansion;
    bool           hardcore;
    bool           dead; // hardcore characters stay dead
    int            level;
    uint32_t       experience;
    int            stats[STAT_COUNT]; // strength, dexterity, vitality, energy
    int            stat_points;       // to spend
    int            skill_points;
    int            life; // current
    int            mana;
    int            stamina;
    int            act;         // 0..4: the town the game starts in
    uint64_t       last_played; // seconds since 1970
    // Skills: points in each of the class's skills (Skills.h's order), and what the mouse buttons use
    uint8_t skill_levels[CHARACTER_SKILLS];
    int     left_skill; // a skill's index, or SKILL_ATTACK (-1)
    int     right_skill;
    // Items
    InventoryItem inventory[INVENTORY_MAX];
    int           inventory_count;
    Item          equipped[EQUIP_COUNT];
    Item          belt[BELT_MAX]; // slot 0..3 the bottom row (keys 1-4), then the rows above
    int           gold;
    InventoryItem stash[STASH_MAX]; // in town (Stash_Pages pages)
    int           stash_count;
    InventoryItem cube[CUBE_MAX];             // in the Horadric Cube (any cube you carry opens them)
    Item          hireling_items[HIRE_SLOTS]; // what your hireling wears
    int           cube_count;
    int           stash_gold;
    // A hireling (hireling.txt): row + 1 (0 = none), their level, name (string table number), life; dead: to be
    // brought back by whoever sells them, for gold
    int      hireling;
    int      hireling_level;
    int      hireling_name;
    int      hireling_life;
    bool     hireling_dead;
    uint32_t hireling_experience;
    // Quests: each one's QuestState (Quests.h) for Normal, Nightmare and Hell, and what rewards added for good
    uint8_t quests[3][CHARACTER_QUESTS];
    int     quest_resist;
    // Your corpse (softcore death): what you wore and had in the belt stays on it until you get back to it (not saved
    // as such: a save puts it back on you, the game's way of not losing it)
    bool     has_corpse;
    Item     corpse_equipped[EQUIP_COUNT];
    Item     corpse_belt[BELT_MAX];
    int      quest_life;
    uint64_t waypoints; // the waypoints found (bit = World_WaypointIndex), the Rogue Encampment's from the start
    uint8_t  films;     // the acts' films seen (bit: CharacterFilm), so each plays once
    int      bonus[BONUS_COUNT]; // from skills right now (not saved)
    char     realm[40];          // made in a multiplayer game that keeps its own characters: that host's id ("" = here)
} Character;

// How a character looks (what they wear and hold): the weapon class their animations use and each layer's picture
// (data/global/chars/<class>/<layer>/...): "lit", "med", "hvy" for the body, the helm's, weapon's or shield's code.
// Empty: that layer isn't drawn.
#define CHARACTER_LOOK_LAYERS 16 // COF_COMPONENT_COUNT
#define CHARACTER_LOOKS_TEXT  80
typedef struct CharacterLooks {
    char     weapon_class[4];
    char     layer[CHARACTER_LOOK_LAYERS][4];
    char     shape; // the Druid's: 'w' a wolf, 'b' a bear, 0 none (the game sets it from the buff; sent with the rest)
    uint16_t tint[CHARACTER_LOOK_LAYERS]; // what's worn's colours (Affixes_WorldTint), per layer; 0 none
} CharacterLooks;

void Character_Looks(const Character *character, CharacterLooks *looks);
void Character_LooksDefault(CharacterLooks *looks);                            // light armor, bare hands
void Character_LooksToText(const CharacterLooks *looks, char *text, int size); // for the network
bool Character_LooksFromText(const char *text, CharacterLooks *looks);

#define CHARACTER_TEXT_MAX 49152 // a character as text (Character_WriteText): the save file's contents

const char *Character_ClassName(CharacterClass class_id);  // "Amazon"
const char *Character_ClassToken(CharacterClass class_id); // "am" (graphics)
bool        Character_ClassIsExpansion(CharacterClass class_id);
const char *Character_StatName(CharacterStat stat);

// A new level 1 character with the class's starting stats
void Character_Create(Character *character, const char *name, CharacterClass class_id, bool expansion, bool hardcore);

// Derived numbers
// A stat with what's worn added (requirements, damage, life and mana use this one)
int Character_Stat(const Character *character, CharacterStat stat);
int Character_ItemDefense(const Item *item); // an armor's defense with its enhanced defense
// A resistance (fire, cold, lightning, poison) from what's worn, less the difficulty's penalty (the game's: 0, 40,
// 100), at most RESIST_MAX
int Character_Resist(const Character *character, Element element, int penalty);
// Fire, cold, lightning and poison damage added to each blow (what's worn)
void Character_ElementDamage(const Character *character, Element element, int *minimum, int *maximum);
int  Character_MaxLife(const Character *character);
int  Character_MaxMana(const Character *character);
int  Character_MaxStamina(const Character *character);
int  Character_Defense(const Character *character);
// Blocking with a shield, the game's way: (its block + the class's + magic and skills) x (dexterity - 15) / (level x
// 2), at most 75 (percent); 0 without a shield
int         Character_BlockChance(const Character *character);
int         Character_AttackRating(const Character *character);
void        Character_Damage(const Character *character, int *minimum, int *maximum); // with the weapon held
const char *Character_WeaponName(const Character *character);
int         Character_ManaRegenSeconds(const Character *character); // time to refill mana from empty
int         Character_RunDrain(const Character *character);         // stamina used running (charstats RunDrain)
uint32_t    Character_ExperienceFor(int level);                     // experience a character needs to be this level
// Adds experience; levels up as many times as it covers (stat and skill points, life/mana/stamina refilled).
// Returns the number of levels gained.
int  Character_AddExperience(Character *character, uint32_t experience);
bool Character_SpendStatPoint(Character *character, CharacterStat stat);

// The game's hit chance (5..95%): attacker's attack rating and level against the defender's defense and level
int Character_HitChance(int attack_rating, int attacker_level, int defense, int defender_level);

// Names: 2..15 letters, at most one '-' or '_' and not at either end. On failure, why (for the player).
bool Character_IsValidName(const char *name, const char **reason);

// Saved characters
bool Character_Save(Character *character); // also stamps last_played
// The save file's contents: written into text (cut if it doesn't fit; returns the full length), read back
size_t Character_WriteText(const Character *character, char *text, size_t size);
bool   Character_ReadText(const char *text, Character *character);
// Saving goes here instead of the save folder (a game on a dedicated server keeps the character there); NULL: back
void Character_SetSaveHook(bool (*hook)(Character *character));
bool Character_Exists(const char *name);
bool Character_Delete(const char *name);
// All saved characters, most recently played first. Free the array with free().
int Character_ListSaved(Character **characters);

// The character being played (copied in when the game starts)
Character *Character_Current(void);
void       Character_SetCurrent(const Character *character);
// Single Player: the difficulty picked for the game (0 Normal, 1 Nightmare, 2 Hell), and how many are open to a
// character (Nightmare once Normal is finished: Diablo dead, or Baal for expansion characters; Hell after Nightmare)
void Character_SetDifficulty(int difficulty);
int  Character_Difficulty(void);
int  Character_DifficultiesOpen(const Character *character);

void Character_Release(void); // frees the cached tables

#endif // ABYSS_CHARACTER_H
