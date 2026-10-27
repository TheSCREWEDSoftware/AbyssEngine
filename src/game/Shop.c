#include "Shop.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include "Affixes.h"
#include "Inventory.h"
#include "Items.h"
#include "Sockets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const struct {
    const char *npc;    // monstats id (lower case, as the towns have them)
    const char *column; // the item tables' column name
} vendors[] = {
    {"charsi",   "Charsi"  },
    {"gheed",    "Gheed"   },
    {"akara",    "Akara"   },
    {"fara",     "Fara"    },
    {"lysander", "Lysander"},
    {"drognan",  "Drognan" },
    {"elzix",    "Elzix"   },
    {"hratli",   "Hralti"  }, // (the game's spelling)
    {"alkor",    "Alkor"   },
    {"ormus",    "Ormus"   },
    {"asheara",  "Asheara" },
    {"halbu",    "Halbu"   },
    {"jamella",  "Jamella" },
    {"larzuk",   "Larzuk"  },
    {"malah",    "Malah"   },
    {"drehya",   "Drehya"  },
};

const char *Shop_Vendor(const char *npc_id) {
    for (size_t v = 0; v < sizeof(vendors) / sizeof(vendors[0]); v++) {
        if (strcasecmp(vendors[v].npc, npc_id) == 0) {
            return vendors[v].column;
        }
    }
    return NULL;
}

const char *Shop_TabName(const int tab) {
    static const char *names[SHOP_TABS] = {"Armor", "Weapons", "Misc"};
    return tab >= 0 && tab < SHOP_TABS ? names[tab] : "";
}

// npc.txt: what they charge (sell mult) and pay (buy mult), out of 1024, and the most they pay
typedef struct Merchant {
    int sell_mult;
    int buy_mult;
    int max_buy[3];
    int repair_mult;
} Merchant;

static Merchant Shop__Merchant(const char *npc) {
    Merchant m = {
        1024, 512, {5000, 30000, 35000},
          128
    };
    DataTable *data = DataTable_Load("/data/global/excel/npc.txt");
    if (data == NULL) {
        return m;
    }
    for (int r = 0; r < DataTable_GetRowCount(data); r++) {
        if (strcasecmp(DataTable_GetString(data, r, DataTable_GetColumnIndex(data, "npc")), npc) == 0) {
            m.sell_mult   = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "sell mult"));
            m.buy_mult    = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "buy mult"));
            m.max_buy[0]  = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "max buy"));
            m.max_buy[1]  = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "max buy (N)"));
            m.max_buy[2]  = DataTable_GetInt(data, r, DataTable_GetColumnIndex(data, "max buy (H)"));
            const int rc  = DataTable_GetColumnIndex(data, "rep mult");
            m.repair_mult = rc >= 0 && DataTable_GetInt(data, r, rc) > 0 ? DataTable_GetInt(data, r, rc) : 128;
            break;
        }
    }
    DataTable_Destroy(&data);
    return m;
}

static int Shop__Tab(const ItemBase *base) {
    return base->kind == ITEM_KIND_ARMOR ? 0 : (base->kind == ITEM_KIND_WEAPON ? 1 : 2);
}

// Onto its tab, column by column like the game's store fills
static bool Shop__Place(ShopStock *stock, const Item *item, const bool endless) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || stock->count >= SHOP_ITEMS) {
        return false;
    }
    const int tab = Shop__Tab(base);
    for (int x = 0; x + base->width <= SHOP_W; x++) {
        for (int y = 0; y + base->height <= SHOP_H; y++) {
            bool free = true;
            for (int cy = y; cy < y + base->height && free; cy++) {
                for (int cx = x; cx < x + base->width && free; cx++) {
                    free = Shop_ItemAt(stock, tab, cx, cy) < 0;
                }
            }
            if (free) {
                InventoryItem *slot            = &stock->items[stock->count];
                slot->item                     = *item;
                slot->x                        = x;
                slot->y                        = y;
                slot->page                     = tab;
                stock->endless[stock->count++] = endless;
                return true;
            }
        }
    }
    return false;
}

