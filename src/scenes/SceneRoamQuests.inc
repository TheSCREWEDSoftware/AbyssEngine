// SceneRoam: quests (included by SceneRoam.c). Twice a second the open ones are checked: the boss dead, the area
// cleared, the place reached. Bosses the area's presets don't place are put there. A quest with someone to reward it
// waits for you to go back to them (their menu has the reward); the others are done on the spot.

#define QUEST_CHECK_MS 500

static uint8_t *Roam__QuestState(SceneRoam *roam, const int index) {
    return &roam->character->quests[Roam__Difficulty(roam)][index];
}

// The area of a level in the map loaded now (outdoors several are joined), NULL: not here
static const MapArea *Roam__MapAreaFor(const SceneRoam *roam, const int level_id) {
    for (int a = 0; a < roam->map->area_count; a++) {
        if (roam->map->areas[a].level_id == level_id) {
            return &roam->map->areas[a];
        }
    }
    static MapArea whole; // a map that's one area (caves, dungeons): all of it
    if (roam->map->area_count == 0 && roam->area_level == level_id) {
        memset(&whole, 0, sizeof(whole));
        whole.level_id = level_id;
        whole.x1       = roam->map->width - 1;
        whole.y1       = roam->map->height - 1;
        return &whole;
    }
    return NULL;
}

// The level the player is in
static int Roam__LevelHere(const SceneRoam *roam) {
    const MapArea *here = Map_AreaAt(roam->map, roam->player.x, roam->player.y);
    return here != NULL ? here->level_id : roam->area_level;
}

static bool Roam__InArea(const MapArea *area, const float x, const float y) {
    const int tx = (int)x / MAP_SUBTILES;
    const int ty = (int)y / MAP_SUBTILES;
    return tx >= area->x0 && tx <= area->x1 && ty >= area->y0 && ty <= area->y1;
}

static int Roam__QuestBoss(const SceneRoam *roam, const Quest *quest) {
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!unit->is_object && !unit->ally && strcmp(unit->id, quest->monster) == 0 &&
            (quest->boss_name == NULL || strcmp(unit->name, quest->boss_name) == 0)) {
            return i;
        }
    }
    return -1;
}

// A boss the presets leave out, in the middle of its area (the game's superuniques are tougher than their kind)
static void Roam__SpawnQuestBoss(SceneRoam *roam, const Quest *quest, const MapArea *area) {
    // The middle of the area, or a quarter of the way in from a corner: whichever is furthest from you
    const float w        = (float)(area->x1 - area->x0 + 1) * MAP_SUBTILES;
    const float h        = (float)(area->y1 - area->y0 + 1) * MAP_SUBTILES;
    const float left     = (float)area->x0 * MAP_SUBTILES;
    const float top      = (float)area->y0 * MAP_SUBTILES;
    const float at[5][2] = {
        {0.5f,  0.5f },
        {0.25f, 0.25f},
        {0.75f, 0.25f},
        {0.25f, 0.75f},
        {0.75f, 0.75f}
    };
    float x    = left + (w / 2.0f);
    float y    = top + (h / 2.0f);
    float best = -1.0f;
    for (int c = 0; c < 5; c++) {
        float sx;
        float sy;
        Roam__ArrivalSpot(roam->map, left + (w * at[c][0]), top + (h * at[c][1]), &sx, &sy);
        const float far = Roam__Distance(sx, sy, roam->player.x, roam->player.y);
        if (Map_IsWalkable(roam->map, (int)sx, (int)sy) && far > best) {
            best = far;
            x    = sx;
            y    = sy;
        }
    }
    Unit *boss = Roam__AddUnit(roam, "monsters", quest->monster, x, y);
    if (boss == NULL) {
        return;
    }
    if (quest->boss_name != NULL) {
        snprintf(boss->name, sizeof(boss->name), "%s", quest->boss_name);
        boss->max_hp     *= 4;
        boss->hp          = boss->max_hp;
        boss->damage_min += boss->damage_min / 2;
        boss->damage_max += boss->damage_max / 2;
        boss->experience *= 3;
        boss->level      += 3;
    }
    if (quest->reward == REWARD_TREASURE && quest->treasure != NULL) {
        snprintf(boss->treasure, sizeof(boss->treasure), "%s", quest->treasure);
    }
}

