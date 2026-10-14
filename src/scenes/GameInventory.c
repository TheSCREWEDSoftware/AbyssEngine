#include "GameInventory.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Inventory.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVENTORY_PANEL "/data/global/ui/PANEL/invchar6.dc6" // frames 4-7: the inventory (0-3: the character)
#define CELL            29                                   // a grid cell, in pixels
#define ICON_CACHE      64
#define TOOLTIP_LINES   10

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

typedef enum Origin { ORIGIN_GRID, ORIGIN_EQUIP, ORIGIN_BELT } Origin;

typedef struct IconCache {
    char    file[32];
    Sprite *sprite;
} IconCache;

struct GameInventory {
    Sprite   *panel;
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
};

const char *GameInventory_Sound(const GameInventory *inventory) { return inventory->sound; }

static void GameInventory__PutDown(GameInventory *inventory, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    snprintf(inventory->sound, sizeof(inventory->sound), "%s", base != NULL ? base->use_sound : "");
}

GameInventory *GameInventory_Create(void) {
    GameInventory *inventory = calloc(1, sizeof(GameInventory));
    FAIL_IF_NULL(inventory);
    inventory->panel = Sprite_Create(INVENTORY_PANEL, PALETTE_UNITS);
    inventory->text  = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    inventory->small = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    return inventory;
}

void GameInventory_Destroy(GameInventory **inventory_ref) {
    GameInventory *inventory = *inventory_ref;
    if (inventory == NULL) {
        return;
    }
    Sprite_Destroy(&inventory->panel);
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

static Sprite *GameInventory__Icon(GameInventory *inventory, const ItemBase *base) {
    if (base == NULL || base->inv_file[0] == '\0') {
        return NULL;
    }
    for (int i = 0; i < inventory->icon_count; i++) {
        if (strcmp(inventory->icons[i].file, base->inv_file) == 0) {
            return inventory->icons[i].sprite;
        }
    }
    if (inventory->icon_count >= ICON_CACHE) {
        return NULL;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/%s.dc6", base->inv_file);
    IconCache *entry = &inventory->icons[inventory->icon_count++];
    snprintf(entry->file, sizeof(entry->file), "%s", base->inv_file);
    entry->sprite = Sprite_Create(path, PALETTE_UNITS);
    return entry->sprite;
}

// An item's picture, centred in a box; tinted red when it can't be used
static void GameInventory__DrawItem(GameInventory *inventory, const ItemBase *base, const SDL_Rect *box,
                                    const bool usable) {
    Sprite *icon = GameInventory__Icon(inventory, base);
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

void GameInventory_PutBack(GameInventory *inventory, Character *character) {
    if (!inventory->holding) {
        return;
    }
    if (inventory->still_worn) { // Modern: it never came off
        inventory->holding    = false;
        inventory->still_worn = false;
        return;
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
    }
    if (!placed && !Inventory_Add(character, &inventory->held)) {
        Inventory_AddToBelt(character, &inventory->held);
    }
    inventory->holding = false;
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
    if (!open && inventory->holding) {
        GameInventory_PutBack(inventory, character);
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

void GameInventory_Draw(GameInventory *inventory, const Character *character, const int mouse_x, const int mouse_y) {
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
            GameInventory__DrawItem(inventory, base, &slot_rects[s], Inventory_MeetsRequirements(character, base));
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
        GameInventory__DrawItem(inventory, base, &rect,
                                base == NULL || base->kind == ITEM_KIND_MISC ||
                                    Inventory_MeetsRequirements(character, base));
    }

    // Gold
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
            GameInventory__DrawItem(inventory, Items_Find(item->code), &rect, true);
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
    GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, base->name);
    if (base->kind == ITEM_KIND_WEAPON) {
        if (base->max_damage > 0) {
            snprintf(text, sizeof(text), "One-Hand Damage: %d to %d", base->min_damage, base->max_damage);
            GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
        }
        if (base->two_hand_max > 0) {
            snprintf(text, sizeof(text), "Two-Hand Damage: %d to %d", base->two_hand_min, base->two_hand_max);
            GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
        }
    } else if (base->kind == ITEM_KIND_ARMOR) {
        snprintf(text, sizeof(text), "Defense: %d", item->defense);
        GameInventory__AddLine(lines, colors, &count, 0xFF, 0xFF, 0xFF, text);
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
        {"Required Strength",  base->required_strength,  character->stats[STAT_STRENGTH] },
        {"Required Dexterity", base->required_dexterity, character->stats[STAT_DEXTERITY]},
        {"Required Level",     base->required_level,     character->level                },
    };
    for (int r = 0; r < 3; r++) {
        if (requirements[r].need > 0) {
            const bool met = requirements[r].have >= requirements[r].need;
            snprintf(text, sizeof(text), "%s: %d", requirements[r].name, requirements[r].need);
            GameInventory__AddLine(lines, colors, &count, 0xFF, met ? 0xFF : 0x40, met ? 0xFF : 0x40, text);
        }
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

void GameInventory_DrawOverlay(GameInventory *inventory, const Character *character, const bool open, const int mouse_x,
                               const int mouse_y) {
    if (inventory->holding) {
        const ItemBase *base = Items_Find(inventory->held.code);
        const SDL_Rect  box  = {mouse_x - ((base != NULL ? base->width : 1) * CELL / 2),
                                mouse_y - ((base != NULL ? base->height : 1) * CELL / 2),
                                (base != NULL ? base->width : 1) * CELL, (base != NULL ? base->height : 1) * CELL};
        GameInventory__DrawItem(inventory, base, &box, true);
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
        if (GameInventory__CellAt(mouse_x, mouse_y, &cell_x, &cell_y)) {
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
        GameInventory__Tooltip(inventory, character, item, anchor_x, anchor_y);
    }
}