static int Shop__Roll(const int low, const int high) { return high > low ? low + (rand() % (high - low + 1)) : low; }

static void Shop__FillFrom(ShopStock *stock, const char *path, const char *vendor, const int level) {
    DataTable *data = DataTable_Load(path);
    if (data == NULL) {
        return;
    }
    char column[40];
#define VCOL(suffix) (snprintf(column, sizeof(column), "%s%s", vendor, suffix), DataTable_GetColumnIndex(data, column))
    const int c_min = VCOL("Min"), c_max = VCOL("Max"), c_mmin = VCOL("MagicMin"), c_mmax = VCOL("MagicMax"),
              c_mlvl = VCOL("MagicLvl");
#undef VCOL
    const int c_code = DataTable_GetColumnIndex(data, "code"), c_perm = DataTable_GetColumnIndex(data, "PermStoreItem"),
              c_level = DataTable_GetColumnIndex(data, "level");
    for (int r = 0; r < DataTable_GetRowCount(data); r++) {
        const ItemBase *base = Items_Find(DataTable_GetString(data, r, c_code));
        if (base == NULL || c_max < 0) {
            continue;
        }
        const int  most    = DataTable_GetInt(data, r, c_max);
        const int  magic   = DataTable_GetInt(data, r, c_mmax);
        const bool endless = c_perm >= 0 && DataTable_GetInt(data, r, c_perm) == 1;
        if ((most <= 0 && magic <= 0) || (!endless && DataTable_GetInt(data, r, c_level) > level + 4)) {
            continue; // not theirs, or not yet (the game keeps better gear for later)
        }
        if (endless) {
            Item item = Inventory_NewItem(base->code);
            Shop__Place(stock, &item, true);
            continue;
        }
        // (half the table's numbers: the store holds a little of everything, like the game's looks)
        const int plain = (Shop__Roll(DataTable_GetInt(data, r, c_min), most) + 1) / 2;
        for (int i = 0; i < plain; i++) {
            Item item = Inventory_NewItem(base->code);
            Shop__Place(stock, &item, false);
        }
        const int magics = (Shop__Roll(DataTable_GetInt(data, r, c_mmin), magic) + 1) / 2;
        for (int i = 0; i < magics; i++) {
            Item item = Inventory_NewItem(base->code);
            for (int tries = 0; tries < 50 && item.quality != ITEM_MAGIC; tries++) {
                item = Inventory_NewItem(base->code);
                Affixes_Roll(&item, level + DataTable_GetInt(data, r, c_mlvl));
            }
            if (item.quality == ITEM_MAGIC) {
                Shop__Place(stock, &item, false);
            }
        }
    }
    DataTable_Destroy(&data);
}

void Shop_Fill(ShopStock *stock, const char *npc_id, const int character_level, const int difficulty) {
    memset(stock, 0, sizeof(ShopStock));
    const char *vendor = Shop_Vendor(npc_id);
    if (vendor == NULL) {
        return;
    }
    snprintf(stock->vendor, sizeof(stock->vendor), "%s", vendor);
    snprintf(stock->npc, sizeof(stock->npc), "%s", npc_id);
    const Merchant m   = Shop__Merchant(npc_id);
    stock->sell_mult   = m.sell_mult;
    stock->buy_mult    = m.buy_mult;
    stock->repair_mult = m.repair_mult;
    memcpy(stock->max_buy, m.max_buy, sizeof(stock->max_buy));
    const int level = character_level + (difficulty * 30); // Nightmare and Hell stores sell better things
    Shop__FillFrom(stock, "/data/global/excel/misc.txt", vendor, level);
    Shop__FillFrom(stock, "/data/global/excel/armor.txt", vendor, level);
    Shop__FillFrom(stock, "/data/global/excel/weapons.txt", vendor, level);
}

int Shop_ItemAt(const ShopStock *stock, const int tab, const int cell_x, const int cell_y) {
    for (int i = 0; i < stock->count; i++) {
        const InventoryItem *it = &stock->items[i];
        if (it->page != tab) {
            continue;
        }
        const ItemBase *base = Items_Find(it->item.code);
        const int       w    = base != NULL ? base->width : 1;
        const int       h    = base != NULL ? base->height : 1;
        if (cell_x >= it->x && cell_x < it->x + w && cell_y >= it->y && cell_y < it->y + h) {
            return i;
        }
    }
    return -1;
}

