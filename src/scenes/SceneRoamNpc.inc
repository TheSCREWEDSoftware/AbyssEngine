// SceneRoam: talking to the town folk (included by SceneRoam.c). Click one and walk up: a menu of what they do:
// trade (their store, from the item tables and npc.txt), travel to the next town (Warriv, Meshif, Tyrael), and the
// healers (Akara, Fara, Ormus, Jamella, Malah) heal you as you come to them, the game's way.

#define NPC_MENU_W   440
#define NPC_MENU_ROW 26
#define NPC_MENU_Y   140
#define NPC_RANGE    4.0f // how close you stay to them while trading

enum { NPC_TRADE, NPC_TRAVEL, NPC_HIRE, NPC_REVIVE, NPC_CANCEL, NPC_OFFER = 10 }; // NPC_OFFER + n: hire offer n

static const struct {
    const char *npc;
    int         from_act; // the town they're in (0..4)
    int         to_act;
    const char *label;
} npc_travel[] = {
    {"warriv1", 0, 1, "Go East (Lut Gholein)"     },
    {"warriv2", 1, 0, "Go West (Rogue Encampment)"},
    {"meshif1", 1, 2, "Sail East (Kurast Docks)"  },
    {"meshif2", 2, 1, "Sail West (Lut Gholein)"   },
    {"tyrael2", 3, 4, "Travel to Harrogath"       },
};

static void Roam__Hire(SceneRoam *roam, const HireOffer *offer);
static void Roam__ReviveHireling(SceneRoam *roam);

static const char *const npc_healers[] = {"akara", "fara", "ormus", "jamella", "malah"};

static int Roam__NpcTravel(const Unit *unit) {
    for (size_t t = 0; t < sizeof(npc_travel) / sizeof(npc_travel[0]); t++) {
        if (strcasecmp(npc_travel[t].npc, unit->id) == 0) {
            return (int)t;
        }
    }
    return -1;
}

static bool Roam__NpcHeals(const Unit *unit) {
    for (size_t h = 0; h < sizeof(npc_healers) / sizeof(npc_healers[0]); h++) {
        if (strcasecmp(npc_healers[h], unit->id) == 0) {
            return true;
        }
    }
    return false;
}

static void Roam__CloseShop(SceneRoam *roam) {
    if (GameInventory_ShopOpen(roam->inventory_ui)) {
        GameInventory_SetShop(roam->inventory_ui, NULL, 0);
        if (roam->online && NetClient_ServerKeepsCharacters()) {
            Roam__StoreCharacter(roam);
        }
    }
    roam->shop_npc = -1;
}

// The merchant's stock: rolled the first time you trade with them in this game, kept after that
static ShopStock *Roam__ShopFor(SceneRoam *roam, const Unit *unit) {
    for (int s = 0; s < roam->shop_count; s++) {
        if (strcasecmp(roam->shops[s]->npc, unit->id) == 0) {
            return roam->shops[s];
        }
    }
    if (roam->shop_count >= ROAM_SHOPS) {
        free(roam->shops[0]);
        memmove(roam->shops, roam->shops + 1, sizeof(roam->shops[0]) * (ROAM_SHOPS - 1));
        roam->shop_count--;
    }
    ShopStock *stock = malloc(sizeof(ShopStock));
    FAIL_IF_NULL(stock);
    Shop_Fill(stock, unit->id, roam->character->level, Roam__Difficulty(roam));
    roam->shops[roam->shop_count++] = stock;
    return stock;
}

static void Roam__OpenShop(SceneRoam *roam, const int npc) {
    ShopStock *stock = Roam__ShopFor(roam, &roam->units[npc]);
    roam->shop_npc   = npc;
    GameInventory_SetStash(roam->inventory_ui, false);
    GameInventory_SetShop(roam->inventory_ui, stock, Roam__Difficulty(roam));
    roam->inventory_panel = true;
    roam->skill_panel     = false;
    roam->char_panel      = false;
    roam->panel           = PANEL_NONE;
}

// Come up to them: healers heal, then the menu of what they do
static void Roam__TalkTo(SceneRoam *roam, const int npc) {
    Unit *unit = &roam->units[npc];
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, roam->player.x, roam->player.y);
    if (Roam__NpcHeals(unit) && roam->game) {
        roam->player.hp          = roam->player.max_hp;
        roam->mana               = (float)Character_MaxMana(roam->character);
        roam->stamina            = (float)Character_MaxStamina(roam->character);
        roam->player.poison_left = 0.0f;
    }
    roam->npc_option_count = 0;
    if (Shop_Vendor(unit->id) != NULL) {
        roam->npc_options[roam->npc_option_count++] = NPC_TRADE;
    }
    const int travel = Roam__NpcTravel(unit);
    if (travel >= 0) {
        roam->npc_options[roam->npc_option_count++] = NPC_TRAVEL;
    }
    if (roam->game && Hirelings_Sells(unit->id)) {
        if (roam->character->hireling > 0 && roam->character->hireling_dead) {
            roam->npc_options[roam->npc_option_count++] = NPC_REVIVE;
        }
        roam->npc_options[roam->npc_option_count++] = NPC_HIRE;
    }
    roam->npc_options[roam->npc_option_count++] = NPC_CANCEL;
    roam->npc_menu                              = npc;
}

