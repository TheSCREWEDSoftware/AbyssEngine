#include "GameInventory.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Affixes.h"
#include "../game/Cube.h"
#include "../game/Hirelings.h"
#include "../game/Inventory.h"
#include "../game/Shop.h"
#include "../game/Sockets.h"
#include "../managers/InputManager.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVENTORY_PANEL "/data/global/ui/PANEL/invchar6.dc6" // frames 4-7: the inventory (0-3: the character)
#define CELL            29                                   // a grid cell, in pixels
#define ICON_CACHE      192
#define TOOLTIP_LINES   26

// inventory.txt's 640 x 480 layout ("Amazon" .. "Assassin", all the same), moved to the panel's place on 800 x 600
#define LAYOUT_DX (GAME_PANEL_X - 320)
#define GRID_X    (339 + LAYOUT_DX)
#define GRID_Y    255

static const SDL_Rect slot_rects[EQUIP_COUNT] = {
    [EQUIP_HEAD] = {455 + LAYOUT_DX, 8,   54, 51 },
      [EQUIP_NECK] = {529 + LAYOUT_DX, 35,  23, 24 },
    [EQUIP_TORSO] = {453 + LAYOUT_DX, 77,  56, 82 },
      [EQUIP_RIGHT_ARM] = {340 + LAYOUT_DX, 47,  55, 112},
    [EQUIP_LEFT_ARM] = {571 + LAYOUT_DX, 47,  55, 112},
      [EQUIP_RIGHT_RING] = {415 + LAYOUT_DX, 180, 23, 24 },
    [EQUIP_LEFT_RING] = {529 + LAYOUT_DX, 180, 23, 24 },
      [EQUIP_BELT] = {456 + LAYOUT_DX, 179, 52, 25 },
    [EQUIP_FEET] = {572 + LAYOUT_DX, 182, 54, 52 },
      [EQUIP_GLOVES] = {341 + LAYOUT_DX, 181, 54, 53 },
};

// The belt's slots on the control panel (the bottom row; keys 1-4)
static int belt_x = 422; // the belt's bottom row on the control panel (GameInventory_SetBeltPosition)
static int belt_y = 568;
#define BELT_X    belt_x
#define BELT_Y    belt_y
#define BELT_STEP 31

typedef enum Origin {
    ORIGIN_GRID,
    ORIGIN_EQUIP,
    ORIGIN_BELT,
    ORIGIN_GROUND,
    ORIGIN_STASH,
    ORIGIN_TRADE,
    ORIGIN_CUBE
} Origin;

// The hireling's screen (NpcInv.dc6): the four slots, their numbers below, the close button
#define HIRE_PANEL   "/data/global/ui/PANEL/NpcInv.dc6"
#define HIRE_CLOSE_X 272
#define HIRE_CLOSE_Y 383
static const SDL_Rect hire_rects[HIRE_SLOTS] = {
    {133, 4,  56, 56 }, // helm
    {133, 73, 56, 87 }, // armor
    {17,  46, 58, 113}, // weapon
    {249, 46, 58, 113}, // shield
};

// The Horadric Cube, on the left (supertransmogrifier.dc6, inventory.txt's "Transmogrify Box Page 1"): its 3 x 4
// inside, the Transmute button below it and the close button
#define CUBE_PANEL       "/data/global/ui/PANEL/supertransmogrifier.dc6"
#define CUBE_GRID_X      118
#define CUBE_GRID_Y      139
#define CUBE_TRANSMUTE_X 144
#define CUBE_TRANSMUTE_Y 258
#define CUBE_CLOSE_X     273
#define CUBE_CLOSE_Y     383

// The trade panel (trade.dc6): their grid above, yours below (inventory.txt's "Trade Page 1" and "2"), the gold boxes
// and the accept / close buttons
#define TRADE_PANEL      "/data/global/ui/PANEL/trade.dc6"
#define TRADE_GRID_X     20
#define TRADE_THEIR_Y    41
#define TRADE_MY_Y       255
#define TRADE_ACCEPT_X   222
#define TRADE_CLOSE_X    274
#define TRADE_BUTTONS_Y  383
#define TRADE_THEIR_OK_X 274
#define TRADE_THEIR_OK_Y 170

// The stash, on the left (TradeStash.dc6, inventory.txt's "Big Bank Page 1"), and its gold with the withdraw button
#define STASH_PANEL  "/data/global/ui/PANEL/TradeStash.dc6"
#define BANK_PANEL   "/data/global/ui/PANEL/bank.DC6" // classic characters' stash: "Bank Page 1", 6 x 4
#define STASH_GRID_X 74
// The expansion's big stash (6 x 8, its grid at 82) or the classic bank (6 x 4, at 273, the gold boxes above it): set
// for the character shown (GameInventory__StashLayout)
static int stash_grid_y = 82;
static int stash_gold_y = 23;
static int stash_rows   = STASH_H;
// A merchant's store, on the left (buysell.dc6: tabs along the top, a 10 x 10 grid, your gold, the buttons)
#define SHOP_PANEL          "/data/global/ui/PANEL/buysell.dc6"
#define SHOP_GRID_X         15
#define SHOP_GRID_Y         62
#define SHOP_TAB_W          79
#define SHOP_TAB_H          31
#define SHOP_CLOSE_X        270
#define SHOP_CLOSE_Y        383
#define SHOP_BUTTON_SZ      32
#define STASH_GOLD_BUTTON_X 75

typedef struct IconCache {
    char    file[32];
    Sprite *sprite;
} IconCache;

// The gold button, left of the gold in its box (goldcoinbtn.dc6: up, pressed)
#define GOLD_BUTTON_X (GAME_PANEL_X + 84)
#define GOLD_BUTTON_Y 392
#define GOLD_BUTTON_W 20
#define GOLD_BUTTON_H 18

struct GameInventory {
    Sprite             *panel;
    Sprite             *gold_button;
    Sprite             *stash_panel;
    Sprite             *bank_panel; // (classic characters')
    Sprite             *socket;     // gemsocket.dc6
    Sprite             *shop_panel;
    Sprite             *shop_tabs;
    Sprite             *shop_buttons; // buysellbtn.dc6
    ShopStock          *shop;         // trading with a merchant (NULL: not)
    TradeView          *trade;        // trading with another player (NULL: not)
    Sprite             *trade_panel;
    int                 shop_tab;
    int                 difficulty;    // what they pay depends on it
    char                tip_extra[48]; // the price line under a tooltip while trading
    bool                stash_open;    // in town, at the stash
    bool                cube_open;     // the Horadric Cube (right click it in the inventory)
    const HirelingView *hire;          // the hireling's screen (NULL: closed)
    Sprite             *hire_panel;
    Sprite             *cube_panel;
    char                cube_message[200];
    IconCache           icons[ICON_CACHE];
    int                 icon_count;
    Label              *text;  // tooltip lines (font16)
    Label              *small; // belt keys, quantities
    // The item on the cursor, and where it came from (it goes back there if the panel closes)
    bool   holding;
    Item   held;
    Origin origin;
    int    origin_index; // grid: x + y * INVENTORY_W + page * INVENTORY_PAGE; equip: the slot; belt: the slot
    bool   modern;       // Options > Gameplay > Picking up worn gear: Modern
    bool   still_worn;   // Modern: the carried item is still worn (in origin_index's slot) until it's put down
    char   sound[32];    // what was last put down sounds like
    bool   no_room;      // the panel closed and the carried item had nowhere to go: it stays on the cursor
    // Identifying with a scroll or tome: the next item clicked; where the scroll is (the belt slot, or the grid's
    // cell x + y * INVENTORY_W + page * INVENTORY_PAGE)
    bool identifying;
    bool id_from_belt;
    int  id_where;
};

const char *GameInventory_Sound(const GameInventory *inventory) { return inventory->sound; }

static void GameInventory__PutDown(GameInventory *inventory, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    snprintf(inventory->sound, sizeof(inventory->sound), "%s", base != NULL ? base->use_sound : "");
}

GameInventory *GameInventory_Create(void) {
    GameInventory *inventory = calloc(1, sizeof(GameInventory));
    FAIL_IF_NULL(inventory);
    inventory->panel        = Sprite_Create(INVENTORY_PANEL, PALETTE_UNITS);
    inventory->gold_button  = Sprite_Create("/data/global/ui/PANEL/goldcoinbtn.dc6", PALETTE_SKY);
    inventory->stash_panel  = Sprite_Create(STASH_PANEL, PALETTE_SKY);
    inventory->bank_panel   = Sprite_Create(BANK_PANEL, PALETTE_SKY);
    inventory->socket       = Sprite_Create("/data/global/ui/PANEL/gemsocket.dc6", PALETTE_UNITS);
    inventory->shop_panel   = Sprite_Create(SHOP_PANEL, PALETTE_SKY);
    inventory->trade_panel  = Sprite_Create(TRADE_PANEL, PALETTE_SKY);
    inventory->shop_tabs    = Sprite_Create("/data/global/ui/PANEL/buysellTabs.dc6", PALETTE_SKY);
    inventory->shop_buttons = Sprite_Create("/data/global/ui/PANEL/buysellbtn.dc6", PALETTE_SKY);
    inventory->cube_panel   = Sprite_Create(CUBE_PANEL, PALETTE_SKY);
    inventory->hire_panel   = Sprite_Create(HIRE_PANEL, PALETTE_SKY);
    inventory->text         = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    inventory->small        = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    return inventory;
}

