// SceneRoam: text mode (included at the end of SceneRoam.c; AbyssEngine --text). The game draws itself as characters
// for a terminal: the sub-tiles around you as a grid ('#' blocked, '.' ground, '>' a way out, letters for monsters,
// '@' you), a few lines of life, mana, messages and help below, and keys do what the mouse does in the game. The same
// game underneath: areas, seeds, combat, quests, saving.

#define TEXT_CELL_MAX 16 // bytes one grid cell can take with its colour

typedef struct TextCell {
    char ch;
    int  color; // ANSI: 30-37, +60 bright; 0 = default
} TextCell;

static int Roam__TextNearest(const SceneRoam *roam, const float range, const int kind) {
    // kind 0: a monster to fight; 1: something to use or someone to talk to
    int   best      = -1;
    float best_dist = range;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        const float d    = Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y);
        if (d >= best_dist || !Roam__IsAlive(unit)) {
            continue;
        }
        const bool fits = kind == 0 ? Roam__IsCombatant(unit) && !Roam__IsPeaceful(roam, unit) && !unit->ally
                                    : Roam__CanUse(unit) || (roam->game && Roam__CanTalk(roam, unit));
        if (fits) {
            best      = i;
            best_dist = d;
        }
    }
    return best;
}

static int Roam__TextNearestItem(const SceneRoam *roam, const float range) {
    int   best      = -1;
    float best_dist = range;
    for (int g = 0; g < roam->ground_count; g++) {
        const GroundItem *ground = &roam->ground[g];
        const float       d      = Roam__Distance(ground->x, ground->y, roam->player.x, roam->player.y);
        if (ground->map_key == Roam__MapKey(roam) && d < best_dist) {
            best      = g;
            best_dist = d;
        }
    }
    return best;
}