static bool Roam__CanTalk(const Unit *unit) {
    return unit->npc && !unit->is_object &&
           (Shop_Vendor(unit->id) != NULL || Roam__NpcTravel(unit) >= 0 || Roam__NpcHeals(unit) ||
            Hirelings_Sells(unit->id));
}

static SDL_Rect Roam__NpcMenuRow(const int row) {
    const SDL_Rect rect = {400 - (NPC_MENU_W / 2) + 10, NPC_MENU_Y + 34 + (row * NPC_MENU_ROW), NPC_MENU_W - 20,
                           NPC_MENU_ROW - 4};
    return rect;
}

static const char *Roam__NpcOptionText(SceneRoam *roam, const int option) {
    static char text[160];
    if (option >= NPC_OFFER && option - NPC_OFFER < roam->hire_offer_count) {
        const HireOffer *offer = &roam->hire_offers[option - NPC_OFFER];
        char             name[48];
        Hirelings_Name(offer->row, offer->name, name, sizeof(name));
        snprintf(text, sizeof(text), "%s, level %d, %s: %d gold", name, offer->level, offer->kind, offer->cost);
        return text;
    }
    switch (option) {
    case NPC_HIRE:
        return "Hire";
    case NPC_REVIVE:
        snprintf(text, sizeof(text), "Resurrect your hireling: %d gold",
                 Hirelings_ReviveCost(roam->character->hireling_level));
        return text;
    case NPC_TRADE:
        return "Trade";
    case NPC_TRAVEL: {
        const int travel = Roam__NpcTravel(&roam->units[roam->npc_menu]);
        return travel >= 0 ? npc_travel[travel].label : "Travel";
    }
    default:
        return "Cancel";
    }
}

static void Roam__NpcChoose(SceneRoam *roam, const int option) {
    const int npc  = roam->npc_menu;
    roam->npc_menu = -1;
    if (option >= NPC_OFFER && option - NPC_OFFER < roam->hire_offer_count) {
        Roam__Hire(roam, &roam->hire_offers[option - NPC_OFFER]);
        return;
    }
    switch (option) {
    case NPC_HIRE: {
        // Who's for hire: the same ones while the game lasts
        const Unit *seller = &roam->units[npc];
        if (strcasecmp(roam->hire_seller, seller->id) != 0) {
            snprintf(roam->hire_seller, sizeof(roam->hire_seller), "%s", seller->id);
            roam->hire_offer_count = Hirelings_Offers(seller->id, Roam__Difficulty(roam), roam->character->level,
                                                      roam->hire_offers, HIRE_OFFERS);
        }
        if (roam->hire_offer_count == 0) {
            Roam__ShowMessage(roam, "Nobody's for hire here at this difficulty.");
            break;
        }
        roam->npc_option_count = 0;
        for (int o = 0; o < roam->hire_offer_count; o++) {
            roam->npc_options[roam->npc_option_count++] = NPC_OFFER + o;
        }
        roam->npc_options[roam->npc_option_count++] = NPC_CANCEL;
        roam->npc_menu                              = npc; // the list in the same box
        break;
    }
    case NPC_REVIVE:
        Roam__ReviveHireling(roam);
        break;
    case NPC_TRADE:
        Roam__OpenShop(roam, npc);
        break;
    case NPC_TRAVEL: {
        const int travel = Roam__NpcTravel(&roam->units[npc]);
        if (travel < 0) {
            break;
        }
        const int act = npc_travel[travel].to_act;
        const int wp  = World_WaypointIndex(town_level_ids[act]);
        if (wp >= 0) {
            roam->character->waypoints |= 1ull << wp; // the town's waypoint is known from now on
        }
        Roam__GoToTown(roam, act);
        Roam__UpdateButtons(roam);
        break;
    }
    default:
        break;
    }
}