void GameInventory_Destroy(GameInventory **inventory_ref) {
    GameInventory *inventory = *inventory_ref;
    if (inventory == NULL) {
        return;
    }
    Sprite_Destroy(&inventory->panel);
    Sprite_Destroy(&inventory->gold_button);
    Sprite_Destroy(&inventory->stash_panel);
    Sprite_Destroy(&inventory->bank_panel);
    Sprite_Destroy(&inventory->socket);
    Sprite_Destroy(&inventory->shop_panel);
    Sprite_Destroy(&inventory->trade_panel);
    Sprite_Destroy(&inventory->shop_tabs);
    Sprite_Destroy(&inventory->shop_buttons);
    for (int i = 0; i < inventory->icon_count; i++) {
        Sprite_Destroy(&inventory->icons[i].sprite);
    }
    Label_Destroy(&inventory->text);
    Label_Destroy(&inventory->small);
    free(inventory);
    *inventory_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pictures

// A colour shift table (data/global/items/Palette/*.dat: 256 entries a colour), by InvTrans, false if none
static bool GameInventory__Shift(const int shift, const int color, uint8_t remap[256]) {
    static const char *const files[] = {NULL,        "grey",    "grey2",    "gold",        "brown",
                                        "greybrown", "invgrey", "invgrey2", "invgreybrown"};
    if (shift <= 0 || shift >= (int)(sizeof(files) / sizeof(files[0])) || color < 0) {
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/Palette/%s.dat", files[shift]);
    MpqStream *stream = FileManager_FileExists(path) ? FileManager_OpenFile(path) : NULL;
    if (stream == NULL) {
        return false;
    }
    const uint32_t offset = (uint32_t)color * 256;
    bool           ok     = MpqStream_GetSize(stream) >= offset + 256;
    if (ok) {
        MpqStream_Seek(stream, offset, SEEK_SET);
        ok = MpqStream_Read(stream, remap, 0, 256) == 256;
    }
    MpqStream_Destroy(&stream);
    return ok;
}

static Sprite *GameInventory__Icon(GameInventory *inventory, const char *file, const int shift, const int color) {
    if (file == NULL || file[0] == '\0') {
        return NULL;
    }
    char key[32];
    snprintf(key, sizeof(key), shift > 0 ? "%.20s#%d.%d" : "%.20s", file, shift, color); // (each tint its own)
    for (int i = 0; i < inventory->icon_count; i++) {
        if (strcmp(inventory->icons[i].file, key) == 0) {
            return inventory->icons[i].sprite;
        }
    }
    if (inventory->icon_count >= ICON_CACHE) {
        return NULL;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/%s.dc6", file);
    IconCache *entry = &inventory->icons[inventory->icon_count++];
    snprintf(entry->file, sizeof(entry->file), "%s", key);
    uint8_t remap[256];
    entry->sprite = GameInventory__Shift(shift, color, remap) ? Sprite_CreateShifted(path, PALETTE_UNITS, remap)
                                                              : Sprite_Create(path, PALETTE_UNITS);
    return entry->sprite;
}

// An item's picture, centred in a box; tinted red when it can't be used
static void GameInventory__DrawItem(GameInventory *inventory, const Item *item, const SDL_Rect *box,
                                    const bool usable) {
    int        shift  = 0;
    int        color  = -1;
    const bool tinted = Affixes_InvTint(item, &shift, &color); // (uniques, sets and some affixes colour it)
    Sprite    *icon   = GameInventory__Icon(inventory, Affixes_InvFile(item), tinted ? shift : 0,
                                            color); // (uniques have their own picture)
    if (icon == NULL) {
        return;
    }
    if (!usable || Item_IsBroken(item)) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 140, 0, 0, 110);
        SDL_RenderFillRect(sdl_renderer, box);
    }
    int w;
    int h;
    Sprite_GetFrameSize(icon, 0, &w, &h);
    Sprite_DrawScaled(icon, 0, box->x + ((box->w - w) / 2), box->y + ((box->h - h) / 2), 1.0f);
}

static SDL_Rect GameInventory__GridRect(const InventoryItem *it, const ItemBase *base) {
    const SDL_Rect rect = {GRID_X + (it->x * CELL), GRID_Y + (it->y * CELL), (base != NULL ? base->width : 1) * CELL,
                           (base != NULL ? base->height : 1) * CELL};
    return rect;
}

static SDL_Rect GameInventory__BeltRect(const int slot) {
    const SDL_Rect rect = {BELT_X + (slot * BELT_STEP), BELT_Y, CELL, CELL};
    return rect;
}

static bool GameInventory__In(const SDL_Rect *rect, const int x, const int y) {
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

// ---------------------------------------------------------------------------------------------------------------------
// Where the mouse is

bool GameInventory_IsOver(const int mouse_x, const int mouse_y) {
    return mouse_x >= GAME_PANEL_X && mouse_y < GAME_PANEL_H;
}

void GameInventory_SetStash(GameInventory *inventory, const bool open) {
    inventory->stash_open = open;
    inventory->cube_open  = open ? false : inventory->cube_open;
}

bool GameInventory_CubeOpen(const GameInventory *inventory) { return inventory->cube_open; }

void GameInventory_SetHireling(GameInventory *inventory, const HirelingView *view) {
    inventory->hire = view;
    if (view != NULL) {
        inventory->stash_open = false;
        inventory->cube_open  = false;
    }
}

bool GameInventory_HirelingOpen(const GameInventory *inventory) { return inventory->hire != NULL; }

static int GameInventory__HireSlotAt(const int mouse_x, const int mouse_y) {
    for (int s = 0; s < HIRE_SLOTS; s++) {
        if (GameInventory__In(&hire_rects[s], mouse_x, mouse_y)) {
            return s;
        }
    }
    return -1;
}

static bool GameInventory__CubeCellAt(const int mouse_x, const int mouse_y, int *cell_x, int *cell_y) {
    if (mouse_x < CUBE_GRID_X || mouse_y < CUBE_GRID_Y || mouse_x >= CUBE_GRID_X + (CUBE_W * CELL) ||
        mouse_y >= CUBE_GRID_Y + (CUBE_H * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - CUBE_GRID_X) / CELL;
    *cell_y = (mouse_y - CUBE_GRID_Y) / CELL;
    return true;
}

static SDL_Rect GameInventory__CubeRect(const InventoryItem *it, const ItemBase *base) {
    const SDL_Rect rect = {CUBE_GRID_X + (it->x * CELL), CUBE_GRID_Y + (it->y * CELL),
                           (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
    return rect;
}

void GameInventory_SetShop(GameInventory *inventory, ShopStock *stock, const int difficulty) {
    inventory->shop       = stock;
    inventory->difficulty = difficulty;
    if (stock != NULL) {
        inventory->stash_open = false;
        inventory->cube_open  = false;
        // The first tab with something on it
        inventory->shop_tab = 0;
        for (int t = SHOP_TABS - 1; t >= 0; t--) {
            for (int i = 0; i < stock->count; i++) {
                inventory->shop_tab = stock->items[i].page == t ? t : inventory->shop_tab;
            }
        }
    }
}

bool GameInventory_ShopOpen(const GameInventory *inventory) { return inventory->shop != NULL; }

void GameInventory_SetTrade(GameInventory *inventory, TradeView *trade) {
    inventory->trade = trade;
    if (trade != NULL) {
        inventory->stash_open = false;
        inventory->cube_open  = false;
        inventory->shop       = NULL;
    }
}

bool GameInventory_TradeOpen(const GameInventory *inventory) { return inventory->trade != NULL; }

int GameInventory_TradeAt(const InventoryItem *items, const int count, const int cell_x, const int cell_y) {
    for (int i = 0; i < count; i++) {
        const ItemBase *base = Items_Find(items[i].item.code);
        const int       w    = base != NULL ? base->width : 1;
        const int       h    = base != NULL ? base->height : 1;
        if (cell_x >= items[i].x && cell_x < items[i].x + w && cell_y >= items[i].y && cell_y < items[i].y + h) {
            return i;
        }
    }
    return -1;
}

// What putting an item there would lift: -1 nothing, an index, -2 more than one or off the grid
static int GameInventory__TradeCovered(const InventoryItem *items, const int count, const ItemBase *base, const int x,
                                       const int y) {
    if (base == NULL || x < 0 || y < 0 || x + base->width > TRADE_W || y + base->height > TRADE_H) {
        return -2;
    }
    int found = -1;
    for (int cy = y; cy < y + base->height; cy++) {
        for (int cx = x; cx < x + base->width; cx++) {
            const int at = GameInventory_TradeAt(items, count, cx, cy);
            if (at >= 0 && found >= 0 && at != found) {
                return -2;
            }
            found = at >= 0 ? at : found;
        }
    }
    return found;
}

bool GameInventory_TradePlace(InventoryItem *items, int *count, const Item *item, const int x, const int y) {
    const ItemBase *base = Items_Find(item->code);
    if (*count >= TRADE_ITEMS || GameInventory__TradeCovered(items, *count, base, x, y) != -1) {
        return false;
    }
    items[*count].item = *item;
    items[*count].x    = x;
    items[*count].y    = y;
    items[*count].page = 0;
    (*count)++;
    return true;
}

bool GameInventory_TradeAdd(InventoryItem *items, int *count, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    for (int x = 0; base != NULL && x + base->width <= TRADE_W; x++) {
        for (int y = 0; y + base->height <= TRADE_H; y++) {
            if (GameInventory_TradePlace(items, count, item, x, y)) {
                return true;
            }
        }
    }
    return false;
}

static void GameInventory__TradeRemove(InventoryItem *items, int *count, const int index) {
    for (int i = index; i + 1 < *count; i++) {
        items[i] = items[i + 1];
    }
    (*count)--;
}

static bool GameInventory__TradeCellAt(const int mouse_x, const int mouse_y, const int top, int *cell_x, int *cell_y) {
    if (mouse_x < TRADE_GRID_X || mouse_y < top || mouse_x >= TRADE_GRID_X + (TRADE_W * CELL) ||
        mouse_y >= top + (TRADE_H * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - TRADE_GRID_X) / CELL;
    *cell_y = (mouse_y - top) / CELL;
    return true;
}

static bool GameInventory__InBox(const int mouse_x, const int mouse_y, const int x, const int y) {
    return mouse_x >= x && mouse_x < x + SHOP_BUTTON_SZ && mouse_y >= y && mouse_y < y + SHOP_BUTTON_SZ;
}

bool GameInventory_IsOverShop(const GameInventory *inventory, const int mouse_x, const int mouse_y) {
    return (inventory->shop != NULL || inventory->trade != NULL) && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H;
}

static bool GameInventory__ShopCellAt(const int mouse_x, const int mouse_y, int *cell_x, int *cell_y) {
    if (mouse_x < SHOP_GRID_X || mouse_y < SHOP_GRID_Y || mouse_x >= SHOP_GRID_X + (SHOP_W * CELL) ||
        mouse_y >= SHOP_GRID_Y + (SHOP_H * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - SHOP_GRID_X) / CELL;
    *cell_y = (mouse_y - SHOP_GRID_Y) / CELL;
    return true;
}

static SDL_Rect GameInventory__ShopRect(const InventoryItem *it, const ItemBase *base) {
    const SDL_Rect rect = {SHOP_GRID_X + (it->x * CELL), SHOP_GRID_Y + (it->y * CELL),
                           (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
    return rect;
}

static bool GameInventory__OverShopClose(const int mouse_x, const int mouse_y) {
    return mouse_x >= SHOP_CLOSE_X && mouse_x < SHOP_CLOSE_X + SHOP_BUTTON_SZ && mouse_y >= SHOP_CLOSE_Y &&
           mouse_y < SHOP_CLOSE_Y + SHOP_BUTTON_SZ;
}

// Selling to the merchant: gold for it, and it joins their wares
static void GameInventory__Sell(GameInventory *inventory, Character *character, const Item *item,
                                const char **message) {
    const int price = Shop_SellPrice(inventory->shop, item, inventory->difficulty);
    const int limit = Inventory_GoldLimit(character);
    character->gold = character->gold + price > limit ? limit : character->gold + price;
    Shop_Take(inventory->shop, item);
    snprintf(inventory->sound, sizeof(inventory->sound), "item_gold");
    (void)message;
}

bool GameInventory_StashOpen(const GameInventory *inventory) { return inventory->stash_open; }

bool GameInventory_IsOverStash(const GameInventory *inventory, const int mouse_x, const int mouse_y) {
    return (inventory->stash_open || inventory->cube_open || inventory->hire != NULL) && mouse_x < GAME_PANEL_W &&
           mouse_y < GAME_PANEL_H;
}

static bool GameInventory__StashCellAt(const int mouse_x, const int mouse_y, int *cell_x, int *cell_y) {
    if (mouse_x < STASH_GRID_X || mouse_y < stash_grid_y || mouse_x >= STASH_GRID_X + (STASH_W * CELL) ||
        mouse_y >= stash_grid_y + (stash_rows * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - STASH_GRID_X) / CELL;
    *cell_y = (mouse_y - stash_grid_y) / CELL;
    return true;
}

static SDL_Rect GameInventory__StashRect(const InventoryItem *it, const ItemBase *base) {
    const SDL_Rect rect = {STASH_GRID_X + (it->x * CELL), stash_grid_y + (it->y * CELL),
                           (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
    return rect;
}

static bool GameInventory__OverStashGold(const int mouse_x, const int mouse_y) {
    return mouse_x >= STASH_GOLD_BUTTON_X && mouse_x < STASH_GOLD_BUTTON_X + GOLD_BUTTON_W && mouse_y >= stash_gold_y &&
           mouse_y < stash_gold_y + GOLD_BUTTON_H;
}

static int GameInventory__BeltSlotAt(const int mouse_x, const int mouse_y) {
    for (int s = 0; s < 4; s++) {
        const SDL_Rect rect = GameInventory__BeltRect(s);
        if (GameInventory__In(&rect, mouse_x, mouse_y)) {
            return s;
        }
    }
    return -1;
}

bool GameInventory_IsOverBelt(const Character *character, const int mouse_x, const int mouse_y) {
    (void)character;
    return GameInventory__BeltSlotAt(mouse_x, mouse_y) >= 0;
}

static int GameInventory__SlotAt(const int mouse_x, const int mouse_y) {
    for (int s = 0; s < EQUIP_COUNT; s++) {
        if (GameInventory__In(&slot_rects[s], mouse_x, mouse_y)) {
            return s;
        }
    }
    return -1;
}

static bool GameInventory__CellAt(const int mouse_x, const int mouse_y, int *cell_x, int *cell_y) {
    if (mouse_x < GRID_X || mouse_y < GRID_Y || mouse_x >= GRID_X + (INVENTORY_W * CELL) ||
        mouse_y >= GRID_Y + (INVENTORY_H * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - GRID_X) / CELL;
    *cell_y = (mouse_y - GRID_Y) / CELL;
    return true;
}

// Where a carried item would go: its top-left cell, with the item centred on the mouse
static void GameInventory__DropCell(const ItemBase *base, const int mouse_x, const int mouse_y, int *x, int *y) {
    const int left = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
    const int top  = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
    *x             = left >= GRID_X ? (left - GRID_X) / CELL : -1;
    *y             = top >= GRID_Y ? (top - GRID_Y) / CELL : -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Picking up, putting down, drinking

static void GameInventory__Take(GameInventory *inventory, const Item *item, const Origin origin, const int index) {
    inventory->holding      = true;
    inventory->held         = *item;
    inventory->origin       = origin;
    inventory->origin_index = index;
}

bool GameInventory_Holding(const GameInventory *inventory) { return inventory->holding; }

void GameInventory_SetModernPickup(GameInventory *inventory, const bool modern) { inventory->modern = modern; }

void GameInventory_SetBeltPosition(const int x, const int y) {
    belt_x = x;
    belt_y = y;
}

// The page arrows under the grid (shown with more than one page): < 2 / 5 >
#define PAGE_Y       390
#define PAGE_H       24
#define PAGE_ARROW_W 22
#define PAGE_LEFT_X  (GAME_PANEL_X + 206)
#define PAGE_RIGHT_X (GAME_PANEL_X + 290)
static bool GameInventory__PageArrow(const int mouse_x, const int mouse_y, int *step) {
    if (Inventory_Pages() <= 1 || mouse_y < PAGE_Y || mouse_y >= PAGE_Y + PAGE_H) {
        return false;
    }
    if (mouse_x >= PAGE_LEFT_X && mouse_x < PAGE_LEFT_X + PAGE_ARROW_W) {
        *step = -1;
        return true;
    }
    if (mouse_x >= PAGE_RIGHT_X && mouse_x < PAGE_RIGHT_X + PAGE_ARROW_W) {
        *step = 1;
        return true;
    }
    return false;
}

bool GameInventory_PutBack(GameInventory *inventory, Character *character) {
    if (!inventory->holding) {
        return true;
    }
    if (inventory->still_worn) { // Modern: it never came off
        inventory->holding    = false;
        inventory->still_worn = false;
        return true;
    }
    bool placed = false;
    switch (inventory->origin) {
    case ORIGIN_EQUIP:
        if (character->equipped[inventory->origin_index].code[0] == '\0') {
            character->equipped[inventory->origin_index] = inventory->held;
            placed                                       = true;
        }
        break;
    case ORIGIN_BELT:
        if (character->belt[inventory->origin_index].code[0] == '\0') {
            character->belt[inventory->origin_index] = inventory->held;
            placed                                   = true;
        }
        break;
    case ORIGIN_GRID:
        placed = Inventory_PlaceOnPage(character, &inventory->held, inventory->origin_index % INVENTORY_W,
                                       (inventory->origin_index % INVENTORY_PAGE) / INVENTORY_W,
                                       inventory->origin_index / INVENTORY_PAGE);
        break;
    case ORIGIN_STASH:
        placed = Stash_Place(character, &inventory->held, inventory->origin_index % STASH_W,
                             inventory->origin_index / STASH_W);
        break;
    case ORIGIN_CUBE:
        placed =
            Cube_Place(character, &inventory->held, inventory->origin_index % CUBE_W, inventory->origin_index / CUBE_W);
        break;
    case ORIGIN_TRADE: // back in the inventory (taking it off the offer is what the trade sees)
        break;
    case ORIGIN_GROUND: // picked up off the ground: anywhere it fits
        break;
    }
    const ItemBase *base = Items_Find(inventory->held.code);
    if (!placed && base != NULL && base->beltable) {
        placed = Inventory_AddToBelt(character, &inventory->held);
    }
    if (!placed) {
        placed = Inventory_Add(character, &inventory->held);
    }
    if (!placed) {
        return false; // nowhere for it: still on the cursor (a click in the world drops it)
    }
    inventory->holding = false;
    return true;
}

bool GameInventory_Drop(GameInventory *inventory, Character *character, Item *item, bool *equipment_changed) {
    *equipment_changed = false;
    if (!inventory->holding) {
        return false;
    }
    if (inventory->still_worn) { // Modern: it comes off now, onto the ground
        memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
        inventory->still_worn = false;
        *equipment_changed    = true;
    }
    *item              = inventory->held;
    inventory->holding = false;
    return true;
}

void GameInventory_PutBackCopy(const GameInventory *inventory, Character *copy) {
    GameInventory scratch = *inventory; // the real one keeps carrying it
    GameInventory_PutBack(&scratch, copy);
}

void GameInventory_Hold(GameInventory *inventory, const Item *item) {
    GameInventory__Take(inventory, item, ORIGIN_GROUND, 0);
}

// Scrolls and tomes of identify
static bool GameInventory__Identifies(const Item *item) {
    return strcmp(item->code, "isc") == 0 || strcmp(item->code, "ibk") == 0;
}

static bool GameInventory__AnyUnidentified(const Character *character) {
    for (int i = 0; i < character->inventory_count; i++) {
        if (character->inventory[i].item.unidentified) {
            return true;
        }
    }
    for (int s = 0; s < EQUIP_COUNT; s++) {
        if (character->equipped[s].unidentified) {
            return true;
        }
    }
    return false;
}

// Right click on a scroll or tome of identify: the next item clicked is identified with it
static bool GameInventory__StartIdentify(GameInventory *inventory, const Character *character, const Item *scroll,
                                         const bool from_belt, const int where, const char **message) {
    if (!GameInventory__Identifies(scroll)) {
        return false;
    }
    if (strcmp(scroll->code, "ibk") == 0 && scroll->quantity <= 0) {
        *message = "The tome is empty: Akara and the other healers sell scrolls to fill it.";
    } else if (!GameInventory__AnyUnidentified(character)) {
        *message = "You have nothing to identify.";
    } else {
        inventory->identifying  = true;
        inventory->id_from_belt = from_belt;
        inventory->id_where     = where;
        *message                = "Click the item to identify.";
    }
    return true;
}

// The scroll used up (a tome: one charge)
static void GameInventory__UseIdentifyScroll(GameInventory *inventory, Character *character) {
    Item *scroll = NULL;
    int   index  = -1;
    if (inventory->id_from_belt) {
        scroll = &character->belt[inventory->id_where];
    } else {
        const int page = inventory->id_where / INVENTORY_PAGE;
        const int cell = inventory->id_where % INVENTORY_PAGE;
        for (int i = 0; i < character->inventory_count; i++) {
            const InventoryItem *it = &character->inventory[i];
            if (it->page == page && it->x == cell % INVENTORY_W && it->y == cell / INVENTORY_W) {
                scroll = &character->inventory[i].item;
                index  = i;
            }
        }
    }
    if (scroll == NULL || !GameInventory__Identifies(scroll)) {
        return;
    }
    if (strcmp(scroll->code, "ibk") == 0) {
        scroll->quantity = scroll->quantity > 0 ? scroll->quantity - 1 : 0;
    } else if (inventory->id_from_belt) {
        memset(scroll, 0, sizeof(Item));
        Inventory_SettleBelt(character);
    } else {
        Inventory_Remove(character, index);
    }
}

// While identifying: a click on an unidentified item identifies it; anything else puts the scroll away
static int GameInventory__Identify(GameInventory *inventory, Character *character, const int mouse_x, const int mouse_y,
                                   const bool left_click, const char **message) {
    Item *target = NULL;
    int   cell_x;
    int   cell_y;
    if (left_click && GameInventory__CellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
        const int under = Inventory_ItemAt(character, cell_x, cell_y);
        target          = under >= 0 ? &character->inventory[under].item : NULL;
    } else if (left_click) {
        const int slot = GameInventory__SlotAt(mouse_x, mouse_y);
        target         = slot >= 0 ? &character->equipped[slot] : NULL;
    }
    inventory->identifying = false;
    if (target == NULL || !target->unidentified) {
        return INVENTORY_EVENT_NONE;
    }
    target->unidentified = false;
    GameInventory__UseIdentifyScroll(inventory, character);
    snprintf(inventory->sound, sizeof(inventory->sound), "cursor_identify_item");
    *message = "Identified.";
    return INVENTORY_EVENT_EQUIPMENT | INVENTORY_EVENT_MOVED; // (its magic counts now, if it's worn)
}

static bool GameInventory__Drink(Item *item, Item *drunk) {
    const ItemBase *base = Items_Find(item->code);
    if (!Items_IsPotion(base)) {
        return false;
    }
    *drunk = *item;
    memset(item, 0, sizeof(Item));
    return true;
}

bool GameInventory_DrinkBelt(Character *character, const int slot, Item *drunk) {
    if (slot < 0 || slot >= 4 || !GameInventory__Drink(&character->belt[slot], drunk)) {
        return false;
    }
    Inventory_SettleBelt(character);
    return true;
}

static void GameInventory__StashLayout(const Character *character) {
    stash_grid_y = character->expansion ? 82 : 273;
    stash_gold_y = character->expansion ? 23 : 219;
    stash_rows   = Stash_Rows(character);
}

int GameInventory_Update(GameInventory *inventory, Character *character, const bool open, const int mouse_x,
                         const int mouse_y, const bool left_click, const bool right_click, const bool shift,
                         Item *drunk, const char **message) {
    GameInventory__StashLayout(character);
    *message            = NULL;
    inventory->sound[0] = '\0';
    int events          = INVENTORY_EVENT_NONE;
    if (!open && inventory->holding && !inventory->no_room) {
        inventory->no_room = !GameInventory_PutBack(inventory, character); // no room: it stays on the cursor
    }
    if (open || !inventory->holding) {
        inventory->no_room = false;
    }
    if (inventory->cube_open && (!open || !Cube_Carried(character))) {
        inventory->cube_open = false; // closed with the inventory, or the cube's gone from it
    }
    if (inventory->identifying) {
        if (!open || inventory->holding) {
            inventory->identifying = false;
        } else if (left_click || right_click) {
            return GameInventory__Identify(inventory, character, mouse_x, mouse_y, left_click, message);
        }
    }

    // The belt (on the control panel): right click drinks; with the inventory open, left click picks up / puts down
    const int belt_slot = GameInventory__BeltSlotAt(mouse_x, mouse_y);
    if (belt_slot >= 0) {
        if (right_click && GameInventory_DrinkBelt(character, belt_slot, drunk)) {
            return INVENTORY_EVENT_DRINK;
        }
        if (right_click && GameInventory__Identifies(&character->belt[belt_slot])) {
            if (!open) {
                *message = "Open the inventory (I) to identify with it.";
            } else {
                GameInventory__StartIdentify(inventory, character, &character->belt[belt_slot], true, belt_slot,
                                             message);
            }
            return events;
        }
        if (!open && left_click && character->belt[belt_slot].code[0] != '\0') {
            *message = "Right click drinks it; open the inventory (I) to move belt items around.";
        }
        if (open && left_click) {
            Item *slot = &character->belt[belt_slot];
            if (inventory->holding) {
                const ItemBase *base = Items_Find(inventory->held.code);
                if (base == NULL || !base->beltable) {
                    *message = "Only potions and scrolls go in the belt.";
                } else {
                    const Item before = *slot;
                    GameInventory__PutDown(inventory, &inventory->held);
                    *slot              = inventory->held;
                    inventory->holding = false;
                    if (before.code[0] != '\0') {
                        GameInventory__Take(inventory, &before, ORIGIN_BELT, belt_slot);
                    }
                    events |= INVENTORY_EVENT_MOVED;
                }
            } else if (slot->code[0] != '\0') {
                GameInventory__Take(inventory, slot, ORIGIN_BELT, belt_slot);
                memset(slot, 0, sizeof(Item));
                Inventory_SettleBelt(character);
                events |= INVENTORY_EVENT_MOVED;
            }
        }
        return events;
    }
    if (!open) {
        return events;
    }

    // Gold: drop some (at the stash: put it in)
    if (left_click && !inventory->holding && mouse_x >= GOLD_BUTTON_X && mouse_x < GOLD_BUTTON_X + GOLD_BUTTON_W &&
        mouse_y >= GOLD_BUTTON_Y && mouse_y < GOLD_BUTTON_Y + GOLD_BUTTON_H) {
        if (character->gold > 0) {
            return events | INVENTORY_EVENT_GOLD;
        }
        *message = "You have no gold.";
        return events;
    }

    // Trading with another player: your offer's grid (to and from the inventory), your gold, accept and close
    if (inventory->trade != NULL && left_click && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H) {
        TradeView *trade = inventory->trade;
        if (GameInventory__InBox(mouse_x, mouse_y, TRADE_ACCEPT_X, TRADE_BUTTONS_Y)) {
            return events | INVENTORY_EVENT_TRADE_OK;
        }
        if (GameInventory__InBox(mouse_x, mouse_y, TRADE_CLOSE_X, TRADE_BUTTONS_Y)) {
            return events | INVENTORY_EVENT_TRADE_END;
        }
        if (mouse_x >= 156 && mouse_x < 306 && mouse_y >= 225 && mouse_y < 245 && !inventory->holding) {
            return events | INVENTORY_EVENT_TRADE_GOLD;
        }
        int tx;
        int ty;
        if (!GameInventory__TradeCellAt(mouse_x, mouse_y, TRADE_MY_Y, &tx, &ty)) {
            return events; // (their side is only to look at)
        }
        if (inventory->holding) {
            const ItemBase *base    = Items_Find(inventory->held.code);
            const int       left    = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
            const int       top     = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
            const int       x       = left >= TRADE_GRID_X ? (left - TRADE_GRID_X) / CELL : -1;
            const int       y       = top >= TRADE_MY_Y ? (top - TRADE_MY_Y) / CELL : -1;
            const int       covered = GameInventory__TradeCovered(trade->mine, trade->mine_count, base, x, y);
            if (covered == -2) {
                return events;
            }
            if (inventory->still_worn) {
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            Item lifted;
            memset(&lifted, 0, sizeof(lifted));
            if (covered >= 0) {
                lifted = trade->mine[covered].item;
                GameInventory__TradeRemove(trade->mine, &trade->mine_count, covered);
            }
            GameInventory_TradePlace(trade->mine, &trade->mine_count, &inventory->held, x, y);
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding = false;
            if (lifted.code[0] != '\0') {
                GameInventory__Take(inventory, &lifted, ORIGIN_TRADE, 0);
            }
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_TRADE;
        }
        const int under = GameInventory_TradeAt(trade->mine, trade->mine_count, tx, ty);
        if (under >= 0) {
            const Item item = trade->mine[under].item;
            GameInventory__TradeRemove(trade->mine, &trade->mine_count, under);
            GameInventory__Take(inventory, &item, ORIGIN_TRADE, 0);
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_TRADE;
        }
        return events;
    }

    // A merchant's store: the tabs, the close button, buying (click an item) and selling (put an item on it)
    if (inventory->shop != NULL && (left_click || right_click) && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H) {
        if (left_click && mouse_y < SHOP_TAB_H && mouse_x < SHOP_TAB_W * SHOP_TABS) {
            inventory->shop_tab = mouse_x / SHOP_TAB_W;
            return events;
        }
        if (left_click && GameInventory__OverShopClose(mouse_x, mouse_y)) {
            return events | INVENTORY_EVENT_SHOP_CLOSE;
        }
        int shop_x;
        int shop_y;
        if (!GameInventory__ShopCellAt(mouse_x, mouse_y, &shop_x, &shop_y)) {
            return events;
        }
        if (inventory->holding) {
            if (inventory->still_worn) { // Modern: it comes off now, sold
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            GameInventory__Sell(inventory, character, &inventory->held, message);
            inventory->holding = false;
            return events | INVENTORY_EVENT_MOVED;
        }
        const int under = Shop_ItemAt(inventory->shop, inventory->shop_tab, shop_x, shop_y);
        if (under < 0) {
            return events;
        }
        Item      item  = inventory->shop->items[under].item;
        const int price = Shop_Price(inventory->shop, &item);
        if (price > character->gold) {
            *message = "You don't have enough gold for that.";
            return events;
        }
        if (inventory->shop->gamble) {
            Shop_Gamble(&item, character->level); // what it really is (the same size: if there's no room, no harm)
        }
        const ItemBase *base   = Items_Find(item.code);
        const bool      placed = (shift && base != NULL && base->beltable && Inventory_AddToBelt(character, &item)) ||
                            Inventory_Add(character, &item);
        if (!placed) {
            *message = "You don't have room for that.";
            return events;
        }
        if (inventory->shop->gamble) {
            static char got[200]; // what the gamble turned out to be
            char        name1[96];
            char        name2[96];
            Affixes_Name(&item, name1, sizeof(name1), name2, sizeof(name2));
            snprintf(got, sizeof(got), "%s%s%s", name1, name2[0] != '\0' ? " - " : "", name2);
            *message = got;
        }
        character->gold -= price;
        Shop_Remove(inventory->shop, under);
        snprintf(inventory->sound, sizeof(inventory->sound), "item_gold");
        return events | INVENTORY_EVENT_MOVED;
    }

    // The hireling's screen: put things on them, take them off
    if (inventory->hire != NULL && left_click && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H) {
        if (GameInventory__InBox(mouse_x, mouse_y, HIRE_CLOSE_X, HIRE_CLOSE_Y)) {
            return events | INVENTORY_EVENT_HIRE_CLOSE;
        }
        const int slot = GameInventory__HireSlotAt(mouse_x, mouse_y);
        if (slot < 0) {
            return events;
        }
        Item *worn = &character->hireling_items[slot];
        if (inventory->holding) {
            const char *why = NULL;
            if (!Hirelings_CanWear(inventory->hire->row, inventory->hire->level, &inventory->held, slot, &why)) {
                *message = why;
                return events;
            }
            if (inventory->still_worn) { // Modern: it comes off you now
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            const Item before = *worn;
            *worn             = inventory->held;
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding = false;
            if (before.code[0] != '\0') {
                GameInventory__Take(inventory, &before, ORIGIN_GROUND, 0); // (back on the cursor: anywhere it fits)
            }
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_HIRELING;
        }
        if (worn->code[0] != '\0') {
            GameInventory__Take(inventory, worn, ORIGIN_GROUND, 0);
            memset(worn, 0, sizeof(Item));
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_HIRELING;
        }
        return events;
    }

    // The Horadric Cube: its inside (like the stash), Transmute and close
    if (inventory->cube_open && (left_click || right_click) && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H) {
        if (left_click && !inventory->holding &&
            GameInventory__InBox(mouse_x, mouse_y, CUBE_TRANSMUTE_X, CUBE_TRANSMUTE_Y)) {
            const bool made = Cube_Transmute(character, inventory->cube_message, sizeof(inventory->cube_message));
            *message        = inventory->cube_message;
            snprintf(inventory->sound, sizeof(inventory->sound), "%s", made ? "cursor_convert_item" : "cursor_error");
            return events | INVENTORY_EVENT_MOVED;
        }
        if (left_click && GameInventory__InBox(mouse_x, mouse_y, CUBE_CLOSE_X, CUBE_CLOSE_Y)) {
            if (!GameInventory_PutBack(inventory, character)) {
                return events;
            }
            inventory->cube_open = false;
            return events;
        }
        int cx;
        int cy;
        if (!GameInventory__CubeCellAt(mouse_x, mouse_y, &cx, &cy)) {
            return events;
        }
        const int under = Cube_ItemAt(character, cx, cy);
        if (right_click) {
            if (under >= 0) {
                *message = "Take it out of the cube first.";
            }
            return events;
        }
        if (inventory->holding) {
            const ItemBase *base    = Items_Find(inventory->held.code);
            const int       left    = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
            const int       top     = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
            const int       x       = left >= CUBE_GRID_X ? (left - CUBE_GRID_X) / CELL : -1;
            const int       y       = top >= CUBE_GRID_Y ? (top - CUBE_GRID_Y) / CELL : -1;
            const int       covered = Cube_Covered(character, base, x, y);
            if (covered == -2) {
                if (strcmp(inventory->held.code, CUBE_ITEM) == 0) {
                    *message = "The cube doesn't go inside itself.";
                }
                return events;
            }
            if (inventory->still_worn) { // Modern: it comes off now, into the cube
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            Item lifted;
            memset(&lifted, 0, sizeof(lifted));
            int lifted_at = 0;
            if (covered >= 0) {
                lifted    = character->cube[covered].item;
                lifted_at = character->cube[covered].x + (character->cube[covered].y * CUBE_W);
                Cube_Remove(character, covered);
            }
            Cube_Place(character, &inventory->held, x, y);
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding = false;
            if (lifted.code[0] != '\0') {
                GameInventory__Take(inventory, &lifted, ORIGIN_CUBE, lifted_at);
            }
            return events | INVENTORY_EVENT_MOVED;
        }
        if (under >= 0) {
            const InventoryItem it = character->cube[under];
            Cube_Remove(character, under);
            GameInventory__Take(inventory, &it.item, ORIGIN_CUBE, it.x + (it.y * CUBE_W));
            return events | INVENTORY_EVENT_MOVED;
        }
        return events;
    }

    // The stash
    if (inventory->stash_open && left_click && !inventory->holding && GameInventory__OverStashGold(mouse_x, mouse_y)) {
        if (character->stash_gold > 0) {
            return events | INVENTORY_EVENT_STASH_GOLD;
        }
        *message = "There's no gold in your stash.";
        return events;
    }
    int stash_x;
    int stash_y;
    if (inventory->stash_open && GameInventory__StashCellAt(mouse_x, mouse_y, &stash_x, &stash_y)) {
        const int under = Stash_ItemAt(character, stash_x, stash_y);
        if (right_click && under >= 0) {
            *message = "Take it out of the stash first.";
            return events;
        }
        if (!left_click) {
            return events;
        }
        if (inventory->holding) {
            const ItemBase *base    = Items_Find(inventory->held.code);
            const int       left    = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
            const int       top     = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
            const int       x       = left >= STASH_GRID_X ? (left - STASH_GRID_X) / CELL : -1;
            const int       y       = top >= stash_grid_y ? (top - stash_grid_y) / CELL : -1;
            const int       covered = Stash_Covered(character, base, x, y);
            if (covered == -2) {
                return events;
            }
            if (inventory->still_worn) { // Modern: it comes off now, into the stash
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            Item lifted;
            memset(&lifted, 0, sizeof(lifted));
            int lifted_at = 0;
            if (covered >= 0) {
                lifted    = character->stash[covered].item;
                lifted_at = character->stash[covered].x + (character->stash[covered].y * STASH_W);
                Stash_Remove(character, covered);
            }
            Stash_Place(character, &inventory->held, x, y);
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding = false;
            if (lifted.code[0] != '\0') {
                GameInventory__Take(inventory, &lifted, ORIGIN_STASH, lifted_at);
            }
            return events | INVENTORY_EVENT_MOVED;
        }
        if (under >= 0) {
            const InventoryItem it = character->stash[under];
            Stash_Remove(character, under);
            GameInventory__Take(inventory, &it.item, ORIGIN_STASH, it.x + (it.y * STASH_W));
            return events | INVENTORY_EVENT_MOVED;
        }
        return events;
    }

    // The pages (carried items can go along, onto another page)
    int step = 0;
    if (left_click && GameInventory__PageArrow(mouse_x, mouse_y, &step)) {
        const int pages = Inventory_Pages();
        Inventory_SetPage((Inventory_Page() + step + pages) % pages);
        return events;
    }

    // The grid
    int cell_x;
    int cell_y;
    if (GameInventory__CellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
        const int under = Inventory_ItemAt(character, cell_x, cell_y);
        if (right_click && under >= 0 && inventory->shop != NULL && !inventory->holding) {
            const Item item = character->inventory[under].item; // right click sells, while trading
            Inventory_Remove(character, under);
            GameInventory__Sell(inventory, character, &item, message);
            return events | INVENTORY_EVENT_MOVED;
        }
        if (right_click && under >= 0 && !inventory->holding &&
            strcmp(character->inventory[under].item.code, CUBE_ITEM) == 0 && inventory->shop == NULL &&
            inventory->trade == NULL) {
            inventory->cube_open  = true; // the cube opens on the left
            inventory->stash_open = false;
            snprintf(inventory->sound, sizeof(inventory->sound), "cursor_convert_ready");
            return events | INVENTORY_EVENT_MOVED;
        }
        if (right_click && under >= 0 && !inventory->holding) {
            const InventoryItem *it = &character->inventory[under];
            if (GameInventory__StartIdentify(inventory, character, &it->item, false,
                                             it->x + (it->y * INVENTORY_W) + (it->page * INVENTORY_PAGE), message)) {
                return events;
            }
        }
        if (right_click && under >= 0) {
            if (GameInventory__Drink(&character->inventory[under].item, drunk)) {
                Inventory_Remove(character, under);
                return INVENTORY_EVENT_DRINK;
            }
            *message = "That can't be used yet.";
            return events;
        }
        if (!left_click) {
            return events;
        }
        // A gem, rune or jewel onto an item with an empty socket: in it goes
        if (inventory->holding && under >= 0 && Sockets_CanGoIn(&inventory->held) &&
            character->inventory[under].item.sockets > 0) {
            const char *why = NULL;
            if (Sockets_Insert(&character->inventory[under].item, &inventory->held, &why)) {
                snprintf(inventory->sound, sizeof(inventory->sound), "item_gem");
                inventory->holding = false;
                return events | INVENTORY_EVENT_MOVED;
            }
            *message = why;
            return events;
        }
        if (inventory->holding) {
            const ItemBase *base = Items_Find(inventory->held.code);
            int             x;
            int             y;
            GameInventory__DropCell(base, mouse_x, mouse_y, &x, &y);
            const int covered = Inventory_Covered(character, base, x, y);
            if (covered == -2) {
                return events; // off the grid, or on more than one item
            }
            if (inventory->still_worn) { // Modern: it comes off now, into the grid
                memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
                inventory->still_worn  = false;
                events                |= INVENTORY_EVENT_EQUIPMENT;
            }
            Item lifted;
            memset(&lifted, 0, sizeof(lifted));
            int lifted_at = 0;
            if (covered >= 0) {
                lifted    = character->inventory[covered].item;
                lifted_at = character->inventory[covered].x + (character->inventory[covered].y * INVENTORY_W) +
                            (Inventory_Page() * INVENTORY_PAGE);
                Inventory_Remove(character, covered);
            }
            Inventory_Place(character, &inventory->held, x, y);
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding = false;
            if (lifted.code[0] != '\0') {
                GameInventory__Take(inventory, &lifted, ORIGIN_GRID, lifted_at);
            }
            return events | INVENTORY_EVENT_MOVED;
        }
        if (under >= 0) {
            const InventoryItem it   = character->inventory[under];
            const ItemBase     *base = Items_Find(it.item.code);
            Inventory_Remove(character, under);
            // Shift + click: a potion or scroll straight into the belt
            if (shift && base != NULL && base->beltable && Inventory_AddToBelt(character, &it.item)) {
                return events | INVENTORY_EVENT_MOVED;
            }
            GameInventory__Take(inventory, &it.item, ORIGIN_GRID,
                                it.x + (it.y * INVENTORY_W) + (it.page * INVENTORY_PAGE));
            return events | INVENTORY_EVENT_MOVED;
        }
        return events;
    }

    // What's worn
    const int slot = GameInventory__SlotAt(mouse_x, mouse_y);
    if (slot >= 0 && left_click) {
        Item *worn = &character->equipped[slot];
        if (inventory->holding && !inventory->still_worn && Sockets_CanGoIn(&inventory->held) && worn->sockets > 0) {
            const char *why = NULL;
            if (Sockets_Insert(worn, &inventory->held, &why)) {
                snprintf(inventory->sound, sizeof(inventory->sound), "item_gem");
                inventory->holding = false;
                return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_EQUIPMENT;
            }
            *message = why;
            return events;
        }
        if (inventory->holding && inventory->still_worn && slot == inventory->origin_index) {
            inventory->holding    = false; // Modern: back where it never left
            inventory->still_worn = false;
            GameInventory__PutDown(inventory, &inventory->held);
            return events | INVENTORY_EVENT_MOVED;
        }
        Item       origin_item;
        const bool from_worn = inventory->holding && inventory->still_worn;
        if (from_worn) {
            // Modern: it comes off its old slot now (put back if it can't go in the new one)
            origin_item = character->equipped[inventory->origin_index];
            memset(&character->equipped[inventory->origin_index], 0, sizeof(Item));
        }
        if (inventory->holding) {
            // Taking off what's there first, so a shield can be swapped for a shield
            const Item before = *worn;
            memset(worn, 0, sizeof(Item));
            const char *why = NULL;
            if (!Inventory_CanEquip(character, &inventory->held, (EquipSlot)slot, &why)) {
                *worn    = before;
                *message = why;
                if (from_worn) {
                    character->equipped[inventory->origin_index] = origin_item;
                }
                return events;
            }
            if (slot == EQUIP_BELT) {
                // A smaller belt can't hold what's in the rows it doesn't have
                const ItemBase *belt = Items_Find(inventory->held.code);
                const int       size = belt != NULL && belt->belt_rows > 0 ? belt->belt_rows * 4 : 4;
                for (int b = size; b < BELT_MAX; b++) {
                    if (character->belt[b].code[0] != '\0') {
                        *worn    = before;
                        *message = "That belt is too small for what's in yours: empty its upper rows first.";
                        if (from_worn) {
                            character->equipped[inventory->origin_index] = origin_item;
                        }
                        return events;
                    }
                }
            }
            *worn = inventory->held;
            GameInventory__PutDown(inventory, &inventory->held);
            inventory->holding    = false;
            inventory->still_worn = false;
            if (before.code[0] != '\0') {
                GameInventory__Take(inventory, &before, ORIGIN_EQUIP, slot);
            }
            if (slot == EQUIP_BELT) {
                Inventory_SettleBelt(character);
            }
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_EQUIPMENT;
        }
        if (worn->code[0] != '\0') {
            if (slot == EQUIP_BELT) {
                // The potions in the rows a belt holds above the first stay only if another belt holds them
                for (int b = 4; b < BELT_MAX; b++) {
                    if (character->belt[b].code[0] != '\0') {
                        *message = "Empty the belt's upper rows first.";
                        return events;
                    }
                }
            }
            GameInventory__Take(inventory, worn, ORIGIN_EQUIP, slot);
            if (inventory->modern) {
                inventory->still_worn = true; // Modern: worn until it's put somewhere else
                return events | INVENTORY_EVENT_MOVED;
            }
            memset(worn, 0, sizeof(Item));
            return events | INVENTORY_EVENT_MOVED | INVENTORY_EVENT_EQUIPMENT;
        }
    }
    return events;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing

// The hireling's screen: what they wear, and their numbers in the boxes below (name and level, life, then two columns)
static void GameInventory__HireText(GameInventory *inventory, const char *text, const int x, const int y,
                                    const label_align_t align) {
    Label_SetAlignment(inventory->text, align, LABEL_ALIGN_CENTER);
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, x, y);
}

static void GameInventory__DrawHireling(GameInventory *inventory, const Character *character, const int mouse_x,
                                        const int mouse_y) {
    const HirelingView *view = inventory->hire;
    Sprite_DrawMulti(inventory->hire_panel, 0, 0, 0, 2, 2);
    const int over = GameInventory__HireSlotAt(mouse_x, mouse_y);
    for (int s = 0; s < HIRE_SLOTS; s++) {
        const Item *worn = &character->hireling_items[s];
        if (inventory->holding && s == over) {
            const char *why  = NULL;
            const bool  fits = Hirelings_CanWear(view->row, view->level, &inventory->held, s, &why);
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, fits ? 30 : 140, fits ? 100 : 0, 30, 110);
            SDL_RenderFillRect(sdl_renderer, &hire_rects[s]);
        }
        if (worn->code[0] != '\0') {
            GameInventory__DrawItem(inventory, worn, &hire_rects[s], true);
        }
    }
    char text[96];
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    GameInventory__HireText(inventory, view->name, 80, 207, LABEL_ALIGN_CENTER);
    snprintf(text, sizeof(text), "Level %d", view->level);
    GameInventory__HireText(inventory, text, 238, 207, LABEL_ALIGN_CENTER);
    snprintf(text, sizeof(text), "%s    Life: %d / %d", view->title, view->hp, view->max_hp);
    GameInventory__HireText(inventory, text, 160, 239, LABEL_ALIGN_CENTER);
    const struct {
        const char *label;
        int         value;
        int         value2; // (damage: to)
    } rows[8] = {
        {"Strength",       view->strength,   -1              },
        {"Dexterity",      view->dexterity,  -1              },
        {"Damage",         view->damage_min, view->damage_max},
        {"Defense",        view->defense,    -1              },
        {"Fire Resist",    view->resist,     -1              },
        {"Cold Resist",    view->resist,     -1              },
        {"Lightning Res.", view->resist,     -1              },
        {"Poison Resist",  view->resist,     -1              },
    };
    for (int r = 0; r < 8; r++) {
        const int col = r / 4;
        const int y   = 273 + ((r % 4) * 24);
        GameInventory__HireText(inventory, rows[r].label, 10 + (col * 158), y, LABEL_ALIGN_BEGIN);
        if (rows[r].value2 >= 0) {
            snprintf(text, sizeof(text), "%d-%d", rows[r].value, rows[r].value2);
        } else {
            snprintf(text, sizeof(text), "%d%s", rows[r].value, r >= 4 ? "%" : "");
        }
        GameInventory__HireText(inventory, text, 132 + (col * 158), y, LABEL_ALIGN_CENTER);
    }
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    const bool over_close = GameInventory__InBox(mouse_x, mouse_y, HIRE_CLOSE_X, HIRE_CLOSE_Y);
    Sprite_Draw(inventory->shop_buttons, over_close && left_down ? 11 : 10, HIRE_CLOSE_X,
                HIRE_CLOSE_Y + SHOP_BUTTON_SZ);
}

// The cube on the left: its inside, Transmute and close
static void GameInventory__DrawCube(GameInventory *inventory, const Character *character, const int mouse_x,
                                    const int mouse_y) {
    Sprite_DrawMulti(inventory->cube_panel, 0, 0, 0, 2, 2);
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        const int       left = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
        const int       top  = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
        const int       x    = left >= CUBE_GRID_X ? (left - CUBE_GRID_X) / CELL : -1;
        const int       y    = top >= CUBE_GRID_Y ? (top - CUBE_GRID_Y) / CELL : -1;
        if (Cube_Covered(character, base, x, y) != -2 &&
            GameInventory__CubeCellAt(mouse_x, mouse_y, &(int){0}, &(int){0})) {
            const SDL_Rect area = {CUBE_GRID_X + (x * CELL), CUBE_GRID_Y + (y * CELL), base->width * CELL,
                                   base->height * CELL};
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, 30, 100, 30, 110);
            SDL_RenderFillRect(sdl_renderer, &area);
        }
    }
    for (int i = 0; i < character->cube_count; i++) {
        const InventoryItem *it   = &character->cube[i];
        const ItemBase      *base = Items_Find(it->item.code);
        const SDL_Rect       rect = GameInventory__CubeRect(it, base);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 60, 90);
        SDL_RenderFillRect(sdl_renderer, &rect);
        GameInventory__DrawItem(inventory, &it->item, &rect, true);
    }
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    const bool over_transmute = GameInventory__InBox(mouse_x, mouse_y, CUBE_TRANSMUTE_X, CUBE_TRANSMUTE_Y);
    const bool over_close     = GameInventory__InBox(mouse_x, mouse_y, CUBE_CLOSE_X, CUBE_CLOSE_Y);
    Sprite_Draw(inventory->shop_buttons, over_transmute && left_down ? 1 : 0, CUBE_TRANSMUTE_X,
                CUBE_TRANSMUTE_Y + SHOP_BUTTON_SZ);
    Sprite_Draw(inventory->shop_buttons, over_close && left_down ? 11 : 10, CUBE_CLOSE_X,
                CUBE_CLOSE_Y + SHOP_BUTTON_SZ);
    if (over_transmute || over_close) {
        Label_SetAlignment(inventory->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_SetText(inventory->text, over_transmute ? "Transmute" : "Close");
        Label_Draw(inventory->text, (over_transmute ? CUBE_TRANSMUTE_X : CUBE_CLOSE_X) + (SHOP_BUTTON_SZ / 2),
                   (over_transmute ? CUBE_TRANSMUTE_Y : CUBE_CLOSE_Y) - 10);
    }
}

// The stash on the left: its items, its gold and the button to take some out
static void GameInventory__DrawStash(GameInventory *inventory, const Character *character, const int mouse_x,
                                     const int mouse_y) {
    Sprite_DrawMulti(character->expansion || inventory->bank_panel == NULL ? inventory->stash_panel
                                                                           : inventory->bank_panel,
                     0, 0, 0, 2, 2);
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        const int       left = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
        const int       top  = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
        const int       x    = left >= STASH_GRID_X ? (left - STASH_GRID_X) / CELL : -1;
        const int       y    = top >= stash_grid_y ? (top - stash_grid_y) / CELL : -1;
        if (Stash_Covered(character, base, x, y) != -2 &&
            GameInventory__StashCellAt(mouse_x, mouse_y, &(int){0}, &(int){0})) {
            const SDL_Rect area = {STASH_GRID_X + (x * CELL), stash_grid_y + (y * CELL), base->width * CELL,
                                   base->height * CELL};
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, 30, 100, 30, 110);
            SDL_RenderFillRect(sdl_renderer, &area);
        }
    }
    for (int i = 0; i < character->stash_count; i++) {
        const InventoryItem *it   = &character->stash[i];
        const ItemBase      *base = Items_Find(it->item.code);
        const SDL_Rect       rect = GameInventory__StashRect(it, base);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 60, 90);
        SDL_RenderFillRect(sdl_renderer, &rect);
        GameInventory__DrawItem(inventory, &it->item, &rect,
                                base == NULL || base->kind == ITEM_KIND_MISC ||
                                    Inventory_ItemMeetsRequirements(character, &it->item));
    }
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    Sprite_Draw(inventory->gold_button, GameInventory__OverStashGold(mouse_x, mouse_y) && left_down ? 1 : 0,
                STASH_GOLD_BUTTON_X, stash_gold_y + GOLD_BUTTON_H);
    char text[64];
    Label_SetAlignment(inventory->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    snprintf(text, sizeof(text), "Gold in Stash: %d", character->stash_gold);
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, STASH_GOLD_BUTTON_X + GOLD_BUTTON_W + 8, stash_gold_y + 10);
    snprintf(text, sizeof(text), "Gold Max: %d", Stash_GoldLimit(character));
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, STASH_GOLD_BUTTON_X + GOLD_BUTTON_W + 8, stash_gold_y + 34);
}

// The merchant's store on the left
static void GameInventory__DrawShop(GameInventory *inventory, const Character *character, const int mouse_x,
                                    const int mouse_y) {
    Sprite_DrawMulti(inventory->shop_panel, 0, 0, 0, 2, 2);
    for (int t = 0; t < SHOP_TABS; t++) {
        Sprite_Draw(inventory->shop_tabs, (uint8_t)(t == inventory->shop_tab ? t : t + 4), t * SHOP_TAB_W, SHOP_TAB_H);
        Label_SetText(inventory->text, Shop_TabName(t));
        Label_SetAlignment(inventory->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Label_SetColor(inventory->text, t == inventory->shop_tab ? 0xFF : 0xA0, t == inventory->shop_tab ? 0xFF : 0xA0,
                       t == inventory->shop_tab ? 0xFF : 0xA0, 0xFF);
        Label_Draw(inventory->text, (t * SHOP_TAB_W) + (SHOP_TAB_W / 2), SHOP_TAB_H / 2 + 2);
    }
    for (int i = 0; i < inventory->shop->count; i++) {
        const InventoryItem *it = &inventory->shop->items[i];
        if (it->page != inventory->shop_tab) {
            continue;
        }
        const ItemBase *base = Items_Find(it->item.code);
        const SDL_Rect  rect = GameInventory__ShopRect(it, base);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 60, 90);
        SDL_RenderFillRect(sdl_renderer, &rect);
        GameInventory__DrawItem(inventory, &it->item, &rect,
                                base == NULL || base->kind == ITEM_KIND_MISC ||
                                    Inventory_ItemMeetsRequirements(character, &it->item));
    }
    char text[48];
    snprintf(text, sizeof(text), "Gold: %d", character->gold);
    Label_SetText(inventory->text, text);
    Label_SetAlignment(inventory->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    Label_Draw(inventory->text, 22, 367);
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    Sprite_Draw(inventory->shop_buttons, GameInventory__OverShopClose(mouse_x, mouse_y) && left_down ? 11 : 10,
                SHOP_CLOSE_X, SHOP_CLOSE_Y + SHOP_BUTTON_SZ);
}

// Trading: the panel, both offers, the gold, who has accepted
static void GameInventory__DrawTradeItems(GameInventory *inventory, const Character *character,
                                          const InventoryItem *items, const int count, const int top) {
    for (int i = 0; i < count; i++) {
        const ItemBase *base = Items_Find(items[i].item.code);
        const SDL_Rect  rect = {TRADE_GRID_X + (items[i].x * CELL), top + (items[i].y * CELL),
                                (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 60, 90);
        SDL_RenderFillRect(sdl_renderer, &rect);
        GameInventory__DrawItem(inventory, &items[i].item, &rect,
                                base == NULL || base->kind == ITEM_KIND_MISC ||
                                    Inventory_ItemMeetsRequirements(character, &items[i].item));
    }
}

static void GameInventory__DrawCheck(const int x, const int y, const bool on) {
    if (!on) {
        return;
    }
    SDL_SetRenderDrawColor(sdl_renderer, 60, 230, 60, 255);
    for (int t = 0; t < 3; t++) { // a tick
        SDL_RenderDrawLine(sdl_renderer, x + 6, y + 16 + t, x + 13, y + 23 + t);
        SDL_RenderDrawLine(sdl_renderer, x + 13, y + 23 + t, x + 26, y + 8 + t);
    }
}

static void GameInventory__DrawTrade(GameInventory *inventory, const Character *character, const int mouse_x,
                                     const int mouse_y) {
    const TradeView *trade = inventory->trade;
    Sprite_DrawMulti(inventory->trade_panel, 0, 0, 0, 2, 2);
    GameInventory__DrawTradeItems(inventory, character, trade->theirs, trade->their_count, TRADE_THEIR_Y);
    GameInventory__DrawTradeItems(inventory, character, trade->mine, trade->mine_count, TRADE_MY_Y);
    char text[48];
    Label_SetAlignment(inventory->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    Label_SetText(inventory->text, trade->their_name);
    Label_Draw(inventory->text, 24, 19);
    snprintf(text, sizeof(text), "Gold: %d", trade->their_gold);
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, 162, 19);
    Label_SetText(inventory->text, character->name);
    Label_Draw(inventory->text, 24, 235);
    snprintf(text, sizeof(text), "Gold: %d", trade->my_gold);
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, 162, 235);
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    Sprite_Draw(inventory->shop_buttons,
                (uint8_t)(16 + (GameInventory__InBox(mouse_x, mouse_y, TRADE_ACCEPT_X, TRADE_BUTTONS_Y) && left_down)),
                TRADE_ACCEPT_X, TRADE_BUTTONS_Y + SHOP_BUTTON_SZ);
    Sprite_Draw(inventory->shop_buttons,
                (uint8_t)(10 + (GameInventory__InBox(mouse_x, mouse_y, TRADE_CLOSE_X, TRADE_BUTTONS_Y) && left_down)),
                TRADE_CLOSE_X, TRADE_BUTTONS_Y + SHOP_BUTTON_SZ);
    GameInventory__DrawCheck(TRADE_THEIR_OK_X, TRADE_THEIR_OK_Y, trade->their_accept);
    if (trade->my_accept) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 30, 120, 30, 120);
        const SDL_Rect box = {TRADE_ACCEPT_X, TRADE_BUTTONS_Y, SHOP_BUTTON_SZ, SHOP_BUTTON_SZ};
        SDL_RenderFillRect(sdl_renderer, &box);
    }
}

void GameInventory_Draw(GameInventory *inventory, const Character *character, const int mouse_x, const int mouse_y) {
    GameInventory__StashLayout(character);
    if (inventory->trade != NULL) {
        GameInventory__DrawTrade(inventory, character, mouse_x, mouse_y);
    } else if (inventory->shop != NULL) {
        GameInventory__DrawShop(inventory, character, mouse_x, mouse_y);
    } else if (inventory->stash_open) {
        GameInventory__DrawStash(inventory, character, mouse_x, mouse_y);
    } else if (inventory->cube_open) {
        GameInventory__DrawCube(inventory, character, mouse_x, mouse_y);
    } else if (inventory->hire != NULL) {
        GameInventory__DrawHireling(inventory, character, mouse_x, mouse_y);
    }
    Sprite_DrawMulti(inventory->panel, 4, GAME_PANEL_X, 0, 2, 2);

    // Worn items; a soft highlight where the carried item would go
    const int over_slot = GameInventory__SlotAt(mouse_x, mouse_y);
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item *worn = &character->equipped[s];
        if (inventory->holding && s == over_slot) {
            const bool fits = Inventory_CanEquip(character, &inventory->held, (EquipSlot)s, NULL) ||
                              (worn->code[0] != '\0' && Items_Find(inventory->held.code) != NULL &&
                               Items_Find(inventory->held.code)->slots[s]);
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, fits ? 30 : 140, fits ? 100 : 20, fits ? 30 : 20, 120);
            SDL_RenderFillRect(sdl_renderer, &slot_rects[s]);
        }
        if (worn->code[0] != '\0') {
            const ItemBase *base = Items_Find(worn->code);
            (void)base;
            GameInventory__DrawItem(inventory, worn, &slot_rects[s], Inventory_ItemMeetsRequirements(character, worn));
            if (inventory->holding && inventory->still_worn && s == inventory->origin_index) {
                SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND); // Modern: still worn, on the cursor
                SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 185);
                SDL_RenderFillRect(sdl_renderer, &slot_rects[s]);
            }
        }
    }

    // Where the carried item would land in the grid
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        int             x;
        int             y;
        GameInventory__DropCell(base, mouse_x, mouse_y, &x, &y);
        const int covered = Inventory_Covered(character, base, x, y);
        if (covered != -2 && GameInventory__CellAt(mouse_x, mouse_y, &(int){0}, &(int){0})) {
            const SDL_Rect area = {GRID_X + (x * CELL), GRID_Y + (y * CELL), base->width * CELL, base->height * CELL};
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, 30, 100, 30, 110);
            SDL_RenderFillRect(sdl_renderer, &area);
        }
    }

    // The grid's items
    for (int i = 0; i < character->inventory_count; i++) {
        const InventoryItem *it = &character->inventory[i];
        if (it->page != Inventory_Page()) {
            continue;
        }
        const ItemBase *base = Items_Find(it->item.code);
        const SDL_Rect  rect = GameInventory__GridRect(it, base);
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 60, 90); // the game's blue behind items
        SDL_RenderFillRect(sdl_renderer, &rect);
        GameInventory__DrawItem(inventory, &it->item, &rect,
                                base == NULL || base->kind == ITEM_KIND_MISC ||
                                    Inventory_ItemMeetsRequirements(character, &it->item));
    }

    // Gold, and its button
    const bool over_gold = mouse_x >= GOLD_BUTTON_X && mouse_x < GOLD_BUTTON_X + GOLD_BUTTON_W &&
                           mouse_y >= GOLD_BUTTON_Y && mouse_y < GOLD_BUTTON_Y + GOLD_BUTTON_H;
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    const bool pressed = over_gold && left_down;
    Sprite_Draw(inventory->gold_button, pressed ? 1 : 0, GOLD_BUTTON_X, GOLD_BUTTON_Y + GOLD_BUTTON_H); // (bottom)
    char text[32];
    snprintf(text, sizeof(text), "%d", character->gold);
    Label_SetText(inventory->text, text);
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    Label_SetAlignment(inventory->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_Draw(inventory->text, GAME_PANEL_X + 140, 400);

    // The pages: < 2 / 5 >
    if (Inventory_Pages() > 1) {
        for (int side = 0; side < 2; side++) {
            const int      x     = side == 0 ? PAGE_LEFT_X : PAGE_RIGHT_X;
            const SDL_Rect box   = {x, PAGE_Y, PAGE_ARROW_W, PAGE_H};
            int            step  = 0;
            const bool     hover = GameInventory__PageArrow(mouse_x, mouse_y, &step) && (step < 0) == (side == 0);
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, hover ? 70 : 20, hover ? 60 : 18, hover ? 35 : 15, 230);
            SDL_RenderFillRect(sdl_renderer, &box);
            SDL_SetRenderDrawColor(sdl_renderer, 199, 179, 119, 255);
            SDL_RenderDrawRect(sdl_renderer, &box);
            const int cx = x + (PAGE_ARROW_W / 2);
            const int cy = PAGE_Y + (PAGE_H / 2);
            for (int i = 0; i < 6; i++) { // a triangle pointing left or right
                const int reach = side == 0 ? cx - 3 + i : cx + 3 - i;
                SDL_RenderDrawLine(sdl_renderer, reach, cy - i, reach, cy + i);
            }
        }
        snprintf(text, sizeof(text), "%d / %d", Inventory_Page() + 1, Inventory_Pages());
        Label_SetText(inventory->small, text);
        Label_SetColor(inventory->small, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_SetAlignment(inventory->small, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Label_Draw(inventory->small, (PAGE_LEFT_X + PAGE_ARROW_W + PAGE_RIGHT_X) / 2, PAGE_Y + (PAGE_H / 2));
        Label_SetAlignment(inventory->small, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    }
}

void GameInventory_DrawBelt(GameInventory *inventory, const Character *character) {
    for (int s = 0; s < 4; s++) {
        const SDL_Rect rect = GameInventory__BeltRect(s);
        const Item    *item = &character->belt[s];
        if (item->code[0] != '\0') {
            GameInventory__DrawItem(inventory, item, &rect, true);
        }
        char key[4];
        snprintf(key, sizeof(key), "%d", s + 1);
        Label_SetText(inventory->small, key);
        Label_SetColor(inventory->small, 0xC0, 0xC0, 0xC0, 0xFF);
        Label_SetAlignment(inventory->small, LABEL_ALIGN_END, LABEL_ALIGN_END);
        Label_Draw(inventory->small, rect.x + rect.w - 1, rect.y + rect.h - 1);
    }
}

static void GameInventory__AddLine(char lines[][96], uint8_t colors[][3], int *count, const uint8_t r, const uint8_t g,
                                   const uint8_t b, const char *text) {
    if (*count >= TOOLTIP_LINES) {
        return;
    }
    snprintf(lines[*count], 96, "%s", text);
    colors[*count][0] = r;
    colors[*count][1] = g;
    colors[*count][2] = b;
    (*count)++;
}

// The game's item description: name, then damage or defense, then requirements (red when not met)
static void GameInventory__Tooltip(GameInventory *inventory, const Character *character, const Item *item, const int x,
                                   const int y) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return;
    }
    char    lines[TOOLTIP_LINES][96];
    uint8_t colors[TOOLTIP_LINES][3];
    int     count = 0;
    char    text[96];
    // The name in the quality's colour: magic blue, rare yellow (and the base below it), unique gold
    uint8_t qr;
    uint8_t qg;
    uint8_t qb;
    Affixes_QualityColor(item, &qr, &qg, &qb);
    char name1[96];
    char name2[96];
    Affixes_Name(item, name1, sizeof(name1), name2, sizeof(name2));
    GameInventory__AddLine(lines, colors, &count, qr, qg, qb, name1);
    if (name2[0] != '\0') {
        GameInventory__AddLine(lines, colors, &count, qr, qg, qb, name2);
    }
    // Damage and defense with the item's own magic in (blue when it changes them, like the game)
    const int  dmg_pct  = Affixes_ItemTotal(item, MOD_DAMAGE_PCT);
    const int  dmg_min  = Affixes_ItemTotal(item, MOD_DAMAGE_MIN);
    const int  dmg_max  = Affixes_ItemTotal(item, MOD_DAMAGE_MAX);
    const bool dmg_blue = dmg_pct != 0 || dmg_min != 0 || dmg_max != 0;
    if (base->kind == ITEM_KIND_WEAPON) {
        if (base->max_damage > 0) {
            snprintf(text, sizeof(text), "One-Hand Damage: %d to %d",
                     base->min_damage + (base->min_damage * dmg_pct / 100) + dmg_min,
                     base->max_damage + (base->max_damage * dmg_pct / 100) + dmg_max);
            GameInventory__AddLine(lines, colors, &count, dmg_blue ? 0x69 : 0xFF, dmg_blue ? 0x69 : 0xFF, 0xFF, text);
        }
        if (base->two_hand_max > 0) {
            snprintf(text, sizeof(text), "Two-Hand Damage: %d to %d",
                     base->two_hand_min + (base->two_hand_min * dmg_pct / 100) + dmg_min,
                     base->two_hand_max + (base->two_hand_max * dmg_pct / 100) + dmg_max);
            GameInventory__AddLine(lines, colors, &count, dmg_blue ? 0x69 : 0xFF, dmg_blue ? 0x69 : 0xFF, 0xFF, text);
        }
    } else if (base->kind == ITEM_KIND_ARMOR) {
        const int defense = Character_ItemDefense(item) + Affixes_ItemTotal(item, MOD_DEFENSE);
        snprintf(text, sizeof(text), "Defense: %d", defense);
        const bool blue = defense != item->defense;
        GameInventory__AddLine(lines, colors, &count, blue ? 0x69 : 0xFF, blue ? 0x69 : 0xFF, 0xFF, text);
        if (base->block > 0) {
            snprintf(text, sizeof(text), "Chance to Block: %d%%", base->block);
            GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
        }
        if (base->belt_rows > 0) {
            snprintf(text, sizeof(text), "Belt Size: %d slots", base->belt_rows * 4);
            GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
        }
    } else if (base->life > 0 || base->mana > 0) {
        snprintf(text, sizeof(text), "Replenishes %d %s over %d seconds", base->life > 0 ? base->life : base->mana,
                 base->life > 0 ? "Life" : "Mana", base->seconds);
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
    } else if (base->rejuvenate_pct > 0) {
        snprintf(text, sizeof(text), "Replenishes %d%% of Life and Mana", base->rejuvenate_pct);
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
    }
    if (base->stackable && item->quantity > 1) {
        snprintf(text, sizeof(text), "Quantity: %d", item->quantity);
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
    }
    const struct {
        const char *name;
        int         need;
        int         have;
    } requirements[3] = {
        {"Required Strength", Inventory_Requirement(item, STAT_STRENGTH), Character_Stat(character, STAT_STRENGTH)},
        {"Required Dexterity", Inventory_Requirement(item, STAT_DEXTERITY), Character_Stat(character, STAT_DEXTERITY)},
        {"Required Level", Inventory_Requirement(item, -1), character->level},
    };
    for (int r = 0; r < 3; r++) {
        if (requirements[r].need > 0) {
            const bool met = requirements[r].have >= requirements[r].need;
            snprintf(text, sizeof(text), "%s: %d", requirements[r].name, requirements[r].need);
            GameInventory__AddLine(lines, colors, &count, 0xFF, met ? 0xFF : 0x40, met ? 0xFF : 0x40, text);
        }
    }
    if (item->max_durability > 0) {
        snprintf(text, sizeof(text), "Durability: %d of %d", item->durability, item->max_durability);
        const bool low = item->durability * 10 <= item->max_durability;
        GameInventory__AddLine(lines, colors, &count, 0xFF, low ? 0x40 : 0xFF, low ? 0x40 : 0xFF, text);
    }
    if (item->unidentified) {
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0x40, 0x40, "Unidentified");
    }
    // The magic, in blue
    for (int m = 0; m < item->mod_count && m < ITEM_MODS_MAX && !item->unidentified; m++) {
        Affixes_ModText(&item->mods[m], text, sizeof(text));
        GameInventory__AddLine(lines, colors, &count, 0x69, 0x69, 0xFF, text);
    }
    const char *set = item->unidentified ? NULL : Affixes_SetName(item);
    if (set != NULL) {
        snprintf(text, sizeof(text), "%s (%d worn)", set, Affixes_SetPiecesWorn(character, set));
        GameInventory__AddLine(lines, colors, &count, 0xC7, 0xB3, 0x77, text);
        GameInventory__AddLine(lines, colors, &count, 0x00, 0xFF, 0x00, "More magic for each piece of it worn");
    }
    if (item->sockets > 0) {
        snprintf(text, sizeof(text), "Socketed (%d)", item->sockets);
        GameInventory__AddLine(lines, colors, &count, 0x69, 0x69, 0xFF, text);
    }
    if (Sockets_CanGoIn(item)) {
        GameInventory__AddLine(lines, colors, &count, 0xA0, 0xA0, 0xA0, "Put it on an item with sockets");
    }
    if (inventory->tip_extra[0] != '\0') {
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, inventory->tip_extra);
    } else if (Items_IsPotion(base)) {
        GameInventory__AddLine(lines, colors, &count, 0xA0, 0xA0, 0xA0, "Right click to drink");
    } else if (GameInventory__Identifies(item)) {
        GameInventory__AddLine(lines, colors, &count, 0xA0, 0xA0, 0xA0, "Right click to identify an item");
    } else if (strcmp(item->code, CUBE_ITEM) == 0) {
        GameInventory__AddLine(lines, colors, &count, 0xA0, 0xA0, 0xA0, "Right click to open");
    }

    // A dark box, centred above the item, kept on the screen
    int width = 0;
    for (int l = 0; l < count; l++) {
        int w = 0;
        Label_SetText(inventory->text, lines[l]);
        Label_GetSize(inventory->text, &w, NULL);
        width = w > width ? w : width;
    }
    const int line_h   = 16;
    int       box_x    = x - (width / 2) - 6;
    int       box_y    = y - (count * line_h) - 10;
    box_x              = box_x < 2 ? 2 : (box_x + width + 12 > 798 ? 798 - width - 12 : box_x);
    box_y              = box_y < 2 ? 2 : box_y;
    const SDL_Rect box = {box_x, box_y, width + 12, (count * line_h) + 8};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 215);
    SDL_RenderFillRect(sdl_renderer, &box);
    Label_SetAlignment(inventory->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int l = 0; l < count; l++) {
        Label_SetText(inventory->text, lines[l]);
        Label_SetColor(inventory->text, colors[l][0], colors[l][1], colors[l][2], 0xFF);
        Label_Draw(inventory->text, box.x + (box.w / 2), box.y + 4 + (l * line_h) + (line_h / 2));
    }
}

// The sockets over an item's picture when the mouse is on it (anchor: the middle of its top), with what's in them
static void GameInventory__DrawSockets(GameInventory *inventory, const Item *item, const int anchor_x,
                                       const int anchor_y) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || item->sockets == 0 || inventory->socket == NULL) {
        return;
    }
    const int count = item->sockets;
    const int cols  = base->width >= 2 && count >= 4 ? 2 : 1;
    const int rows  = count == 5 ? 3 : (count + cols - 1) / cols;
    const int top   = anchor_y + ((base->height * CELL) - (rows * CELL)) / 2;
    int       n     = 0;
    for (int r = 0; r < rows && n < count; r++) {
        const int in_row = count == 5 && r == 1 ? 1 : cols;
        for (int c = 0; c < in_row && n < count; c++, n++) {
            const int x = anchor_x - ((in_row * CELL) / 2) + (c * CELL);
            const int y = top + (r * CELL);
            Sprite_Draw(inventory->socket, 0, x, y + CELL - 1); // (drawn from its bottom)
            if (n < item->socketed_count) {
                Item inside;
                memset(&inside, 0, sizeof(inside));
                snprintf(inside.code, sizeof(inside.code), "%s", item->socketed[n]);
                const SDL_Rect box = {x, y, CELL, CELL};
                GameInventory__DrawItem(inventory, &inside, &box, true);
            }
        }
    }
}