static TextCell Roam__TextUnit(const SceneRoam *roam, const Unit *unit) {
    TextCell cell = {'?', 37};
    if (unit->is_object) {
        switch (unit->object.operate_fn) {
        case 23:
            cell = (TextCell){'W', 96};
            break;
        case 32:
            cell = (TextCell){'S', 93};
            break;
        case 2:
            cell = (TextCell){'!', 95};
            break;
        case 8:
            cell = (TextCell){unit->used ? '\'' : '+', 33};
            break;
        default:
            cell = (TextCell){unit->used ? '_' : '=', 33};
            break;
        }
        return cell;
    }
    const char letter = unit->name[0] != '\0' ? unit->name[0] : unit->id[0];
    if (unit->ally) {
        return (TextCell){(char)(unit->hireling ? 'h' : 'm'), 92};
    }
    if (unit->npc) {
        return (TextCell){(char)(letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter), 36};
    }
    if (!Roam__IsAlive(unit)) {
        return (TextCell){'%', 31};
    }
    const char upper = (char)(letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter);
    switch (unit->rank) {
    case RANK_CHAMPION:
        return (TextCell){upper, 94};
    case RANK_UNIQUE:
    case RANK_SUPER_UNIQUE:
        return (TextCell){upper, 93};
    default:
        (void)roam;
        return (TextCell){upper, 91};
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Text mode's lists: your items (i), and a store (Trade or Gamble at the town folk)

enum { TEXT_VIEW_MAP, TEXT_VIEW_ITEMS, TEXT_VIEW_STORE };

static void Roam__TextItemName(const Item *item, char *text, const size_t size) {
    char name1[96];
    char name2[96];
    Affixes_Name(item, name1, sizeof(name1), name2, sizeof(name2));
    static const char *const qualities[ITEM_QUALITY_COUNT] = {"",         "magic", "rare",   "unique",
                                                              "runeword", "set",   "crafted"};
    const char              *quality = item->quality < ITEM_QUALITY_COUNT ? qualities[item->quality] : "";
    snprintf(text, size, "%s%s%s%s%s%s%s", name1, name2[0] != '\0' ? " - " : "", name2, quality[0] != '\0' ? " (" : "",
             quality, quality[0] != '\0' ? ")" : "", item->unidentified ? " [unidentified]" : "");
    if (item->quantity > 1) {
        const size_t len = strlen(text);
        snprintf(text + len, size - len, " x%d", item->quantity);
    }
}

// Appends to the frame being built, never past its end: where the text now ends (snprintf's return is what it would
// have written, which can be more than there was room for)
static int Roam__TextAppend(char *out, const int size, const int at, const char *format, ...) {
    if (at < 0 || at >= size - 1) {
        return size > 0 ? size - 1 : 0;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(out + at, (size_t)(size - at), format, args);
    va_end(args);
    if (written < 0) {
        return at;
    }
    return at + written < size - 1 ? at + written : size - 1;
}

// The list in the map's place; returns where the output got to
static int Roam__TextList(SceneRoam *roam, char *out, int at, const int size, const int rows) {
    const Character *c    = roam->character;
    int              line = 0;
    char             name[200];
#define TEXT_LINE(...)                                                                                                 \
    do {                                                                                                               \
        if (line < rows && at < size - 300) {                                                                          \
            at = Roam__TextAppend(out, size, at, __VA_ARGS__);                                                         \
            at = Roam__TextAppend(out, size, at, "\x1b[0m\x1b[K\n");                                                   \
            line++;                                                                                                    \
        }                                                                                                              \
    } while (0)
    if (roam->text_view == TEXT_VIEW_STORE && roam->open_stock != NULL) {
        const ShopStock *stock = roam->open_stock;
        TEXT_LINE("\x1b[93m%s's %s\x1b[0m   (your gold: %d)", stock->vendor, stock->gamble ? "gamble" : "wares",
                  c->gold);
        for (int i = 0; i < stock->count && i < 26; i++) {
            Roam__TextItemName(&stock->items[i].item, name, sizeof(name));
            TEXT_LINE("  %c) %-60s %d gold", 'a' + i, name, Shop_Price(stock, &stock->items[i].item));
        }
    } else {
        TEXT_LINE("\x1b[93mCarried\x1b[0m");
        for (int i = 0; i < c->inventory_count && i < 26; i++) {
            Roam__TextItemName(&c->inventory[i].item, name, sizeof(name));
            TEXT_LINE("  %s%c) %s", i == roam->text_pick ? "\x1b[7m" : "", 'a' + i, name);
        }
        TEXT_LINE("\x1b[93mWorn\x1b[0m");
        for (int s = 0; s < EQUIP_COUNT; s++) {
            if (c->equipped[s].code[0] != '\0') {
                Roam__TextItemName(&c->equipped[s], name, sizeof(name));
                TEXT_LINE("  %-10s %s", Items_SlotName((EquipSlot)s), name);
            }
        }
        if (roam->in_town) {
            TEXT_LINE("\x1b[93mStash\x1b[0m   (gold: %d)", c->stash_gold);
            for (int i = 0; i < c->stash_count && i < 26; i++) {
                Roam__TextItemName(&c->stash[i].item, name, sizeof(name));
                TEXT_LINE("  %c) %s", 'A' + i, name);
            }
        }
        if (Cube_Carried(c)) {
            TEXT_LINE("\x1b[93mHoradric Cube\x1b[0m");
            for (int i = 0; i < c->cube_count && i < 9; i++) {
                Roam__TextItemName(&c->cube[i].item, name, sizeof(name));
                TEXT_LINE("  %c) %s", '1' + i, name);
            }
        }
    }
    while (line < rows && at < size - 20) {
        at = Roam__TextAppend(out, size, at, "\x1b[K\n");
        line++;
    }
#undef TEXT_LINE
    return at;
}

// Wear the picked item: the first slot it suits (what was there goes into the carried items)
static void Roam__TextWear(SceneRoam *roam, const int index) {
    Character *c = roam->character;
    if (index < 0 || index >= c->inventory_count) {
        return;
    }
    const Item  item = c->inventory[index].item;
    const char *why  = "That can't be worn.";
    for (int s = 0; s < EQUIP_COUNT; s++) {
        const Item before = c->equipped[s];
        memset(&c->equipped[s], 0, sizeof(Item));
        if (!Inventory_CanEquip(c, &item, (EquipSlot)s, &why)) {
            c->equipped[s] = before;
            continue;
        }
        Inventory_Remove(c, index);
        c->equipped[s] = item;
        if (before.code[0] != '\0' && !Inventory_Add(c, &before)) {
            // no room for what came off: it stays on
            Inventory_Add(c, &item);
            c->equipped[s] = before;
            Roam__ShowMessage(roam, "No room for what you have on there.");
            return;
        }
        Roam__ApplyCharacter(roam);
        Roam__ShowMessage(roam, "Worn.");
        return;
    }
    Roam__ShowMessage(roam, why);
}

static void Roam__TextItemsKey(SceneRoam *roam, const int key) {
    Character *c = roam->character;
    if (key >= 'a' && key <= 'z' && roam->text_pick < 0) {
        roam->text_pick = key - 'a' < c->inventory_count ? key - 'a' : -1;
        return;
    }
    if (roam->text_pick < 0 && key >= 'A' && key <= 'Z' && roam->in_town && key - 'A' < c->stash_count) {
        const Item item = c->stash[key - 'A'].item; // out of the stash
        if (Inventory_Add(c, &item)) {
            Stash_Remove(c, key - 'A');
        } else {
            Roam__ShowMessage(roam, "No room in your inventory.");
        }
        return;
    }
    if (roam->text_pick < 0 && key >= '1' && key <= '9' && Cube_Carried(c) && key - '1' < c->cube_count) {
        const Item item = c->cube[key - '1'].item; // out of the cube
        if (Inventory_Add(c, &item)) {
            Cube_Remove(c, key - '1');
        } else {
            Roam__ShowMessage(roam, "No room in your inventory.");
        }
        return;
    }
    if (roam->text_pick < 0 && key == '0' && Cube_Carried(c)) {
        char message[128];
        Cube_Transmute(c, message, sizeof(message));
        Roam__ShowMessage(roam, message);
        return;
    }
    const int pick = roam->text_pick;
    if (pick < 0 || pick >= c->inventory_count) {
        roam->text_pick = -1;
        return;
    }
    const Item item = c->inventory[pick].item;
    switch (key) {
    case 'w':
        Roam__TextWear(roam, pick);
        break;
    case 'u': {
        const ItemBase *base = Items_Find(item.code);
        if (Items_IsPotion(base)) {
            Inventory_Remove(c, pick);
            Roam__Drink(roam, &item);
        } else if (strcmp(item.code, "tsc") == 0 || strcmp(item.code, "tbk") == 0) {
            // Town Portal: it opens beside you (u by it goes through)
            if (roam->in_town) {
                Roam__ShowMessage(roam, "You can't open a portal here.");
            } else if (strcmp(item.code, "tbk") == 0 && item.quantity <= 0) {
                Roam__ShowMessage(roam, "The tome is empty.");
            } else {
                if (strcmp(item.code, "tbk") == 0) {
                    c->inventory[pick].item.quantity--;
                } else {
                    Inventory_Remove(c, pick);
                }
                Roam__OpenTownPortal(roam);
                Roam__ShowMessage(roam, "A Town Portal opens beside you.");
            }
        } else {
            Roam__ShowMessage(roam, "That can't be used here.");
        }
        break;
    }
    case 'x': {
        float x;
        float y;
        Inventory_Remove(c, pick);
        Roam__FreeSpot(roam, &x, &y);
        if (!roam->online) {
            Roam__AddGroundItem(roam, &item, Roam__MapKey(roam), x, y, true, 0);
        } else {
            Roam__PutOnGround(roam, &item, x, y);
        }
        break;
    }
    case 'p':
        if (!roam->in_town) {
            Roam__ShowMessage(roam, "The stash is in town.");
        } else if (Stash_Add(c, &item)) {
            Inventory_Remove(c, pick);
        } else {
            Roam__ShowMessage(roam, "No room in the stash.");
        }
        break;
    case 'b':
        if (!Cube_Carried(c) || strcmp(item.code, CUBE_ITEM) == 0) {
            Roam__ShowMessage(roam, "You need the Horadric Cube for that.");
        } else if (Cube_Add(c, &item)) {
            Inventory_Remove(c, pick);
        } else {
            Roam__ShowMessage(roam, "No room in the cube.");
        }
        break;
    case 's':
        if (roam->open_stock != NULL && GameInventory_ShopOpen(roam->inventory_ui)) {
            const int price = Shop_SellPrice(roam->open_stock, &item, Roam__Difficulty(roam));
            Inventory_Remove(c, pick);
            Shop_Take(roam->open_stock, &item);
            c->gold += price;
            char text[64];
            snprintf(text, sizeof(text), "Sold for %d gold.", price);
            Roam__ShowMessage(roam, text);
        } else {
            Roam__ShowMessage(roam, "Nobody's buying: trade with someone first.");
        }
        break;
    default:
        break;
    }
    roam->text_pick = -1;
}

static void Roam__TextStoreKey(SceneRoam *roam, const int key) {
    ShopStock *stock = roam->open_stock;
    Character *c     = roam->character;
    if (stock == NULL || key < 'a' || key > 'z' || key - 'a' >= stock->count) {
        return;
    }
    const int index = key - 'a';
    Item      item  = stock->items[index].item;
    const int price = Shop_Price(stock, &item);
    if (price > c->gold) {
        Roam__ShowMessage(roam, "You don't have enough gold for that.");
        return;
    }
    if (stock->gamble) {
        Shop_Gamble(&item, c->level);
    }
    if (!Inventory_Add(c, &item)) {
        Roam__ShowMessage(roam, "You don't have room for that.");
        return;
    }
    c->gold -= price;
    Shop_Remove(stock, index);
    char name[200];
    Roam__TextItemName(&item, name, sizeof(name));
    char text[240];
    snprintf(text, sizeof(text), "Bought %s.", name);
    Roam__ShowMessage(roam, text);
}

// m: the area a tile a letter, only what you've seen ('#' walls, '.' ground, '>' a way out, 'W' a waypoint, '&' the
// others, '@' you), centred on you
static void Roam__TextAutomap(SceneRoam *roam, TextCell *grid, const int cols, const int map_rows) {
    const int ptx  = (int)(roam->player.x / MAP_SUBTILES);
    const int pty  = (int)(roam->player.y / MAP_SUBTILES);
    const int left = ptx - (cols / 2);
    const int top  = pty - (map_rows / 2);
    for (int r = 0; r < map_rows; r++) {
        for (int c = 0; c < cols; c++) {
            const int tx   = left + c;
            const int ty   = top + r;
            TextCell *cell = &grid[(r * cols) + c];
            *cell          = (TextCell){' ', 0};
            if (tx < 0 || ty < 0 || tx >= roam->map->width || ty >= roam->map->height ||
                !Automap_TileSeen(roam->automap, tx, ty)) {
                continue;
            }
            int open = 0;
            for (int sy = 0; sy < MAP_SUBTILES; sy++) {
                for (int sx = 0; sx < MAP_SUBTILES; sx++) {
                    open += Map_IsWalkable(roam->map, (tx * MAP_SUBTILES) + sx, (ty * MAP_SUBTILES) + sy) ? 1 : 0;
                }
            }
            const float cx = ((float)tx + 0.5f) * MAP_SUBTILES;
            const float cy = ((float)ty + 0.5f) * MAP_SUBTILES;
            if (Map_ExitAt(roam->map, cx, cy) != NULL) {
                *cell = (TextCell){'>', 95};
            } else {
                *cell = open >= 8 ? (TextCell){'.', 90} : (TextCell){'#', 37};
            }
        }
    }
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        const int   c    = (int)(unit->x / MAP_SUBTILES) - left;
        const int   r    = (int)(unit->y / MAP_SUBTILES) - top;
        if (unit->is_object && unit->object.operate_fn == 23 && c >= 0 && r >= 0 && c < cols && r < map_rows &&
            grid[(r * cols) + c].ch != ' ') {
            grid[(r * cols) + c] = (TextCell){'W', 96};
        }
    }
    for (int o = 0; o < roam->other_count; o++) {
        const int c = (int)(roam->others[o].x / MAP_SUBTILES) - left;
        const int r = (int)(roam->others[o].y / MAP_SUBTILES) - top;
        if (c >= 0 && r >= 0 && c < cols && r < map_rows) {
            grid[(r * cols) + c] = (TextCell){'&', 92};
        }
    }
    grid[((map_rows / 2) * cols) + (cols / 2)] = (TextCell){'@', 97};
}

int Game_TextRender(void *scene_ref, char *out, const int size, int cols, int rows) {
    SceneRoam *roam    = scene_ref;
    roam->text_mode    = true;
    cols               = cols < 40 ? 40 : (cols > 300 ? 300 : cols);
    rows               = rows < 12 ? 12 : (rows > 120 ? 120 : rows);
    const int map_rows = rows - 5;
    TextCell *grid     = calloc((size_t)(cols * map_rows), sizeof(TextCell));
    if (grid == NULL || roam->map == NULL) {
        free(grid);
        return Roam__TextAppend(out, size, 0, "Loading...\n");
    }
    Automap_Reveal(roam->automap, roam->player.x, roam->player.y); // (what's been seen: m shows it)
    const int sub_w = roam->map->width * MAP_SUBTILES;
    const int sub_h = roam->map->height * MAP_SUBTILES;
    if (roam->text_automap) {
        Roam__TextAutomap(roam, grid, cols, map_rows);
    }
    const int left = (int)roam->player.x - (cols / 2);
    const int top  = (int)roam->player.y - (map_rows / 2);
    for (int r = 0; r < map_rows && !roam->text_automap; r++) {
        for (int c = 0; c < cols; c++) {
            const int x    = left + c;
            const int y    = top + r;
            TextCell *cell = &grid[(r * cols) + c];
            if (x < 0 || y < 0 || x >= sub_w || y >= sub_h) {
                *cell = (TextCell){' ', 0};
            } else if (Map_ExitAt(roam->map, (float)x + 0.5f, (float)y + 0.5f) != NULL) {
                *cell = (TextCell){'>', 95};
            } else if (Map_IsWalkable(roam->map, x, y)) {
                *cell = (TextCell){'.', 90};
            } else {
                *cell = (TextCell){'#', 37};
            }
        }
    }
#define TEXT_PUT(fx, fy, value)                                                                                        \
    do {                                                                                                               \
        const int tc_ = (int)(fx) - left;                                                                              \
        const int tr_ = (int)(fy) - top;                                                                               \
        if (tc_ >= 0 && tr_ >= 0 && tc_ < cols && tr_ < map_rows) {                                                    \
            grid[(tr_ * cols) + tc_] = (value);                                                                        \
        }                                                                                                              \
    } while (0)
    for (int g = 0; g < roam->ground_count && !roam->text_automap; g++) {
        const GroundItem *ground = &roam->ground[g];
        if (ground->map_key == Roam__MapKey(roam)) {
            TEXT_PUT(ground->x, ground->y, ((TextCell){strcmp(ground->item.code, "gld") == 0 ? '$' : '*', 97}));
        }
    }
    for (int i = 0; i < roam->unit_count && !roam->text_automap; i++) {
        const Unit *unit = &roam->units[i];
        if ((unit->is_object && !unit->object.selectable) || unit->vanished) {
            continue;
        }
        TEXT_PUT(unit->x, unit->y, Roam__TextUnit(roam, unit));
    }
    for (int o = 0; o < roam->other_count && !roam->text_automap; o++) {
        TEXT_PUT(roam->others[o].x, roam->others[o].y, ((TextCell){'&', 92}));
    }
    if (roam->corpse_key != 0 && roam->corpse_key == Roam__MapKey(roam) && !roam->text_automap) {
        TEXT_PUT(roam->corpse.x, roam->corpse.y, ((TextCell){'%', 97}));
    }
    if (!roam->text_automap) {
        TEXT_PUT(roam->player.x, roam->player.y, ((TextCell){'@', 97}));
    }
#undef TEXT_PUT

    // The bell: when you've been hit, and now and then while your life is low (b turns it off)
    const bool hit = roam->game && roam->player.hp < roam->text_last_hp && Roam__IsAlive(&roam->player);
    const bool low = roam->game && Roam__IsAlive(&roam->player) && roam->player.hp * 4 < roam->player.max_hp &&
                     SDL_GetTicks64() - roam->text_warned > 3000;
    roam->text_last_hp = roam->player.hp;
    if (low) {
        roam->text_warned = SDL_GetTicks64();
    }
    // The grid, with a colour code only where it changes (or the list that's open in its place)
    int at    = Roam__TextAppend(out, size, 0, "%s\x1b[H", (hit || low) && !roam->text_quiet ? "\a" : "");
    int color = -1;
    if (roam->text_view == TEXT_VIEW_STORE && !GameInventory_ShopOpen(roam->inventory_ui)) {
        roam->text_view = TEXT_VIEW_MAP; // (walked away from the store)
    }
    if (roam->text_view != TEXT_VIEW_MAP) {
        at = Roam__TextList(roam, out, at, size, map_rows);
    }
    for (int r = 0; r < map_rows && at < size - (cols * TEXT_CELL_MAX) && roam->text_view == TEXT_VIEW_MAP; r++) {
        for (int c = 0; c < cols; c++) {
            const TextCell *cell = &grid[(r * cols) + c];
            if (cell->color != color) {
                at    = Roam__TextAppend(out, size, at, "\x1b[%dm", cell->color);
                color = cell->color;
            }
            if (at < size - 1) {
                out[at++] = cell->ch;
            }
        }
        at    = Roam__TextAppend(out, size, at, "\x1b[0m\x1b[K\n");
        color = -1;
    }
    free(grid);

    // The panel: who you are, life and mana, the area; the newest message; what's near; the keys
    const Character   *c                   = roam->character;
    static const char *difficulty_names[3] = {"", " (Nightmare)", " (Hell)"};
    if (c != NULL) {
        at = Roam__TextAppend(
            out, size, at,
            "\x1b[97m%s\x1b[0m, level %d %s   \x1b[91mLife %d/%d\x1b[0m   \x1b[94mMana %d/%d\x1b[0m   Gold %d   "
            "%s%s\x1b[K\n",
            c->name, c->level, Character_ClassName(c->class_id), roam->player.hp, roam->player.max_hp, (int)roam->mana,
            Character_MaxMana(c), c->gold, Roam__AreaName(roam), difficulty_names[Roam__Difficulty(roam)]);
    }
    const uint64_t now = SDL_GetTicks64();
    at                 = Roam__TextAppend(out, size, at, "%s\x1b[K\n",
                          roam->player.state == UNIT_DEAD ? "You died."
                                                                          : (now < roam->message_until ? roam->message : ""));
    if (roam->npc_menu >= 0) {
        at = Roam__TextAppend(out, size, at, "%s:", roam->units[roam->npc_menu].name);
        for (int o = 0; o < roam->npc_option_count && at < size - 200; o++) {
            at = Roam__TextAppend(out, size, at, "  %d) %s", o + 1, Roam__NpcOptionText(roam, roam->npc_options[o]));
        }
        at = Roam__TextAppend(out, size, at, "\x1b[K\n");
    } else {
        const int near = Roam__TextNearest(roam, 15.0f, 0);
        if (near >= 0) {
            const Unit *unit = &roam->units[near];
            at               = Roam__TextAppend(out, size, at, "Near: %s (%d%% life)\x1b[K\n", unit->name,
                                  unit->max_hp > 0 ? unit->hp * 100 / unit->max_hp : 0);
        } else {
            at = Roam__TextAppend(out, size, at, "\x1b[K\n");
        }
    }
    const char *keys =
        roam->text_view == TEXT_VIEW_STORE ? "a-z buy   i your items (s sells the one picked)   Esc leave the store"
        : roam->text_view == TEXT_VIEW_ITEMS
            ? (roam->text_pick >= 0
                   ? "w wear   u use   x drop   s sell   p to the stash   b to the cube   other: none"
                   : "a-z pick   A-Z take from the stash   1-9 from the cube   0 transmute   i/Esc back")
            : "wasd qezc move  f fight  r/l skill  u use/talk  g pick up  1-4 potion  i items  m map  t town  Q quit";
    at = Roam__TextAppend(out, size, at, "\x1b[90m%s\x1b[0m\x1b[K\n\x1b[J", keys);
    return at;
}

void Game_TextKey(void *scene_ref, const int key) {
    SceneRoam     *roam   = scene_ref;
    Unit          *player = &roam->player;
    const uint64_t now    = SDL_GetTicks64();
    if (key == 'Q' && roam->game) {
        Roam__ExitGame(roam);
        return;
    }
    if (roam->text_view != TEXT_VIEW_MAP) {
        if (key == 0x1B || (key == 'i' && roam->text_view == TEXT_VIEW_ITEMS && roam->text_pick < 0)) {
            if (roam->text_view == TEXT_VIEW_STORE) {
                Roam__CloseShop(roam);
            }
            roam->text_view = TEXT_VIEW_MAP;
            roam->text_pick = -1;
        } else if (key == 'i' && roam->text_view == TEXT_VIEW_STORE) {
            roam->text_view = TEXT_VIEW_ITEMS; // (the store stays open: s sells)
        } else if (roam->text_view == TEXT_VIEW_ITEMS) {
            Roam__TextItemsKey(roam, key);
        } else {
            Roam__TextStoreKey(roam, key);
        }
        return;
    }
    if (key == 'i' && roam->game) {
        roam->text_view = TEXT_VIEW_ITEMS;
        roam->text_pick = -1;
        return;
    }
    if (roam->npc_menu >= 0 && key >= '1' && key <= '9') {
        const int option = key - '1';
        if (option < roam->npc_option_count) {
            const int choice = roam->npc_options[option];
            Roam__NpcChoose(roam, choice);
            if (choice == NPC_TRADE || choice == NPC_GAMBLE) {
                roam->text_view = TEXT_VIEW_STORE; // the store as a list
            }
        }
        return;
    }
    static const struct {
        int   key;
        float dx;
        float dy;
    } steps[] = {
        {'w', 0,  -1},
        {'s', 0,  1 },
        {'a', -1, 0 },
        {'d', 1,  0 },
        {'q', -1, -1},
        {'e', 1,  -1},
        {'z', -1, 1 },
        {'c', 1,  1 }
    };
    for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        if (key == steps[s].key && Roam__IsAlive(player)) {
            roam->npc_menu      = -1;
            roam->attack_target = -1;
            roam->use_target    = -1;
            roam->item_target   = -1;
            player->target_x    = player->x + (steps[s].dx * 4.0f);
            player->target_y    = player->y + (steps[s].dy * 4.0f);
            roam->moving        = true;
            Roam__ClearPath(player);
            return;
        }
    }
    switch (key) {
    case 'f': {
        const int target = Roam__TextNearest(roam, 20.0f, 0);
        if (target >= 0) {
            roam->attack_target   = target;
            roam->attack_in_place = false;
        } else {
            Roam__ShowMessage(roam, "Nothing to fight near you.");
        }
        break;
    }
    case 'r':
    case 'l': {
        const int target = Roam__TextNearest(roam, 20.0f, 0);
        const int skill  = key == 'r' ? roam->character->right_skill : roam->character->left_skill;
        if (target >= 0 && roam->game) {
            Roam__UseSkill(roam, skill, roam->units[target].x, roam->units[target].y, target, now);
        } else if (roam->game) {
            Roam__UseSkill(roam, skill, player->x + 3.0f, player->y, -1, now);
        }
        break;
    }
    case 'u': {
        const int target = Roam__TextNearest(roam, 12.0f, 1);
        if (target >= 0) {
            roam->use_target = target;
        } else {
            Roam__ShowMessage(roam, "Nothing to use or talk to near you.");
        }
        break;
    }
    case 'g': {
        const int item = Roam__TextNearestItem(roam, 15.0f);
        if (item >= 0) {
            roam->item_target = item;
        } else {
            Roam__ShowMessage(roam, "Nothing to pick up near you.");
        }
        break;
    }
    case '1':
    case '2':
    case '3':
    case '4': {
        Item drunk;
        if (roam->game && GameInventory_DrinkBelt(roam->character, key - '1', &drunk)) {
            Roam__Drink(roam, &drunk);
        }
        break;
    }
    case 'm':
        roam->text_automap = !roam->text_automap;
        break;
    case 'b':
        roam->text_quiet = !roam->text_quiet;
        Roam__ShowMessage(roam, roam->text_quiet ? "The bell is off." : "The bell is on.");
        break;
    case 't':
        if (roam->game && !roam->in_town && Roam__IsAlive(player)) {
            Roam__GoToTown(roam, roam->character->act);
        }
        break;
    case 'Q':
        if (roam->game) {
            Roam__ExitGame(roam);
        }
        break;
    default:
        break;
    }
}