// Clicks on the menu; true while it's open (it takes the mouse)
static bool Roam__UpdateNpcMenu(SceneRoam *roam, const int mouse_x, const int mouse_y, const bool click) {
    if (roam->npc_menu < 0) {
        return false;
    }
    const Unit *unit = roam->npc_menu < roam->unit_count ? &roam->units[roam->npc_menu] : NULL;
    if (unit == NULL || Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > NPC_RANGE + 2.0f) {
        roam->npc_menu = -1; // walked off
        return false;
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        roam->npc_menu = -1;
        return true;
    }
    const SDL_Point mouse = {mouse_x, mouse_y};
    for (int r = 0; r < roam->npc_option_count && click; r++) {
        const SDL_Rect row = Roam__NpcMenuRow(r);
        if (SDL_PointInRect(&mouse, &row)) {
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
            Roam__NpcChoose(roam, roam->npc_options[r]);
            return true;
        }
    }
    const SDL_Rect panel = {400 - (NPC_MENU_W / 2), NPC_MENU_Y, NPC_MENU_W,
                            40 + (roam->npc_option_count * NPC_MENU_ROW)};
    if (click && !SDL_PointInRect(&mouse, &panel)) {
        roam->npc_menu = -1; // a click elsewhere closes it (and does what it does)
        return false;
    }
    return SDL_PointInRect(&mouse, &panel);
}

static void Roam__DrawNpcMenu(SceneRoam *roam, const int mouse_x, const int mouse_y) {
    if (roam->npc_menu < 0 || roam->npc_menu >= roam->unit_count) {
        return;
    }
    const SDL_Rect panel = {400 - (NPC_MENU_W / 2), NPC_MENU_Y, NPC_MENU_W,
                            40 + (roam->npc_option_count * NPC_MENU_ROW)};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 225);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);
    Label_SetText(roam->item_label, roam->units[roam->npc_menu].name);
    Label_SetColor(roam->item_label, 0xC7, 0xB3, 0x77, 0xFF);
    int w = 0;
    Label_GetSize(roam->item_label, &w, NULL);
    Label_Draw(roam->item_label, 400 - (w / 2), NPC_MENU_Y + 10);
    const SDL_Point mouse = {mouse_x, mouse_y};
    for (int r = 0; r < roam->npc_option_count; r++) {
        const SDL_Rect row = Roam__NpcMenuRow(r);
        Roam__PartyButtonDraw(roam, &row, Roam__NpcOptionText(roam, roam->npc_options[r]),
                              SDL_PointInRect(&mouse, &row));
    }
}