static void Roam__QuestDone(SceneRoam *roam, const int index);
static bool Roam__TakeItem(SceneRoam *roam, const char *code);
static void Roam__PlaceQuestObjects(SceneRoam *roam);

static void Roam__CheckQuests(SceneRoam *roam, const uint64_t now) {
    if (!roam->game || roam->character == NULL || roam->map == NULL || now < roam->quest_check_at) {
        return;
    }
    roam->quest_check_at = now + QUEST_CHECK_MS;
    if (roam->quest_objects_key != Roam__MapKey(roam) + 1) {
        roam->quest_objects_key = Roam__MapKey(roam) + 1;
        Roam__PlaceQuestObjects(roam);
    }
    const int level_here = Roam__LevelHere(roam);
    for (int q = 0; q < QUEST_COUNT; q++) {
        const Quest *quest = Quests_Get(q);
        if (*Roam__QuestState(roam, q) != QUEST_OPEN || quest->goal == QUEST_NOT_YET) {
            continue;
        }
        const MapArea *area = Roam__MapAreaFor(roam, quest->level);
        if (area == NULL) {
            continue;
        }
        switch (quest->goal) {
        case QUEST_REACH:
            if (level_here == quest->level) {
                Roam__QuestDone(roam, q);
            }
            break;
        case QUEST_KILL: {
            const int boss = Roam__QuestBoss(roam, quest);
            if (boss < 0) {
                if (quest->spawn && !roam->online) { // (multiplayer games share their monsters: not yet)
                    Roam__SpawnQuestBoss(roam, quest, area);
                }
            } else if (!Roam__IsAlive(&roam->units[boss])) {
                Roam__QuestDone(roam, q);
            }
            break;
        }
        case QUEST_CLEAR: {
            if (level_here != quest->level) {
                break;
            }
            int left = 0;
            for (int i = 0; i < roam->unit_count; i++) {
                const Unit *unit  = &roam->units[i];
                left             += Roam__IsAlive(unit) && Roam__IsCombatant(unit) && !unit->npc && !unit->ally &&
                                Roam__InArea(area, unit->x, unit->y)
                                        ? 1
                                        : 0;
            }
            if (left == 0) {
                Roam__QuestDone(roam, q);
            } else if (left <= 5 && left != roam->quest_left) {
                char text[64];
                snprintf(text, sizeof(text), left == 1 ? "One monster left." : "Monsters remaining: %d", left);
                Roam__ShowMessage(roam, text);
            }
            roam->quest_left = left;
            break;
        }
        default:
            break;
        }
    }
}

// A plain weapon or armor to work on: the one in your hand, else your body armor
static Item *Roam__QuestItem(SceneRoam *roam) {
    Item *slots[2] = {&roam->character->equipped[EQUIP_RIGHT_ARM], &roam->character->equipped[EQUIP_TORSO]};
    for (int s = 0; s < 2; s++) {
        if (slots[s]->code[0] != '\0' && slots[s]->quality == ITEM_NORMAL && slots[s]->sockets == 0) {
            return slots[s];
        }
    }
    return NULL;
}

