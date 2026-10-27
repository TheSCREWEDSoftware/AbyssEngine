#include "GameInventory.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Affixes.h"
#include "../game/Inventory.h"
#include "../game/Sockets.h"
#include "../managers/InputManager.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVENTORY_PANEL "/data/global/ui/PANEL/invchar6.dc6" // frames 4-7: the inventory (0-3: the character)
#define CELL            29                                   // a grid cell, in pixels
#define ICON_CACHE      64
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

typedef enum Origin { ORIGIN_GRID, ORIGIN_EQUIP, ORIGIN_BELT, ORIGIN_GROUND, ORIGIN_STASH } Origin;

// The stash, on the left (TradeStash.dc6, inventory.txt's "Big Bank Page 1"), and its gold with the withdraw button
#define STASH_PANEL         "/data/global/ui/PANEL/TradeStash.dc6"
#define STASH_GRID_X        74
#define STASH_GRID_Y        82
#define STASH_GOLD_BUTTON_X 75
#define STASH_GOLD_BUTTON_Y 23

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
    Sprite   *panel;
    Sprite   *gold_button;
    Sprite   *stash_panel;
    Sprite   *socket;     // gemsocket.dc6
    bool      stash_open; // in town, at the stash
    IconCache icons[ICON_CACHE];
    int       icon_count;
    Label    *text;  // tooltip lines (font16)
    Label    *small; // belt keys, quantities
    // The item on the cursor, and where it came from (it goes back there if the panel closes)
    bool   holding;
    Item   held;
    Origin origin;
    int    origin_index; // grid: x + y * INVENTORY_W + page * INVENTORY_PAGE; equip: the slot; belt: the slot
    bool   modern;       // Options > Gameplay > Picking up worn gear: Modern
    bool   still_worn;   // Modern: the carried item is still worn (in origin_index's slot) until it's put down
    char   sound[32];    // what was last put down sounds like
    bool   no_room;      // the panel closed and the carried item had nowhere to go: it stays on the cursor
};

const char *GameInventory_Sound(const GameInventory *inventory) { return inventory->sound; }

static void GameInventory__PutDown(GameInventory *inventory, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    snprintf(inventory->sound, sizeof(inventory->sound), "%s", base != NULL ? base->use_sound : "");
}