void GameInventory_DrawOverlay(GameInventory *inventory, const Character *character, const bool open, const int mouse_x,
                               const int mouse_y) {
    if (inventory->identifying) {
        // The identify pointer: a question mark by the mouse
        Label_SetText(inventory->text, "?");
        Label_SetColor(inventory->text, 0xFF, 0xD0, 0x40, 0xFF);
        Label_SetAlignment(inventory->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_BEGIN);
        Label_Draw(inventory->text, mouse_x + 14, mouse_y + 4);
    }
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        const SDL_Rect  box  = {mouse_x - ((base != NULL ? base->width : 1) * CELL / 2),
                                mouse_y - ((base != NULL ? base->height : 1) * CELL / 2),
                                (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
        GameInventory__DrawItem(inventory, &inventory->held, &box, true);
        return;
    }
    // What's under the mouse
    const Item *item     = NULL;
    int         anchor_x = mouse_x;
    int         anchor_y = mouse_y;
    const int   belt     = GameInventory__BeltSlotAt(mouse_x, mouse_y);
    if (belt >= 0 && character->belt[belt].code[0] != '\0') {
        item     = &character->belt[belt];
        anchor_y = BELT_Y - 4;
    } else if (open) {
        int              cell_x;
        int              cell_y;
        const TradeView *trade       = inventory->trade;
        int              trade_index = -1;
        bool             theirs      = false;
        if (trade != NULL && GameInventory__TradeCellAt(mouse_x, mouse_y, TRADE_THEIR_Y, &cell_x, &cell_y)) {
            trade_index = GameInventory_TradeAt(trade->theirs, trade->their_count, cell_x, cell_y);
            theirs      = true;
        } else if (trade != NULL && GameInventory__TradeCellAt(mouse_x, mouse_y, TRADE_MY_Y, &cell_x, &cell_y)) {
            trade_index = GameInventory_TradeAt(trade->mine, trade->mine_count, cell_x, cell_y);
        }
        if (trade_index >= 0) {
            const InventoryItem *it   = theirs ? &trade->theirs[trade_index] : &trade->mine[trade_index];
            const ItemBase      *base = Items_Find(it->item.code);
            item                      = &it->item;
            anchor_x                  = TRADE_GRID_X + (it->x * CELL) + ((base != NULL ? base->width : 1) * CELL / 2);
            anchor_y                  = (theirs ? TRADE_THEIR_Y : TRADE_MY_Y) + (it->y * CELL);
        } else if (inventory->shop != NULL && GameInventory__ShopCellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
            const int under = Shop_ItemAt(inventory->shop, inventory->shop_tab, cell_x, cell_y);
            if (under >= 0) {
                const InventoryItem *it   = &inventory->shop->items[under];
                const SDL_Rect       rect = GameInventory__ShopRect(it, Items_Find(it->item.code));
                item                      = &it->item;
                anchor_x                  = rect.x + (rect.w / 2);
                anchor_y                  = rect.y;
                snprintf(inventory->tip_extra, sizeof(inventory->tip_extra), "Cost: %d",
                         Shop_Price(inventory->shop, item));
            }
        } else if (inventory->hire != NULL && GameInventory__HireSlotAt(mouse_x, mouse_y) >= 0 &&
                   character->hireling_items[GameInventory__HireSlotAt(mouse_x, mouse_y)].code[0] != '\0') {
            const int slot = GameInventory__HireSlotAt(mouse_x, mouse_y);
            item           = &character->hireling_items[slot];
            anchor_x       = hire_rects[slot].x + (hire_rects[slot].w / 2);
            anchor_y       = hire_rects[slot].y;
        } else if (inventory->cube_open && GameInventory__CubeCellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
            const int under = Cube_ItemAt(character, cell_x, cell_y);
            if (under >= 0) {
                const SDL_Rect rect =
                    GameInventory__CubeRect(&character->cube[under], Items_Find(character->cube[under].item.code));
                item     = &character->cube[under].item;
                anchor_x = rect.x + (rect.w / 2);
                anchor_y = rect.y;
            }
        } else if (inventory->stash_open && GameInventory__StashCellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
            const int under = Stash_ItemAt(character, cell_x, cell_y);
            if (under >= 0) {
                const SDL_Rect rect =
                    GameInventory__StashRect(&character->stash[under], Items_Find(character->stash[under].item.code));
                item     = &character->stash[under].item;
                anchor_x = rect.x + (rect.w / 2);
                anchor_y = rect.y;
            }
        } else if (GameInventory__CellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
            const int under = Inventory_ItemAt(character, cell_x, cell_y);
            if (under >= 0) {
                const SDL_Rect rect = GameInventory__GridRect(&character->inventory[under],
                                                              Items_Find(character->inventory[under].item.code));
                item                = &character->inventory[under].item;
                anchor_x            = rect.x + (rect.w / 2);
                anchor_y            = rect.y;
            }
        } else {
            const int slot = GameInventory__SlotAt(mouse_x, mouse_y);
            if (slot >= 0 && character->equipped[slot].code[0] != '\0') {
                item     = &character->equipped[slot];
                anchor_x = slot_rects[slot].x + (slot_rects[slot].w / 2);
                anchor_y = slot_rects[slot].y;
            }
        }
    }
    if (item != NULL) {
        if (inventory->shop != NULL && inventory->tip_extra[0] == '\0' && belt < 0) {
            snprintf(inventory->tip_extra, sizeof(inventory->tip_extra), "Sell value: %d",
                     Shop_SellPrice(inventory->shop, item, inventory->difficulty));
        }
        GameInventory__DrawSockets(inventory, item, anchor_x, anchor_y);
        GameInventory__Tooltip(inventory, character, item, anchor_x, anchor_y);
    }
    inventory->tip_extra[0] = '\0';
}