// The reward; false when it can't be had yet (it stays waiting)
static bool Roam__GiveQuestReward(SceneRoam *roam, const int index) {
    const Quest *quest = Quests_Get(index);
    Character   *c     = roam->character;
    char         text[128];
    text[0] = '\0';
    switch (quest->reward) {
    case REWARD_SKILL_POINTS:
        c->skill_points += quest->amount;
        snprintf(text, sizeof(text), "You get %d skill point%s.", quest->amount, quest->amount == 1 ? "" : "s");
        break;
    case REWARD_STAT_POINTS:
        c->stat_points += quest->amount;
        snprintf(text, sizeof(text), "You get %d stat points.", quest->amount);
        break;
    case REWARD_RESIST:
        c->quest_resist += quest->amount;
        snprintf(text, sizeof(text), "All your resistances are %d higher for good.", quest->amount);
        break;
    case REWARD_LIFE:
        if (quest->act == 2 && quest->number == 4 && !Roam__TakeItem(roam, "g34")) {
            Roam__ShowMessage(roam, "Bring Alkor the Golden Bird.");
            return false;
        }
        c->quest_life += quest->amount;
        snprintf(text, sizeof(text), "You have %d more life for good.", quest->amount);
        break;
    case REWARD_IMBUE:
    case REWARD_SOCKET: {
        Item *item = Roam__QuestItem(roam);
        if (item == NULL) {
            Roam__ShowMessage(roam, "Wear a plain weapon (or body armor) without sockets, then come back.");
            return false;
        }
        if (quest->reward == REWARD_IMBUE) {
            Affixes_MakeRare(item, c->level + 4);
            snprintf(text, sizeof(text), "Your %s is imbued with magic.", Items_Find(item->code)->name);
        } else {
            const ItemBase *base = Items_Find(item->code);
            item->sockets =
                (uint8_t)(base != NULL && base->max_sockets > 0 ? (base->max_sockets < 4 ? base->max_sockets : 4) : 1);
            snprintf(text, sizeof(text), "Sockets added to your %s.", base != NULL ? base->name : "item");
        }
        Roam__ApplyCharacter(roam);
        break;
    }
    case REWARD_EXPERIENCE: {
        const uint32_t next = Character_ExperienceFor(c->level + 1);
        const uint32_t gap  = next > c->experience ? next - c->experience : 0;
        Roam__GainExperienceAmount(roam, (int)(gap + (gap / 2) + 1));
        snprintf(text, sizeof(text), "The Ancients' blessing: you gain experience.");
        break;
    }
    case REWARD_RING: {
        if (quest->act == 2 && quest->number == 3 && !Roam__TakeItem(roam, "g33")) {
            Roam__ShowMessage(roam, "Bring Ormus the Gidbinn.");
            return false;
        }
        Item ring = Inventory_NewItem("rin");
        for (int tries = 0; tries < 50 && ring.quality == ITEM_NORMAL; tries++) {
            Affixes_Roll(&ring, c->level + 5);
        }
        if (!Inventory_Add(c, &ring)) {
            Roam__ShowMessage(roam, "Make room in your inventory for your reward.");
            return false;
        }
        snprintf(text, sizeof(text), "You're given a ring.");
        break;
    }
    case REWARD_FREE_HIRE: {
        if (quest->act == 4) {
            // Rescue on Mount Arreat: the runes Ral, Ort and Tal (Qual-Kehk's thanks), and a free hireling
            static const char *const runes[3] = {"r08", "r09", "r07"};
            for (int r = 0; r < 3; r++) {
                Item rune = Inventory_NewItem(runes[r]);
                if (!Inventory_Add(c, &rune)) {
                    Stash_Add(c, &rune);
                }
            }
        }
        HireOffer offer;
        if (Hirelings_Offers(roam->units[roam->npc_menu >= 0 ? roam->npc_menu : 0].id, Roam__Difficulty(roam), c->level,
                             &offer, 1) == 1) {
            offer.cost = 0;
            Roam__Hire(roam, &offer);
            snprintf(text, sizeof(text), "A hireling joins you, free of charge.");
        }
        break;
    }
    default:
        break;
    }
    if (text[0] != '\0') {
        Roam__ShowMessage(roam, text);
    }
    if (quest->reward == REWARD_LIFE || quest->reward == REWARD_RESIST) {
        Roam__ApplyCharacter(roam); // (more life, resistances: on the globes and the character screen now)
    }
    Sounds_Play("cursor_questdone", SOUND_KIND_UI);
    return true;
}

static void Roam__QuestDone(SceneRoam *roam, const int index) {
    const Quest *quest = Quests_Get(index);
    char         text[128];
    if (quest->giver != NULL) {
        *Roam__QuestState(roam, index) = QUEST_REWARD;
        snprintf(text, sizeof(text), "%s: %s", Quests_Name(index), Quests_Step(index, QUEST_REWARD));
        Roam__ShowMessage(roam, text);
        Sounds_Play("cursor_questdone", SOUND_KIND_UI);
    } else {
        *Roam__QuestState(roam, index) = QUEST_COMPLETE;
        snprintf(text, sizeof(text), "Quest completed: %s", Quests_Name(index));
        Roam__ShowMessage(roam, text);
        Roam__GiveQuestReward(roam, index);
    }
    Roam__StoreCharacter(roam);
}

// The quests waiting for this one to reward them
static int Roam__QuestsFor(SceneRoam *roam, const Unit *unit, int *out, const int most) {
    int count = 0;
    for (int q = 0; q < QUEST_COUNT && count < most && roam->game; q++) {
        const Quest *quest = Quests_Get(q);
        if (quest->giver != NULL && strcasecmp(quest->giver, unit->id) == 0 &&
            *Roam__QuestState(roam, q) == QUEST_REWARD) {
            out[count++] = q;
        }
    }
    return count;
}