void Shop_Take(ShopStock *stock, const Item *item) { Shop__Place(stock, item, false); }

void Shop_Remove(ShopStock *stock, const int index) {
    if (index < 0 || index >= stock->count || stock->endless[index]) {
        return;
    }
    for (int i = index; i + 1 < stock->count; i++) {
        stock->items[i]   = stock->items[i + 1];
        stock->endless[i] = stock->endless[i + 1];
    }
    stock->count--;
}

// What an item is worth: its table cost, more for magic and the rest
static int Shop__Value(const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return 1;
    }
    long             value                            = base->cost > 0 ? base->cost : 1;
    static const int quality_mult[ITEM_QUALITY_COUNT] = {1, 3, 5, 8, 8, 6};
    value *= quality_mult[item->quality < ITEM_QUALITY_COUNT ? item->quality : 0];
    value += (long)item->mod_count * 50;
    value += (long)item->sockets * 100;
    return value > 1000000 ? 1000000 : (int)value;
}

int Shop_Price(const ShopStock *stock, const Item *item) {
    if (stock->gamble) {
        const ItemBase *base = Items_Find(item->code);
        const long      cost = base != NULL && base->gamble_cost > 0 ? base->gamble_cost : 10000;
        return (int)(cost * (stock->sell_mult > 0 ? stock->sell_mult : 1024) / 1024);
    }
    const long p = (long)Shop__Value(item) * (stock->sell_mult > 0 ? stock->sell_mult : 1024) / 1024;
    return p > 0 ? (int)p : 1;
}

int Shop_SellPrice(const ShopStock *stock, const Item *item, const int difficulty) {
    long      p    = (long)Shop__Value(item) * (stock->buy_mult > 0 ? stock->buy_mult : 512) / 1024 / 4;
    const int most = stock->max_buy[difficulty >= 0 && difficulty <= 2 ? difficulty : 0];
    p              = most > 0 && p > most ? most : p;
    return p > 0 ? (int)p : 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Repairs

static const char *const smiths[]   = {"charsi", "fara", "hratli", "halbu", "larzuk"};
static const char *const gamblers[] = {"gheed", "elzix", "alkor", "jamella", "nihlathak", "drehya"};

static bool Shop__In(const char *const *list, const size_t count, const char *npc_id) {
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(list[i], npc_id) == 0) {
            return true;
        }
    }
    return false;
}

bool Shop_Repairs(const char *npc_id) { return Shop__In(smiths, sizeof(smiths) / sizeof(smiths[0]), npc_id); }

// Each item's share: its worth times the part worn away, times their repair multiplier
static long Shop__RepairOne(const Item *item, const int repair_mult) {
    long recharge = 0; // charged skills are filled up too: so much a charge, more for higher levels
    for (int m = 0; item->code[0] != '\0' && m < item->mod_count && m < ITEM_MODS_MAX; m++) {
        const ItemMod *mod = &item->mods[m];
        if (mod->type == MOD_CHARGED && ItemMod_Charges(mod) < mod->extra) {
            recharge += (long)(mod->extra - ItemMod_Charges(mod)) * (10 + (ItemMod_ChargeLevel(mod) * 15));
        }
    }
    if (item->code[0] == '\0' || item->max_durability <= 0 || item->durability >= item->max_durability) {
        return recharge;
    }
    const long value   = Shop__Value(item);
    const long missing = item->max_durability - item->durability;
    const long cost    = value * missing / item->max_durability * repair_mult / 1024;
    return (cost > 0 ? cost : 1) + recharge;
}

int Shop_RepairCost(const char *npc_id, const Character *character) {
    const int repair_mult = Shop__Merchant(npc_id).repair_mult;
    long      total       = 0;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        total += Shop__RepairOne(&character->equipped[s], repair_mult);
    }
    for (int i = 0; i < character->inventory_count; i++) {
        total += Shop__RepairOne(&character->inventory[i].item, repair_mult);
    }
    return total > 100000000 ? 100000000 : (int)total;
}

