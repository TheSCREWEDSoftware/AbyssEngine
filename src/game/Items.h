#ifndef ABYSS_ITEMS_H
#define ABYSS_ITEMS_H

#include <stdbool.h>
#include <stdint.h>

// The game's base items (weapons.txt, armor.txt, misc.txt, with itemtypes.txt for where they go): what an item is,
// its size in the inventory, its picture, damage or defense, durability, requirements, and what a potion does. Magic is
// in Affixes.h, sockets in Sockets.h.

#define ITEM_CODE_MAX 8

typedef enum ItemKind { ITEM_KIND_WEAPON, ITEM_KIND_ARMOR, ITEM_KIND_MISC } ItemKind;

// An item's quality: normal (white), magic (blue: a prefix and/or a suffix), rare (yellow: a made-up name and up to
// six affixes), unique (gold: one of the game's named items)
// and a runeword (the right runes in the right order in a plain item's sockets: Runes.txt; name1 is its row)
typedef enum ItemQuality {
    ITEM_NORMAL,
    ITEM_MAGIC,
    ITEM_RARE,
    ITEM_UNIQUE,
    ITEM_RUNEWORD,
    ITEM_SET, // green: one of a set (SetItems.txt; name1 is its row), more magic for each piece of it worn
    ITEM_QUALITY_COUNT
} ItemQuality;

// What magic does (the game's properties we know; Affixes.c turns Properties.txt codes into these). The ones marked *
// are shown but don't do anything yet.
typedef enum ItemModType {
    MOD_STRENGTH,
    MOD_DEXTERITY,
    MOD_VITALITY,
    MOD_ENERGY,
    MOD_LIFE,
    MOD_MANA,
    MOD_STAMINA,
    MOD_ATTACK_RATING,
    MOD_ATTACK_RATING_PCT,
    MOD_DEFENSE,
    MOD_DEFENSE_PCT, // enhanced defense: of the item it's on
    MOD_DAMAGE_PCT,  // enhanced damage: on a weapon, of its damage; elsewhere, added to strength's bonus
    MOD_DAMAGE_MIN,
    MOD_DAMAGE_MAX,
    MOD_FIRE_MIN, // elemental damage: added to each blow, against the monster's resistance
    MOD_FIRE_MAX,
    MOD_COLD_MIN,
    MOD_COLD_MAX,
    MOD_LIGHTNING_MIN,
    MOD_LIGHTNING_MAX,
    MOD_POISON,               // poison damage, added to each blow (all at once for now)
    MOD_RES_FIRE,             // against monsters' attacks
    MOD_RES_COLD,             // against monsters' attacks
    MOD_RES_LIGHTNING,        // against monsters' attacks
    MOD_RES_POISON,           // against monsters' attacks
    MOD_RES_ALL,              // against monsters' attacks
    MOD_LIFE_STEAL,           // life back from each blow's damage
    MOD_MANA_STEAL,           // mana back from each blow's damage
    MOD_MAGIC_FIND,           // better drops from what you kill
    MOD_GOLD_FIND,            // more gold picked up
    MOD_REPLENISH_LIFE,       // life back each second
    MOD_FASTER_RUN,           // faster walking and running
    MOD_ATTACK_SPEED,         // quicker swings
    MOD_HIT_RECOVERY,         // over a blow sooner
    MOD_BLOCK,                // more chance to block (with a shield)
    MOD_LIGHT_RADIUS,         // *
    MOD_THORNS,               // attacker takes damage (melee)
    MOD_DAMAGE_REDUCED,       // each blow taken does this much less
    MOD_MAGIC_DAMAGE_REDUCED, // *
    MOD_TYPE_COUNT
} ItemModType;

#define ITEM_MODS_MAX    16
#define ITEM_SOCKETS_MAX 6

typedef struct ItemMod {
    uint8_t type; // ItemModType
    int16_t value;
} ItemMod;

// Where an item can be worn (bodylocs.txt)
typedef enum EquipSlot {
    EQUIP_HEAD,
    EQUIP_NECK,
    EQUIP_TORSO,
    EQUIP_RIGHT_ARM, // the weapon hand (the left of the panel)
    EQUIP_LEFT_ARM,  // the shield hand
    EQUIP_RIGHT_RING,
    EQUIP_LEFT_RING,
    EQUIP_BELT,
    EQUIP_FEET,
    EQUIP_GLOVES,
    EQUIP_COUNT
} EquipSlot;

typedef struct ItemBase {
    char     code[ITEM_CODE_MAX];
    char     name[64];
    char     type[8]; // itemtypes.txt code
    ItemKind kind;
    int      width; // inventory cells
    int      height;
    char     inv_file[32];        // data/global/items/<inv_file>.dc6
    char     unique_inv_file[32]; // its picture when unique (empty: the normal one)
    char     use_sound[32];       // sounds.txt: put down in the inventory, worn, drunk
    char flippy_file[32];  // data/global/items/<flippy_file>.dc6: on the ground (it flips through the air, then lies)
    char drop_sound[32];   // sounds.txt: landing on the ground
    int  drop_sound_frame; // ... played at this frame of the flip
    int  level;            // its item level: which drops it can be in (weap3 has the weapons up to level 3)
    bool spawnable;        // can drop
    char kinds[8][8];      // its type and what that counts as (itemtypes.txt Equiv1/2): "axe", "mele", "weap"
    int  kind_count;
    bool slots[EQUIP_COUNT];
    int  min_damage; // one hand (or the only damage)
    int  max_damage;
    int  two_hand_min;
    int  two_hand_max;
    bool two_handed;
    int  str_bonus; // % damage per 100 strength
    int  dex_bonus;
    int  min_defense;
    int  max_defense;
    int  block;
    int  required_strength;
    int  required_dexterity;
    int  required_level;
    // How it looks on the character (the layers of data/global/chars): the weapon's or helm's picture code
    // (alternategfx), the weapon class it's held with, and armor's light/medium/heavy for each body part
    char gfx[ITEM_CODE_MAX];
    char wclass[4];      // "1hs", "stf", "bow", ...
    char wclass_2h[4];   // held in both hands (a Barbarian's sword with nothing in the other hand)
    bool one_or_two;     // can be held in one hand or both (1or2handed)
    int  look_right_arm; // armor: 0 light, 1 medium, 2 heavy; -1 doesn't change it
    int  look_left_arm;
    int  look_torso;
    int  look_legs;
    int  look_right_pad;
    int  look_left_pad;
    int  max_sockets; // gemsockets
    int  cost;        // what a merchant asks for it (before npc.txt's multiplier)
    int  gamble_cost; // what a gambler asks for one (rings and amulets: misc.txt; the rest: their own table)
    int  durability;  // weapons and armor: how many knocks it takes (0: it doesn't wear down)
    int  belt_rows;   // a belt: rows of four potions it holds
    bool beltable;    // potions and scrolls go in the belt
    bool stackable;
    int  max_stack;
    // Potions
    int life;           // healing: this much life ...
    int mana;           // ... or mana
    int seconds;        // ... over this long (0: at once)
    int rejuvenate_pct; // rejuvenation: this percentage of life and mana at once
} ItemBase;

const ItemBase *Items_Find(const char *code); // NULL if there's no such item
int             Items_Count(void);
const ItemBase *Items_Get(int index);
bool            Items_IsA(const ItemBase *base, const char *type); // its type, or one it counts as ("mele", "armo")
const char     *Items_SlotName(EquipSlot slot);
bool            Items_IsPotion(const ItemBase *base);
void            Items_Release(void);

#endif // ABYSS_ITEMS_H