GameInventory *GameInventory_Create(void) {
    GameInventory *inventory = calloc(1, sizeof(GameInventory));
    FAIL_IF_NULL(inventory);
    inventory->panel       = Sprite_Create(INVENTORY_PANEL, PALETTE_UNITS);
    inventory->gold_button = Sprite_Create("/data/global/ui/PANEL/goldcoinbtn.dc6", PALETTE_SKY);
    inventory->stash_panel = Sprite_Create(STASH_PANEL, PALETTE_SKY);
    inventory->socket      = Sprite_Create("/data/global/ui/PANEL/gemsocket.dc6", PALETTE_UNITS);
    inventory->text        = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    inventory->small       = Label_Create(FONTS_FONT8, PALETTE_STATIC);
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
    Sprite_Destroy(&inventory->socket);
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

static Sprite *GameInventory__Icon(GameInventory *inventory, const char *file) {
    if (file == NULL || file[0] == '\0') {
        return NULL;
    }
    for (int i = 0; i < inventory->icon_count; i++) {
        if (strcmp(inventory->icons[i].file, file) == 0) {
            return inventory->icons[i].sprite;
        }
    }
    if (inventory->icon_count >= ICON_CACHE) {
        return NULL;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/%s.dc6", file);
    IconCache *entry = &inventory->icons[inventory->icon_count++];
    snprintf(entry->file, sizeof(entry->file), "%s", file);
    entry->sprite = Sprite_Create(path, PALETTE_UNITS);
    return entry->sprite;
}

// An item's picture, centred in a box; tinted red when it can't be used
static void GameInventory__DrawItem(GameInventory *inventory, const Item *item, const SDL_Rect *box,
                                    const bool usable) {
    Sprite *icon = GameInventory__Icon(inventory, Affixes_InvFile(item)); // (uniques have their own picture)
    if (icon == NULL) {
        return;
    }
    if (!usable) {
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

void GameInventory_SetStash(GameInventory *inventory, const bool open) { inventory->stash_open = open; }

bool GameInventory_StashOpen(const GameInventory *inventory) { return inventory->stash_open; }

bool GameInventory_IsOverStash(const GameInventory *inventory, const int mouse_x, const int mouse_y) {
    return inventory->stash_open && mouse_x < GAME_PANEL_W && mouse_y < GAME_PANEL_H;
}

static bool GameInventory__StashCellAt(const int mouse_x, const int mouse_y, int *cell_x, int *cell_y) {
    if (mouse_x < STASH_GRID_X || mouse_y < STASH_GRID_Y || mouse_x >= STASH_GRID_X + (STASH_W * CELL) ||
        mouse_y >= STASH_GRID_Y + (STASH_H * CELL)) {
        return false;
    }
    *cell_x = (mouse_x - STASH_GRID_X) / CELL;
    *cell_y = (mouse_y - STASH_GRID_Y) / CELL;
    return true;
}

static SDL_Rect GameInventory__StashRect(const InventoryItem *it, const ItemBase *base) {
    const SDL_Rect rect = {STASH_GRID_X + (it->x * CELL), STASH_GRID_Y + (it->y * CELL),
                           (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
    return rect;
}

static bool GameInventory__OverStashGold(const int mouse_x, const int mouse_y) {
    return mouse_x >= STASH_GOLD_BUTTON_X && mouse_x < STASH_GOLD_BUTTON_X + GOLD_BUTTON_W &&
           mouse_y >= STASH_GOLD_BUTTON_Y && mouse_y < STASH_GOLD_BUTTON_Y + GOLD_BUTTON_H;
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

int GameInventory_Update(GameInventory *inventory, Character *character, const bool open, const int mouse_x,
                         const int mouse_y, const bool left_click, const bool right_click, const bool shift,
                         Item *drunk, const char **message) {
    *message            = NULL;
    inventory->sound[0] = '\0';
    int events          = INVENTORY_EVENT_NONE;
    if (!open && inventory->holding && !inventory->no_room) {
        inventory->no_room = !GameInventory_PutBack(inventory, character); // no room: it stays on the cursor
    }
    if (open || !inventory->holding) {
        inventory->no_room = false;
    }

    // The belt (on the control panel): right click drinks; with the inventory open, left click picks up / puts down
    const int belt_slot = GameInventory__BeltSlotAt(mouse_x, mouse_y);
    if (belt_slot >= 0) {
        if (right_click && GameInventory_DrinkBelt(character, belt_slot, drunk)) {
            return INVENTORY_EVENT_DRINK;
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
            const int       y       = top >= STASH_GRID_Y ? (top - STASH_GRID_Y) / CELL : -1;
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

// The stash on the left: its items, its gold and the button to take some out
static void GameInventory__DrawStash(GameInventory *inventory, const Character *character, const int mouse_x,
                                     const int mouse_y) {
    Sprite_DrawMulti(inventory->stash_panel, 0, 0, 0, 2, 2);
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        const int       left = mouse_x - ((base->width * CELL) / 2) + (CELL / 2);
        const int       top  = mouse_y - ((base->height * CELL) / 2) + (CELL / 2);
        const int       x    = left >= STASH_GRID_X ? (left - STASH_GRID_X) / CELL : -1;
        const int       y    = top >= STASH_GRID_Y ? (top - STASH_GRID_Y) / CELL : -1;
        if (Stash_Covered(character, base, x, y) != -2 &&
            GameInventory__StashCellAt(mouse_x, mouse_y, &(int){0}, &(int){0})) {
            const SDL_Rect area = {STASH_GRID_X + (x * CELL), STASH_GRID_Y + (y * CELL), base->width * CELL,
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
                                    Inventory_MeetsRequirements(character, base));
    }
    bool left_down = false;
    InputManager_GetMouseButtons(&left_down, NULL, NULL);
    Sprite_Draw(inventory->gold_button, GameInventory__OverStashGold(mouse_x, mouse_y) && left_down ? 1 : 0,
                STASH_GOLD_BUTTON_X, STASH_GOLD_BUTTON_Y + GOLD_BUTTON_H);
    char text[64];
    Label_SetAlignment(inventory->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Label_SetColor(inventory->text, 0xFF, 0xFF, 0xFF, 0xFF);
    snprintf(text, sizeof(text), "Gold in Stash: %d", character->stash_gold);
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, STASH_GOLD_BUTTON_X + GOLD_BUTTON_W + 8, STASH_GOLD_BUTTON_Y + 10);
    snprintf(text, sizeof(text), "Gold Max: %d", Stash_GoldLimit(character));
    Label_SetText(inventory->text, text);
    Label_Draw(inventory->text, STASH_GOLD_BUTTON_X + GOLD_BUTTON_W + 8, STASH_GOLD_BUTTON_Y + 34);
}

void GameInventory_Draw(GameInventory *inventory, const Character *character, const int mouse_x, const int mouse_y) {
    if (inventory->stash_open) {
        GameInventory__DrawStash(inventory, character, mouse_x, mouse_y);
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
            GameInventory__DrawItem(inventory, worn, &slot_rects[s], Inventory_MeetsRequirements(character, base));
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
                                    Inventory_MeetsRequirements(character, base));
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
        {"Required Strength", base->required_strength, Character_Stat(character, STAT_STRENGTH)},
        {"Required Dexterity", base->required_dexterity, Character_Stat(character, STAT_DEXTERITY)},
        {"Required Level", base->required_level, character->level},
    };
    for (int r = 0; r < 3; r++) {
        if (requirements[r].need > 0) {
            const bool met = requirements[r].have >= requirements[r].need;
            snprintf(text, sizeof(text), "%s: %d", requirements[r].name, requirements[r].need);
            GameInventory__AddLine(lines, colors, &count, 0xFF, met ? 0xFF : 0x40, met ? 0xFF : 0x40, text);
        }
    }
    // The magic, in blue
    for (int m = 0; m < item->mod_count && m < ITEM_MODS_MAX; m++) {
        Affixes_ModText(&item->mods[m], text, sizeof(text));
        GameInventory__AddLine(lines, colors, &count, 0x69, 0x69, 0xFF, text);
    }
    const char *set = Affixes_SetName(item);
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
    if (Items_IsPotion(base)) {
        GameInventory__AddLine(lines, colors, &count, 0xA0, 0xA0, 0xA0, "Right click to drink");
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
        int cell_x;
        int cell_y;
        if (inventory->stash_open && GameInventory__StashCellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
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
        GameInventory__DrawSockets(inventory, item, anchor_x, anchor_y);
        GameInventory__Tooltip(inventory, character, item, anchor_x, anchor_y);
    }
}
