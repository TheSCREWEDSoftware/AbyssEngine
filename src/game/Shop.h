#ifndef ABYSS_SHOP_H
#define ABYSS_SHOP_H

#include "Character.h"
#include <stdbool.h>

// Town merchants, the game's way: what each sells comes from weapons.txt, armor.txt and misc.txt (their "<Name>Min",
// "<Name>Max" columns, and "<Name>MagicMin/Max/Lvl" for magic ones), potions and scrolls always in stock
// (PermStoreItem), and what they pay and charge from npc.txt (buy mult, sell mult, max buy). The stock is rolled when
// you come to trade, laid out on tabs of 10 x 10 like the game's store.

#define SHOP_W     10
#define SHOP_H     10
#define SHOP_TABS  3 // armor, weapons, misc
#define SHOP_ITEMS (SHOP_W * SHOP_H * SHOP_TABS)

typedef struct ShopStock {
    char          vendor[16];          // "Akara"
    char          npc[16];             // npc.txt's name: "akara"
    InventoryItem items[SHOP_ITEMS];   // page: the tab
    bool          endless[SHOP_ITEMS]; // potions and scrolls: buying one leaves it there
    int           count;
    int           sell_mult; // npc.txt, out of 1024
    int           buy_mult;
    int           max_buy[3];
} ShopStock;

const char *Shop_Vendor(const char *npc_id); // "Akara" for "akara"; NULL: they don't trade
void        Shop_Fill(ShopStock *stock, const char *npc_id, int character_level, int difficulty);
int         Shop_ItemAt(const ShopStock *stock, int tab, int cell_x, int cell_y);
void        Shop_Remove(ShopStock *stock, int index);
void        Shop_Take(ShopStock *stock, const Item *item);        // sold to them: onto its tab (if there's room)
int         Shop_Price(const ShopStock *stock, const Item *item); // what it costs you
int         Shop_SellPrice(const ShopStock *stock, const Item *item, int difficulty); // what they give you
const char *Shop_TabName(int tab);

#endif // ABYSS_SHOP_H