void Shop_RepairAll(Character *character) {
    for (int s = 0; s < EQUIP_COUNT; s++) {
        character->equipped[s].durability = character->equipped[s].max_durability;
        Affixes_Recharge(&character->equipped[s]);
    }
    for (int i = 0; i < character->inventory_count; i++) {
        character->inventory[i].item.durability = character->inventory[i].item.max_durability;
        Affixes_Recharge(&character->inventory[i].item);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Gambling

bool Shop_Gambles(const char *npc_id) { return Shop__In(gamblers, sizeof(gamblers) / sizeof(gamblers[0]), npc_id); }

void Shop_FillGamble(ShopStock *stock, const char *npc_id, const int character_level, const int difficulty) {
    Shop_Fill(stock, npc_id, character_level, difficulty); // (their prices and name)
    stock->count  = 0;
    stock->gamble = true;
    snprintf(stock->vendor, sizeof(stock->vendor), "%s", npc_id);
    stock->vendor[0] = (char)toupper((unsigned char)stock->vendor[0]);
    DataTable *data  = DataTable_Load("/data/global/excel/gamble.txt");
    if (data == NULL) {
        return;
    }
    // The ones within reach of your level, a random few of each (rings and amulets always)
    const int c_code = DataTable_GetColumnIndex(data, "code");
    for (int r = 0; r < DataTable_GetRowCount(data); r++) {
        const ItemBase *base = Items_Find(DataTable_GetString(data, r, c_code));
        if (base == NULL || base->level > character_level + 5) {
            continue;
        }
        const bool jewelry = Items_IsA(base, "ring") || Items_IsA(base, "amul");
        const int  copies  = jewelry ? 2 : (rand() % 3 == 0 ? 1 : 0);
        for (int c = 0; c < copies; c++) {
            // At higher levels the exceptional and elite versions come up too
            const ItemBase *offer = base;
            const ItemBase *exc   = base->tiers[1][0] != '\0' ? Items_Find(base->tiers[1]) : NULL;
            const ItemBase *elite = base->tiers[2][0] != '\0' ? Items_Find(base->tiers[2]) : NULL;
            if (elite != NULL && elite->level <= character_level && rand() % 6 == 0) {
                offer = elite;
            } else if (exc != NULL && exc->level <= character_level && rand() % 3 == 0) {
                offer = exc;
            }
            Item item = Inventory_NewItem(offer->code);
            Shop__Place(stock, &item, false);
        }
    }
    DataTable_Destroy(&data);
}

void Shop_Gamble(Item *item, const int character_level) {
    const int rolled = character_level - 5 + (rand() % 10);
    const int level  = rolled > 0 ? rolled : 1;
    const int roll   = rand() % 10000;
    // Out of 10,000: unique 10, set 20, rare 1,000, the rest magic (a unique or set that isn't there: rare)
    if (roll < 10 && Affixes_RollQuality(item, level, ITEM_UNIQUE)) {
        return;
    }
    if (roll < 30 && Affixes_RollQuality(item, level, ITEM_SET)) {
        return;
    }
    if (roll < 1030 && Affixes_RollQuality(item, level, ITEM_RARE)) {
        return;
    }
    Affixes_RollQuality(item, level, ITEM_MAGIC);
}

// ---------------------------------------------------------------------------------------------------------------------
// Identifying

bool Shop_Identifies(const char *npc_id) { return strncasecmp(npc_id, "cain", 4) == 0; }

int Shop_IdentifyAll(Character *character) {
    int count = 0;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        count                               += character->equipped[s].unidentified ? 1 : 0;
        character->equipped[s].unidentified  = false;
    }
    for (int i = 0; i < character->inventory_count; i++) {
        count                                     += character->inventory[i].item.unidentified ? 1 : 0;
        character->inventory[i].item.unidentified  = false;
    }
    for (int i = 0; i < character->stash_count; i++) {
        count                                 += character->stash[i].item.unidentified ? 1 : 0;
        character->stash[i].item.unidentified  = false;
    }
    return count;
}