// Every frame: the store closes when you walk away from the merchant or close the inventory
static void Roam__KeepShop(SceneRoam *roam) {
    if (!GameInventory_ShopOpen(roam->inventory_ui)) {
        return;
    }
    const Unit *unit = roam->shop_npc >= 0 && roam->shop_npc < roam->unit_count ? &roam->units[roam->shop_npc] : NULL;
    if (!roam->inventory_panel || roam->char_panel || unit == NULL ||
        Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > NPC_RANGE + 4.0f) {
        Roam__CloseShop(roam);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Hirelings: hired from Kashya, Greiz, Asheara or Qual-Kehk, they follow you from area to area and fight (Rogue Scouts
// and Eastern Sorcerers from afar), and when they die whoever sells them brings them back for gold

static void Roam__HirelingStats(SceneRoam *roam, Unit *unit) {
    const Character *c = roam->character;
    HireStats        stats;
    Hirelings_Stats(c->hireling - 1, c->hireling_level, &stats);
    unit->max_hp        = stats.hp > 0 ? stats.hp : 40;
    unit->defense       = stats.defense;
    unit->attack_rating = stats.attack_rating;
    unit->damage_min    = stats.damage_min > 0 ? stats.damage_min : 1;
    unit->damage_max    = stats.damage_max > unit->damage_min ? stats.damage_max : unit->damage_min + 1;
    unit->level         = c->hireling_level;
    unit->element       = stats.element;
    unit->element_min   = stats.element != 0 ? unit->damage_min / 2 + 1 : 0;
    unit->element_max   = stats.element != 0 ? unit->damage_max / 2 + 1 : 0;
    unit->reach         = stats.ranged ? 9.0f : 0.0f;
    for (int e = ELEMENT_FIRE; e < ELEMENT_COUNT; e++) {
        unit->resist[e] = stats.resist;
    }
}

static int Roam__FindHireling(const SceneRoam *roam) {
    for (int i = 0; i < roam->unit_count; i++) {
        if (roam->units[i].hireling) {
            return i;
        }
    }
    return -1;
}

static void Roam__SpawnHireling(SceneRoam *roam) {
    Character  *c  = roam->character;
    const char *id = Hirelings_MonsterId(c->hireling - 1);
    if (id == NULL || id[0] == '\0') {
        return;
    }
    float x;
    float y;
    Roam__ArrivalSpot(roam->map, roam->player.x + 1.5f, roam->player.y + 1.5f, &x, &y);
    Unit *unit = Roam__AddUnit(roam, "monsters", id, x, y);
    if (unit == NULL) {
        return;
    }
    unit->ally        = true;
    unit->hireling    = true;
    unit->npc         = false;
    unit->experience  = 0;
    unit->treasure[0] = '\0';
    Hirelings_Name(c->hireling - 1, c->hireling_name, unit->name, sizeof(unit->name));
    Roam__HirelingStats(roam, unit);
    unit->hp = c->hireling_life > 0 && c->hireling_life <= unit->max_hp ? c->hireling_life : unit->max_hp;
}

// Every frame: there, alive (coming along), or dead; they grow with you (never above your level)
static void Roam__KeepHireling(SceneRoam *roam) {
    Character *c = roam->character;
    if (!roam->game || c == NULL || c->hireling <= 0 || roam->map == NULL) {
        return;
    }
    const int index = Roam__FindHireling(roam);
    if (c->hireling_dead) {
        return;
    }
    if (index < 0) {
        Roam__SpawnHireling(roam);
        return;
    }
    Unit *unit = &roam->units[index];
    if (!Roam__IsAlive(unit)) {
        c->hireling_dead = true;
        c->hireling_life = 0;
        char text[96];
        snprintf(text, sizeof(text), "%s has died.", unit->name);
        Roam__ShowMessage(roam, text);
        return;
    }
    c->hireling_life = unit->hp;
    if (Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > 40.0f) {
        // Left behind (a waypoint, a town portal, a long run): they catch up
        Roam__ArrivalSpot(roam->map, roam->player.x + 1.5f, roam->player.y + 1.5f, &unit->x, &unit->y);
        Roam__ClearPath(unit);
    }
    if (c->hireling_level + 1 < c->level) {
        c->hireling_level++; // they learn as you go
        const int before = unit->max_hp;
        Roam__HirelingStats(roam, unit);
        unit->hp += unit->max_hp - before;
    }
}

// Their portrait's place: top left, name and life
static void Roam__DrawHireling(SceneRoam *roam) {
    const int index = roam->game ? Roam__FindHireling(roam) : -1;
    if (index < 0 || !Roam__IsAlive(&roam->units[index])) {
        return;
    }
    const Unit    *unit = &roam->units[index];
    const SDL_Rect box  = {8, 26, 120, 30};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &box);
    const int      width = unit->max_hp > 0 ? (unit->hp * (box.w - 8)) / unit->max_hp : 0;
    const SDL_Rect life  = {box.x + 4, box.y + 20, width, 5};
    const int      pct   = unit->max_hp > 0 ? unit->hp * 100 / unit->max_hp : 0;
    SDL_SetRenderDrawColor(sdl_renderer, pct > 50 ? 30 : 200, pct > 50 ? 180 : (pct > 25 ? 160 : 30), 30, 255);
    SDL_RenderFillRect(sdl_renderer, &life);
    Label_SetText(roam->item_label, unit->name);
    Label_SetColor(roam->item_label, 0xFF, 0xFF, 0xFF, 0xFF);
    Label_Draw(roam->item_label, box.x + 4, box.y + 3);
}

static void Roam__Hire(SceneRoam *roam, const HireOffer *offer) {
    Character *c = roam->character;
    if (c->gold < offer->cost) {
        Roam__ShowMessage(roam, "You don't have enough gold for that.");
        return;
    }
    const int old = Roam__FindHireling(roam);
    if (old >= 0) {
        Unit *gone     = &roam->units[old]; // the one you had leaves
        gone->hp       = 0;
        gone->vanished = true;
        gone->hireling = false;
        Roam__SetState(gone, UNIT_DEAD, SDL_GetTicks64());
    }
    c->gold                -= offer->cost;
    c->hireling             = offer->row + 1;
    c->hireling_level       = offer->level;
    c->hireling_name        = offer->name;
    c->hireling_life        = 0; // (full)
    c->hireling_dead        = false;
    c->hireling_experience  = 0;
    Sounds_Play("item_gold", SOUND_KIND_SFX);
    Roam__SpawnHireling(roam);
}

static void Roam__ReviveHireling(SceneRoam *roam) {
    Character *c    = roam->character;
    const int  cost = Hirelings_ReviveCost(c->hireling_level);
    if (c->gold < cost) {
        Roam__ShowMessage(roam, "You don't have enough gold for that.");
        return;
    }
    const int old = Roam__FindHireling(roam);
    if (old >= 0) {
        roam->units[old].hireling = false; // the body stays where it fell
    }
    c->gold          -= cost;
    c->hireling_dead  = false;
    c->hireling_life  = 0;
    Sounds_Play("item_gold", SOUND_KIND_SFX);
    Roam__SpawnHireling(roam);
}
