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
    INVENTORY_EVENT_NONE      = 0,
    INVENTORY_EVENT_EQUIPMENT = 1, // what's worn changed: damage and defense need working out again
    INVENTORY_EVENT_DRINK     = 2, // a potion was drunk: *drunk says which
    INVENTORY_EVENT_MOVED     = 4, // something was picked up or put down (a sound)
} GameInventoryEvent;

GameInventory *GameInventory_Create(void);
void           GameInventory_Destroy(GameInventory **inventory);

// Clicks (the moment the button goes down); open: whether the panel is showing (the belt works either way).
// Returns GameInventoryEvent flags; *message is something to tell the player, or NULL.
int GameInventory_Update(GameInventory *inventory, Character *character, bool open, int mouse_x, int mouse_y,
                         bool left_click, bool right_click, bool shift, Item *drunk, const char **message);
// Drinks what's in a belt slot (0..3, the bottom row), the way keys 1-4 do
bool GameInventory_DrinkBelt(Character *character, int slot, Item *drunk);

bool        GameInventory_Holding(const GameInventory *inventory); // an item on the cursor
const char *GameInventory_Sound(const GameInventory *inventory);   // the sound of what was last put down ("" none)
void        GameInventory_PutBack(GameInventory *inventory, Character *character); // the carried item back where it was
bool        GameInventory_IsOver(int mouse_x, int mouse_y);                        // over the panel
bool        GameInventory_IsOverBelt(const Character *character, int mouse_x, int mouse_y);

void GameInventory_Draw(GameInventory *inventory, const Character *character, int mouse_x, int mouse_y);
void GameInventory_DrawBelt(GameInventory *inventory, const Character *character);
// On top of everything: the carried item, and what's under the mouse
void GameInventory_DrawOverlay(GameInventory *inventory, const Character *character, bool open, int mouse_x,
                               int mouse_y);

#endif // ABYSS_GAME_INVENTORY_H
