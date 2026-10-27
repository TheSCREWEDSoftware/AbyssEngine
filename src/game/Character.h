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
#define BELT_MAX            16 // four rows of four, with the biggest belt

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
} Item;

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
    uint64_t      waypoints; // the waypoints found (bit = World_WaypointIndex), the Rogue Encampment's from the start
    char          realm[40]; // made in a multiplayer game that keeps its own characters: that host's id ("" = here)
} Character;

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
void        Character_ElementDamage(const Character *character, Element element, int *minimum, int *maximum);
int         Character_MaxLife(const Character *character);
int         Character_MaxMana(const Character *character);
int         Character_MaxStamina(const Character *character);
int         Character_Defense(const Character *character);
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

void Character_Release(void); // frees the cached tables

#endif // ABYSS_CHARACTER_H
