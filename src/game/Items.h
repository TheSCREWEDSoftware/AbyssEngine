#ifndef ABYSS_ITEMS_H
#define ABYSS_ITEMS_H

#include <stdbool.h>

// The game's base items (weapons.txt, armor.txt, misc.txt, with itemtypes.txt for where they go): what an item is,
// its size in the inventory, its picture, damage or defense, requirements, and what a potion does. Only normal items
// for now (no magic properties, sockets or durability).

#define ITEM_CODE_MAX 8

typedef enum ItemKind { ITEM_KIND_WEAPON, ITEM_KIND_ARMOR, ITEM_KIND_MISC } ItemKind;

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
    char     inv_file[32];  // data/global/items/<inv_file>.dc6
    char     use_sound[32]; // sounds.txt: put down in the inventory, worn, drunk
    char flippy_file[32];   // data/global/items/<flippy_file>.dc6: on the ground (it flips through the air, then lies)
    char drop_sound[32];    // sounds.txt: landing on the ground
    int  drop_sound_frame;  // ... played at this frame of the flip
    int  level;             // its item level: which drops it can be in (weap3 has the weapons up to level 3)
    bool spawnable;         // can drop
    char kinds[8][8];       // its type and what that counts as (itemtypes.txt Equiv1/2): "axe", "mele", "weap"
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
    int  belt_rows; // a belt: rows of four potions it holds
    bool beltable;  // potions and scrolls go in the belt
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
