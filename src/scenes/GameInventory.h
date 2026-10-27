#ifndef ABYSS_GAME_INVENTORY_H
#define ABYSS_GAME_INVENTORY_H

#include "../game/Character.h"
#include "../game/Items.h"
#include <stdbool.h>

// Single Player's inventory screen, the game's own panel (invchar6.dc6) on the right with its layout from
// inventory.txt: the worn items around the figure's outline, the 10 x 4 grid, gold. Items are picked up with a click,
// carried on the cursor and put down with another click (swapping with what's there), like the game's. The belt is on
// the control panel (GameInventory_DrawBelt).

#define GAME_PANEL_X 480 // the right-hand panels (inventory, skill tree): 320 x 432 in the top right corner
#define GAME_PANEL_W 320
#define GAME_PANEL_H 432

typedef struct GameInventory GameInventory;

typedef enum GameInventoryEvent {
    INVENTORY_EVENT_NONE       = 0,
    INVENTORY_EVENT_EQUIPMENT  = 1,   // what's worn changed: damage and defense need working out again
    INVENTORY_EVENT_DRINK      = 2,   // a potion was drunk: *drunk says which
    INVENTORY_EVENT_MOVED      = 4,   // something was picked up or put down (a sound)
    INVENTORY_EVENT_GOLD       = 8,   // the gold button: drop some, or at the stash put some in (the scene asks)
    INVENTORY_EVENT_STASH_GOLD = 16,  // the stash's gold button: take some out
    INVENTORY_EVENT_SHOP_CLOSE = 32,  // the store's close button
    INVENTORY_EVENT_TRADE      = 64,  // trading: your offer changed
    INVENTORY_EVENT_TRADE_OK   = 128, // trading: the accept button
    INVENTORY_EVENT_TRADE_GOLD = 256, // trading: your gold box (how much to offer)
    INVENTORY_EVENT_TRADE_END  = 512,  // trading: the close button
    INVENTORY_EVENT_HIRELING   = 1024, // what the hireling wears changed (their numbers need working out again)
    INVENTORY_EVENT_HIRE_CLOSE = 2048, // the hireling's screen's close button
} GameInventoryEvent;

// Your hireling's screen, on the left (NpcInv.dc6): what they wear (helm, armor, weapon, the sorcerers' shield) and
// their numbers, which the scene keeps up to date while it's open
typedef struct HirelingView {
    char name[48];
    char title[40]; // "Rogue Scout"
    int  row;       // hireling.txt (Hirelings.h)
    int  level;
    int  hp;
    int  max_hp;
    int  damage_min;
    int  damage_max;
    int  defense;
    int  resist;
    int  strength;
    int  dexterity;
} HirelingView;
void GameInventory_SetHireling(GameInventory *inventory, const HirelingView *view); // NULL: closed
bool GameInventory_HirelingOpen(const GameInventory *inventory);

GameInventory *GameInventory_Create(void);
void           GameInventory_Destroy(GameInventory **inventory);

// Clicks (the moment the button goes down); open: whether the panel is showing (the belt works either way).
// Returns GameInventoryEvent flags; *message is something to tell the player, or NULL.
int GameInventory_Update(GameInventory *inventory, Character *character, bool open, int mouse_x, int mouse_y,
                         bool left_click, bool right_click, bool shift, Item *drunk, const char **message);
// Drinks what's in a belt slot (0..3, the bottom row), the way keys 1-4 do
bool GameInventory_DrinkBelt(Character *character, int slot, Item *drunk);

bool GameInventory_Holding(const GameInventory *inventory); // an item on the cursor
// Options > Gameplay > Picking up worn gear: Modern keeps it worn until it is put somewhere else
void GameInventory_SetModernPickup(GameInventory *inventory, bool modern);
// Where the control panel shows the belt's bottom row (its top-left; the Classic panel: 422, 568)
void        GameInventory_SetBeltPosition(int x, int y);
const char *GameInventory_Sound(const GameInventory *inventory); // the sound of what was last put down ("" none)
// The carried item back where it was (or anywhere it fits); false when there's no room (it stays on the cursor)
bool GameInventory_PutBack(GameInventory *inventory, Character *character);
// Lets go of the carried item, to put it on the ground: false when nothing's carried. Modern pick-up: a worn item
// comes off now (*equipment_changed)
bool GameInventory_Drop(GameInventory *inventory, Character *character, Item *item, bool *equipment_changed);
void GameInventory_Hold(GameInventory *inventory, const Item *item); // picked up off the ground, onto the cursor
// For saving: copy (of the character) as if the carried item were put back; the cursor keeps it
void GameInventory_PutBackCopy(const GameInventory *inventory, Character *copy);
bool GameInventory_IsOver(int mouse_x, int mouse_y); // over the panel
// The stash panel on the left (in town, at the stash): its grid and gold, items carried between it and the inventory
void GameInventory_SetStash(GameInventory *inventory, bool open);
// Whether the stash shows its Personal / Shared tabs (Single Player, expansion characters: the shared stash)
void GameInventory_SetSharedStash(GameInventory *inventory, bool allowed);
// How many pages the stash and the shared stash have (Options > Gameplay; never fewer than have items on them)
void GameInventory_SetStashPages(GameInventory *inventory, int pages, int shared_pages);
bool GameInventory_StashOpen(const GameInventory *inventory);
bool GameInventory_IsOverStash(const GameInventory *inventory, int mouse_x, int mouse_y); // (or the cube)
bool GameInventory_CubeOpen(const GameInventory *inventory); // the Horadric Cube on the left (right click it to open)
// A merchant's store on the left (NULL closes it): click an item to buy it (Shift: potions into the belt), put an item
// on it or right click one in the inventory to sell it
struct ShopStock;

// Trading with another player (multiplayer): the game's trade panel on the left, their offer above (to look at),
// yours below (put items from the inventory in it, take them back out), the gold each offers, and who has accepted
#define TRADE_W     10
#define TRADE_H     4
#define TRADE_ITEMS (TRADE_W * TRADE_H)
typedef struct TradeView {
    char          their_name[16];
    InventoryItem theirs[TRADE_ITEMS];
    int           their_count;
    int           their_gold;
    bool          their_accept;
    InventoryItem mine[TRADE_ITEMS];
    int           mine_count;
    int           my_gold;
    bool          my_accept;
} TradeView;
void GameInventory_SetTrade(GameInventory *inventory, TradeView *trade); // NULL: not trading
bool GameInventory_TradeOpen(const GameInventory *inventory);
int  GameInventory_TradeAt(const InventoryItem *items, int count, int cell_x, int cell_y);
bool GameInventory_TradePlace(InventoryItem *items, int *count, const Item *item, int x, int y); // where it's free
bool GameInventory_TradeAdd(InventoryItem *items, int *count, const Item *item);                 // anywhere free
void GameInventory_SetShop(GameInventory *inventory, struct ShopStock *stock, int difficulty);
bool GameInventory_ShopOpen(const GameInventory *inventory);
bool GameInventory_IsOverShop(const GameInventory *inventory, int mouse_x, int mouse_y); // (the trade panel too)
bool GameInventory_IsOverBelt(const Character *character, int mouse_x, int mouse_y);

void GameInventory_Draw(GameInventory *inventory, const Character *character, int mouse_x, int mouse_y);
void GameInventory_DrawBelt(GameInventory *inventory, const Character *character);
// On top of everything: the carried item, and what's under the mouse
void GameInventory_DrawOverlay(GameInventory *inventory, const Character *character, bool open, int mouse_x,
                               int mouse_y);

#endif // ABYSS_GAME_INVENTORY_H