static void Roam__ClaimQuest(SceneRoam *roam, const int index) {
    if (*Roam__QuestState(roam, index) == QUEST_REWARD && Roam__GiveQuestReward(roam, index)) {
        *Roam__QuestState(roam, index) = QUEST_COMPLETE;
        Roam__StoreCharacter(roam);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The quest log (Q): the acts' quests on the left, with where each one stands

static bool Roam__UpdateQuestPanel(SceneRoam *roam, const int mouse_x, const int mouse_y, const bool click) {
    if (!roam->quest_panel) {
        return false;
    }
    if (mouse_x >= GAME_PANEL_W || mouse_y >= GAME_PANEL_H) {
        return false;
    }
    if (click && mouse_y >= 34 && mouse_y < 58) {
        const int act   = (mouse_x - 10) / 60;
        roam->quest_act = act >= 0 && act < 5 ? act : roam->quest_act;
    }
    return true;
}

static void Roam__DrawQuestPanel(SceneRoam *roam) {
    if (!roam->quest_panel || !roam->game) {
        return;
    }
    const SDL_Rect panel = {0, 0, GAME_PANEL_W, GAME_PANEL_H};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 10, 8, 6, 235);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);
    static const char *difficulty_names[3] = {"Normal", "Nightmare", "Hell"};
    char               text[96];
    snprintf(text, sizeof(text), "Quests  (%s)", difficulty_names[Roam__Difficulty(roam)]);
    Label_SetText(roam->item_label, text);
    Label_SetColor(roam->item_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_Draw(roam->item_label, 12, 10);
    for (int a = 0; a < 5; a++) {
        const SDL_Rect tab = {10 + (a * 60), 34, 56, 22};
        SDL_SetRenderDrawColor(sdl_renderer, a == roam->quest_act ? 70 : 30, a == roam->quest_act ? 60 : 26,
                               a == roam->quest_act ? 35 : 18, 255);
        SDL_RenderFillRect(sdl_renderer, &tab);
        SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
        SDL_RenderDrawRect(sdl_renderer, &tab);
        snprintf(text, sizeof(text), "Act %d", a + 1);
        Label_SetText(roam->item_label, text);
        Label_SetColor(roam->item_label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(roam->item_label, tab.x + 8, tab.y + 4);
    }
    int row = 0;
    for (int q = 0; q < QUEST_COUNT; q++) {
        const Quest *quest = Quests_Get(q);
        if (quest->act != roam->quest_act) {
            continue;
        }
        const int state = *Roam__QuestState(roam, q);
        const int y     = 68 + (row++ * 60);
        Label_SetText(roam->item_label, Quests_Name(q));
        if (state == QUEST_COMPLETE) {
            Label_SetColor(roam->item_label, 0xC7, 0xB3, 0x77, 0xFF);
        } else if (state == QUEST_REWARD) {
            Label_SetColor(roam->item_label, 0x40, 0xFF, 0x40, 0xFF);
        } else {
            Label_SetColor(roam->item_label, 0xFF, 0xFF, 0xFF, 0xFF);
        }
        Label_Draw(roam->item_label, 14, y);
        // The step, wrapped onto up to three short lines
        const char *step = Quests_Step(q, state);
        char        line[64];
        int         lines = 0;
        while (*step != '\0' && lines < 3) {
            // As many words as fit in the panel
            int take = 0;
            for (int end = 1; step[end - 1] != '\0' && end < 63; end++) {
                if (step[end] != ' ' && step[end] != '\0') {
                    continue;
                }
                snprintf(line, sizeof(line), "%.*s", end, step);
                Label_SetText(roam->item_label, line);
                if (Label_MeasureText(roam->item_label, line) > GAME_PANEL_W - 40 && take > 0) {
                    break;
                }
                take = end;
            }
            take = take > 0 ? take : (int)strlen(step);
            snprintf(line, sizeof(line), "%.*s", take, step);
            for (char *c = line; *c != '\0'; c++) {
                *c = *c == '\n' ? ' ' : *c;
            }
            Label_SetText(roam->item_label, line);
            Label_SetColor(roam->item_label, 0xA0, 0xA0, 0xA0, 0xFF);
            Label_Draw(roam->item_label, 24, y + 16 + (lines * 12));
            step += take;
            while (*step == ' ') {
                step++;
            }
            lines++;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Quest items: found in the quests' chests and on certain monsters, put together in the Horadric Cube (cubemain.txt),
// used on the quests' objects. The Horadric Staff (the Cube in the Halls of the Dead, the Staff of Kings in the Maggot
// Lair, the Amulet of the Viper from the Claw Viper Temple's altar; the staff goes in the orifice in Tal Rasha's Tomb),
// the Tainted Sun (that altar), Khalim's Will (his Eye, Brain and Heart in chests, his Flail from the High Council; it
// smashes the Compelling Orb in Travincal), the Golden Bird (a Jade Figurine from a unique monster in Act 3: Meshif
// makes it the Golden Bird, Alkor rewards it) and the Blackened Temple (the three of the High Council)

static int Roam__QuestIndex(const int act, const int number) {
    for (int q = 0; q < QUEST_COUNT; q++) {
        if (Quests_Get(q)->act == act && Quests_Get(q)->number == number) {
            return q;
        }
    }
    return -1;
}

static bool Roam__QuestOpen(SceneRoam *roam, const int act, const int number) {
    const int q = Roam__QuestIndex(act, number);
    return q >= 0 && *Roam__QuestState(roam, q) == QUEST_OPEN;
}

// Where an item is among what you have: carried, worn or in the cube
static Item *Roam__FindItem(SceneRoam *roam, const char *code) {
    Character *c = roam->character;
    for (int i = 0; i < c->inventory_count; i++) {
        if (strcmp(c->inventory[i].item.code, code) == 0) {
            return &c->inventory[i].item;
        }
    }
    for (int s = 0; s < EQUIP_COUNT; s++) {
        if (strcmp(c->equipped[s].code, code) == 0) {
            return &c->equipped[s];
        }
    }
    for (int i = 0; i < c->cube_count; i++) {
        if (strcmp(c->cube[i].item.code, code) == 0) {
            return &c->cube[i].item;
        }
    }
    for (int i = 0; i < c->stash_count; i++) {
        if (strcmp(c->stash[i].item.code, code) == 0) {
            return &c->stash[i].item;
        }
    }
    return NULL;
}

static bool Roam__TakeItem(SceneRoam *roam, const char *code) {
    Character *c = roam->character;
    for (int i = 0; i < c->inventory_count; i++) {
        if (strcmp(c->inventory[i].item.code, code) == 0) {
            Inventory_Remove(c, i);
            return true;
        }
    }
    for (int s = 0; s < EQUIP_COUNT; s++) {
        if (strcmp(c->equipped[s].code, code) == 0) {
            memset(&c->equipped[s], 0, sizeof(Item));
            Roam__ApplyCharacter(roam);
            return true;
        }
    }
    for (int i = 0; i < c->cube_count; i++) {
        if (strcmp(c->cube[i].item.code, code) == 0) {
            Cube_Remove(c, i);
            return true;
        }
    }
    return false;
}

// One quest item on the ground (a game drops each once while you don't have it)
static void Roam__DropQuestItem(SceneRoam *roam, const char *code, const float x, const float y) {
    if (Items_Find(code) == NULL || Roam__FindItem(roam, code) != NULL) {
        return;
    }
    for (int g = 0; g < roam->ground_count; g++) {
        if (strcmp(roam->ground[g].item.code, code) == 0) {
            return; // already lying somewhere
        }
    }
    Item  item = Inventory_NewItem(code);
    float spot_x;
    float spot_y;
    Roam__FreeSpotNear(roam, x, y, &spot_x, &spot_y);
    if (!roam->online) {
        Roam__AddGroundItem(roam, &item, Roam__MapKey(roam), spot_x, spot_y, true, 0);
    } else {
        Roam__PutOnGround(roam, &item, spot_x, spot_y);
    }
}

// The quests' objects; true when this one was one of them (and it's been dealt with)
static bool Roam__QuestObject(SceneRoam *roam, Unit *unit) {
    if (!roam->game || roam->character == NULL) {
        return false;
    }
    const int fn = unit->object.operate_fn;
    switch (fn) {
    case 39: // the Horadric Cube's chest (Halls of the Dead)
        Roam__DropQuestItem(roam, CUBE_ITEM, unit->x, unit->y);
        return true;
    case 41: // the Staff of Kings' chest (Maggot Lair)
        Roam__DropQuestItem(roam, "msf", unit->x, unit->y);
        return true;
    case 24: { // the Tainted Sun's altar (Claw Viper Temple): the Amulet of the Viper, and the sun comes back
        Roam__DropQuestItem(roam, "vip", unit->x, unit->y);
        const int q = Roam__QuestIndex(1, 3);
        if (q >= 0 && *Roam__QuestState(roam, q) == QUEST_OPEN) {
            Roam__QuestDone(roam, q);
        }
        return true;
    }
    case 25: { // the orifice in Tal Rasha's Tomb: the Horadric Staff goes in
        const int q = Roam__QuestIndex(1, 2);
        if (q < 0 || *Roam__QuestState(roam, q) != QUEST_OPEN) {
            return true;
        }
        if (!Roam__TakeItem(roam, "hst")) {
            Roam__ShowMessage(roam, Roam__FindItem(roam, "msf") != NULL || Roam__FindItem(roam, "vip") != NULL
                                        ? "The Horadric Staff goes here: put the Staff of Kings and the Amulet of the "
                                          "Viper together in the Horadric Cube."
                                        : "Something belongs here: the Horadric Staff.");
            return true;
        }
        Roam__QuestDone(roam, q);
        Roam__ShowMessage(roam, "The Horadric Staff is in place: the way to Duriel is open.");
        return true;
    }
    case 57:
    case 58:
    case 59: { // Khalim's chests: his Eye (Spider Cavern), Brain (Flayer Dungeon), Heart (the Sewers)
        const int   level = Roam__LevelHere(roam);
        const char *piece = level == 85 ? "qey" : (level == 91 ? "qbr" : (level == 93 ? "qhr" : NULL));
        if (piece == NULL) {
            piece = fn == 57 ? "qhr" : (fn == 58 ? "qey" : "qbr");
        }
        Roam__DropQuestItem(roam, piece, unit->x, unit->y);
        return true;
    }
    case 53: { // the Compelling Orb (Travincal): Khalim's Will smashes it
        const int q = Roam__QuestIndex(2, 2);
        if (q < 0 || *Roam__QuestState(roam, q) != QUEST_OPEN) {
            return true;
        }
        if (!Roam__TakeItem(roam, "qf2")) {
            Roam__ShowMessage(roam, "Only Khalim's Will can break the Compelling Orb.");
            return true;
        }
        Roam__QuestDone(roam, q);
        Roam__ShowMessage(roam, "The Compelling Orb shatters: the way into the Durance of Hate is open.");
        return true;
    }
    default:
        break;
    }
    if (unit->object.row == 251) { // the Gidbinn's altar (Flayer Jungle): the Gidbinn, for Ormus
        Roam__DropQuestItem(roam, "g33", unit->x, unit->y);
        if (Roam__QuestOpen(roam, 2, 3)) {
            Roam__QuestDone(roam, Roam__QuestIndex(2, 3));
        }
        return true;
    }
    if (fn == 65) { // the Ancients' altar (Arreat Summit): Talic, Madawc and Korlic come to test you
        if (!Roam__QuestOpen(roam, 4, 5) || roam->ancients_called) {
            return true;
        }
        roam->ancients_called                   = true;
        static const char *const ancients[3][2] = {
            {"ancientbarb1", "Talic the Defender"  },
            {"ancientbarb2", "Madawc the Guardian" },
            {"ancientbarb3", "Korlic the Protector"},
        };
        for (int a = 0; a < 3; a++) {
            float x;
            float y;
            Roam__ArrivalSpot(roam->map, unit->x + (float)((a - 1) * 4), unit->y + 5.0f, &x, &y);
            Unit *ancient = Roam__AddUnit(roam, "monsters", ancients[a][0], x, y);
            if (ancient != NULL) {
                snprintf(ancient->name, sizeof(ancient->name), "%s", ancients[a][1]);
                ancient->max_hp     *= 4;
                ancient->hp          = ancient->max_hp;
                ancient->experience *= 3;
                ancient->rank        = RANK_SUPER_UNIQUE;
            }
        }
        Roam__ShowMessage(roam, "The Ancients rise to test your worth.");
        return true;
    }
    return false;
}

// A monster died: what the quests take from it
static void Roam__QuestKill(SceneRoam *roam, const Unit *victim) {
    if (!roam->game || roam->character == NULL || victim->is_object || victim->ally) {
        return;
    }
    const bool council = strncmp(victim->id, "councilmember", 13) == 0;
    if (council && Roam__QuestOpen(roam, 2, 2) && Roam__FindItem(roam, "qf2") == NULL) {
        Roam__DropQuestItem(roam, "qf1", victim->x, victim->y); // Khalim's Flail
    }
    if (council && victim->rank == RANK_SUPER_UNIQUE && Roam__QuestOpen(roam, 2, 5)) {
        if (++roam->council_killed >= 3) {
            Roam__QuestDone(roam, Roam__QuestIndex(2, 5)); // the Blackened Temple: the three of the High Council
        }
    }
    if (strcmp(victim->id, "prisondoor") == 0 && Roam__QuestOpen(roam, 4, 2)) {
        roam->prisons_broken++;
        char text[96];
        snprintf(text, sizeof(text), "You free three of Qual-Kehk's men (%d of 15).", roam->prisons_broken * 3);
        Roam__ShowMessage(roam, text);
        if (roam->prisons_broken >= 5) {
            Roam__QuestDone(roam, Roam__QuestIndex(4, 2));
        }
    }
    if (strncmp(victim->id, "ancientbarb", 11) == 0 && roam->ancients_called && Roam__QuestOpen(roam, 4, 5)) {
        int left = 0;
        for (int i = 0; i < roam->unit_count; i++) {
            left += strncmp(roam->units[i].id, "ancientbarb", 11) == 0 && roam->units[i].hp > 0 &&
                            Roam__IsAlive(&roam->units[i])
                        ? 1
                        : 0;
        }
        if (left == 0) {
            Roam__QuestDone(roam, Roam__QuestIndex(4, 5)); // Rite of Passage: the way into the Worldstone Keep
        }
    }
    if (Roam__Act(roam) == 2 && (victim->rank == RANK_UNIQUE || victim->rank == RANK_SUPER_UNIQUE) &&
        Roam__QuestOpen(roam, 2, 4) && Roam__FindItem(roam, "g34") == NULL) {
        Roam__DropQuestItem(roam, "j34", victim->x, victim->y); // the Jade Figurine
    }
}

// Meshif and the Jade Figurine: he gives you the Golden Bird for it (the quest then waits at Alkor)
static bool Roam__QuestTalk(SceneRoam *roam, const Unit *unit) {
    if (!roam->game || strncmp(unit->id, "meshif", 6) != 0 || !Roam__QuestOpen(roam, 2, 4) ||
        !Roam__TakeItem(roam, "j34")) {
        return false;
    }
    Item bird = Inventory_NewItem("g34");
    if (!Inventory_Add(roam->character, &bird)) {
        Item figurine = Inventory_NewItem("j34");
        Inventory_Add(roam->character, &figurine);
        Roam__ShowMessage(roam, "Make room in your inventory first.");
        return true;
    }
    Roam__QuestDone(roam, Roam__QuestIndex(2, 4));
    Roam__ShowMessage(roam, "Meshif gives you a Golden Bird for the figurine. Alkor will want to see it.");
    return true;
}

// Rescue on Mount Arreat: the five prisons in the Frigid Highlands (their doors are monsters to break)
static void Roam__PlacePrisons(SceneRoam *roam) {
    const MapArea *area = Roam__MapAreaFor(roam, 111);
    if (area == NULL || !Roam__QuestOpen(roam, 4, 2)) {
        return;
    }
    const float left = (float)area->x0 * MAP_SUBTILES;
    const float top  = (float)area->y0 * MAP_SUBTILES;
    const float w    = (float)(area->x1 - area->x0 + 1) * MAP_SUBTILES;
    const float h    = (float)(area->y1 - area->y0 + 1) * MAP_SUBTILES;
    for (int p = roam->prisons_broken; p < 5; p++) {
        float x;
        float y;
        Roam__ArrivalSpot(roam->map, left + (w * (0.15f + (0.17f * (float)p))), top + (h * (p % 2 == 0 ? 0.3f : 0.7f)),
                          &x, &y);
        Unit *door = Roam__AddUnit(roam, "monsters", "prisondoor", x, y);
        if (door != NULL) {
            snprintf(door->name, sizeof(door->name), "Prison Door");
            door->damage_min  = 0;
            door->damage_max  = 0;
            door->speed       = 0.0f;
            door->experience  = 0;
            door->treasure[0] = '\0';
        }
    }
}

// The quests' objects that this engine's version of an area doesn't place (the Claw Viper Temple's altar, the Staff of
// Kings' chest, the Compelling Orb): put far from where you come in, once a map
// Town folk the game puts in by its code rather than the town's map: Hratli on the Kurast Docks, Larzuk in Harrogath,
// and Anya there once she's been rescued (Prison of Ice): next to someone who's always there
static void Roam__PlaceTownFolk(SceneRoam *roam) {
    static const struct {
        int         level;
        const char *id;
        const char *beside; // who they stand near
        float       dx;
        float       dy;
        int         after_act; // shown once this quest is done (act, number), 0: always
        int         after_number;
    } folk[] = {
        {75,  "hratli", "asheara", 10.0f,  4.0f, 0, 0},
        {109, "larzuk", "malah",   -10.0f, 8.0f, 0, 0},
        {109, "drehya", "malah",   6.0f,   6.0f, 4, 3},
    };
    for (size_t f = 0; f < sizeof(folk) / sizeof(folk[0]); f++) {
        if (Roam__MapAreaFor(roam, folk[f].level) == NULL) {
            continue;
        }
        if (folk[f].after_act > 0) {
            const int q = Roam__QuestIndex(folk[f].after_act, folk[f].after_number);
            if (!roam->game || q < 0 || *Roam__QuestState(roam, q) == QUEST_OPEN) {
                continue;
            }
        }
        const Unit *near  = NULL;
        bool        there = false;
        for (int i = 0; i < roam->unit_count; i++) {
            there = there || strcasecmp(roam->units[i].id, folk[f].id) == 0;
            near  = strcasecmp(roam->units[i].id, folk[f].beside) == 0 ? &roam->units[i] : near;
        }
        if (there || near == NULL) {
            continue;
        }
        float x;
        float y;
        Roam__ArrivalSpot(roam->map, near->x + folk[f].dx, near->y + folk[f].dy, &x, &y);
        Unit *unit = Roam__AddUnit(roam, "monsters", folk[f].id, x, y);
        if (unit != NULL) {
            unit->npc = true;
        }
    }
}

static void Roam__PlaceQuestObjects(SceneRoam *roam) {
    Roam__PlacePrisons(roam);
    Roam__PlaceTownFolk(roam);
    static const struct {
        int         level;
        int         fn;
        int         row; // objects.txt
        const char *token;
    } needed[] = {
        {61, 24, 149, "za"}, // the Tainted Sun's altar
        {64, 41, 356, "xk"}, // the Staff of Kings' chest
        {83, 53, 404, "55"}, // the Compelling Orb
    };
    for (size_t n = 0; n < sizeof(needed) / sizeof(needed[0]); n++) {
        const MapArea *area = Roam__MapAreaFor(roam, needed[n].level);
        if (area == NULL) {
            continue;
        }
        bool there = false;
        for (int i = 0; i < roam->unit_count && !there; i++) {
            there = roam->units[i].is_object && roam->units[i].object.operate_fn == needed[n].fn;
        }
        if (there) {
            continue;
        }
        // The walkable spot furthest from you, of a few tried across the area
        const float left = (float)area->x0 * MAP_SUBTILES;
        const float top  = (float)area->y0 * MAP_SUBTILES;
        const float w    = (float)(area->x1 - area->x0 + 1) * MAP_SUBTILES;
        const float h    = (float)(area->y1 - area->y0 + 1) * MAP_SUBTILES;
        float       x    = -1.0f;
        float       y    = -1.0f;
        float       best = -1.0f;
        for (int t = 0; t < 40; t++) {
            float sx;
            float sy;
            Roam__ArrivalSpot(roam->map, left + (w * (float)Roam__Roll(10, 90) / 100.0f),
                              top + (h * (float)Roam__Roll(10, 90) / 100.0f), &sx, &sy);
            const float far = Roam__Distance(sx, sy, roam->player.x, roam->player.y);
            if (Map_IsWalkable(roam->map, (int)sx, (int)sy) && far > best) {
                best = far;
                x    = sx;
                y    = sy;
            }
        }
        if (best < 0.0f) {
            continue;
        }
        Unit *unit = Roam__AddUnit(roam, "objects", needed[n].token, x, y);
        if (unit != NULL && Objects_Get(needed[n].row, &unit->object)) {
            unit->is_object = true;
            snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.idle_mode);
            snprintf(unit->name, sizeof(unit->name), "%s", unit->object.name != NULL ? unit->object.name : "");
        }
    }
}
