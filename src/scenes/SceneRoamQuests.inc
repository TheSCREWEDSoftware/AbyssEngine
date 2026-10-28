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
        // (multiplayer: the first spot that will do, the same in everyone's game)
        const float far = roam->online ? (float)(5 - c) : Roam__Distance(sx, sy, roam->player.x, roam->player.y);
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
                if (quest->spawn &&
                    !roam->online) { // (multiplayer: already put in with the map, Roam__PlaceQuestObjects)
                    Roam__SpawnQuestBoss(roam, quest, area);
                }
            } else if (!Roam__IsAlive(&roam->units[boss]) &&
                       !roam->units[boss].burrowed) { // (not just under the ground)
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
                left             += (Roam__IsAlive(unit) || unit->burrowed) && Roam__IsCombatant(unit) && !unit->npc &&
                                !unit->ally && Roam__InArea(area, unit->x, unit->y)
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
        Roam__GainExperienceAmount(roam, (int)(gap + (gap / 2) + 1), -1);
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

static int Roam__QuestIndex(int act, int number);

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
    // Diablo's and Baal's ends: their films
    if (index == Roam__QuestIndex(3, 2)) {
        Roam__PlayFilm(roam, 3);
    } else if (index == Roam__QuestIndex(4, 6)) {
        Roam__PlayFilm(roam, 4);
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

// Like the game's quest log: a quest clicked plays again what its giver said when they set it (their words in the
// box at the top and their voice), "akara_act1_q1_init" and its string A1Q1InitAkara. False when there's none.
static bool Roam__ReplayQuest(SceneRoam *roam, const int index) {
    const Quest *quest = Quests_Get(index);
    if (quest == NULL || quest->giver == NULL) {
        return false;
    }
    Unit giver;
    memset(&giver, 0, sizeof(giver));
    snprintf(giver.id, sizeof(giver.id), "%s", quest->giver);
    char text_name[24];
    char sound_name[24];
    Roam__NpcSpeechNames(&giver, text_name, sizeof(text_name), sound_name, sizeof(sound_name));
    char key[64];
    snprintf(key, sizeof(key), "A%dQ%dInit%s", quest->act + 1, quest->number, text_name);
    const char *text = Roam__NpcString(key);
    if (text == NULL) {
        return false;
    }
    while (*text >= '0' && *text <= '9') {
        text++; // (the game's scrolling speed, on a line of its own)
    }
    while (*text == '\n' || *text == '\r') {
        text++;
    }
    char sound[64];
    snprintf(sound, sizeof(sound), "%s_act%d_q%d_init", sound_name, quest->act + 1, quest->number);
    Roam__CloseSpeech(roam);
    snprintf(roam->speech_text, sizeof(roam->speech_text), "%s", text);
    const int ms     = Sounds_PlaySpeech(sound);
    roam->speech_ms  = ms > 0 ? ms : 2500 + ((int)strlen(text) * 55);
    roam->speech_at  = SDL_GetTicks64();
    roam->speech_npc = SPEECH_REPLAY; // (no one to walk away from)
    return true;
}

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
    } else if (click && mouse_y >= 64) {
        // A quest's row: what its giver said, again
        int row = 0;
        for (int q = 0; q < QUEST_COUNT; q++) {
            if (Quests_Get(q)->act != roam->quest_act) {
                continue;
            }
            if (mouse_y < 64 + ((row + 1) * 60)) {
                if (!Roam__ReplayQuest(roam, q)) {
                    Roam__ShowMessage(roam, "Nothing to hear again for that one.");
                }
                break;
            }
            row++;
        }
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
    snprintf(text, sizeof(text), "Quests  (%s)   click one to hear it again", difficulty_names[Roam__Difficulty(roam)]);
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
static void Roam__CallAncients(SceneRoam *roam, const Unit *altar, bool hidden);

static bool Roam__QuestObject(SceneRoam *roam, Unit *unit) {
    if (!roam->game || roam->character == NULL) {
        return false;
    }
    const int fn = unit->object.operate_fn;
    switch (fn) {
    case 33: // Wirt's body (Tristram): his leg
        Roam__DropQuestItem(roam, "leg", unit->x, unit->y);
        return false; // (and what else he had)
    case 39:          // the Horadric Cube's chest (Halls of the Dead)
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
        if ((!Roam__QuestOpen(roam, 4, 5) && !roam->online) || roam->ancients_called) {
            return true;
        }
        roam->ancients_called = true;
        if (roam->online) {
            // Multiplayer: they were put in out of sight when the map was made (the same for everyone): up they come
            for (int i = 0; i < roam->unit_count; i++) {
                Unit *ancient = &roam->units[i];
                if (strncmp(ancient->id, "ancientbarb", 11) == 0 && ancient->vanished && ancient->max_hp > 0) {
                    ancient->vanished = false;
                    ancient->npc      = false;
                    ancient->hp       = ancient->max_hp;
                    Roam__SetState(ancient, UNIT_IDLE, SDL_GetTicks64());
                }
            }
            Roam__ShowMessage(roam, "The Ancients rise to test your worth.");
            return true;
        }
        Roam__CallAncients(roam, unit, false);
        Roam__ShowMessage(roam, "The Ancients rise to test your worth.");
        return true;
    }
    return false;
}

// Talic, Madawc and Korlic by their altar (hidden: out of sight until they're called)
static void Roam__CallAncients(SceneRoam *roam, const Unit *altar, const bool hidden) {
    static const char *const ancients[3][2] = {
        {"ancientbarb1", "Talic the Defender"  },
        {"ancientbarb2", "Madawc the Guardian" },
        {"ancientbarb3", "Korlic the Protector"},
    };
    {
        const Unit *unit = altar;
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
                if (hidden) {
                    ancient->vanished = true;
                    Roam__SetState(ancient, UNIT_DEAD, SDL_GetTicks64());
                }
            }
        }
    }
}

// A monster died: what the quests take from it
static void Roam__UberKilled(SceneRoam *roam, const Unit *victim);

static void Roam__QuestKill(SceneRoam *roam, const Unit *victim) {
    if (!roam->game || roam->character == NULL || victim->is_object || victim->ally) {
        return;
    }
    if (strncmp(victim->id, "uber", 4) == 0) {
        Roam__UberKilled(roam, victim);
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

// The cube made a red portal: the cow level's (in the Rogue Encampment, once Baal is beaten on that difficulty), or
// Pandemonium's (in Harrogath, in Hell: the three keys open Matron's Den, the Forgotten Sands or the Furnace of Pain,
// one not yet opened this game; the three organs Uber Tristram). You go through at once.
static void Roam__CowLevel(SceneRoam *roam) {
    const CubePortal portal = Cube_TakePortal();
    if (portal == CUBE_NO_PORTAL) {
        return;
    }
    int level = -1;
    if (portal == CUBE_COW_PORTAL) {
        const int baal = Roam__QuestIndex(4, 6);
        if (roam->area_level != 1) {
            Roam__ShowMessage(roam, "The portal can only be opened in the Rogue Encampment.");
        } else if (baal >= 0 && *Roam__QuestState(roam, baal) == QUEST_OPEN) {
            Roam__ShowMessage(roam, "Nothing happens: the cows wait until Baal is beaten.");
        } else {
            level = 39;
            Roam__ShowMessage(roam, "Moo.");
        }
    } else if (roam->area_level != 109 || Roam__Difficulty(roam) < 2) {
        Roam__ShowMessage(roam, "That portal only opens in Harrogath, in Hell.");
    } else if (portal == CUBE_FINALE_PORTAL) {
        level = 136;
        Roam__ShowMessage(roam, "A red portal to Tristram opens...");
    } else {
        int left[3];
        int count = 0;
        for (int p = 0; p < 3; p++) {
            if (!(roam->pandemonium_opened & (1 << p))) {
                left[count++] = p;
            }
        }
        if (count == 0) {
            Roam__ShowMessage(roam, "All three of Pandemonium's portals have been opened this game.");
        } else {
            const int pick            = left[Roam__Roll(0, count - 1)];
            roam->pandemonium_opened |= 1 << pick;
            level                     = 133 + pick;
            Roam__ShowMessage(roam, pick == 0 ? "A red portal to Matron's Den opens..."
                                              : (pick == 1 ? "A red portal to the Forgotten Sands opens..."
                                                           : "A red portal to the Furnace of Pain opens..."));
        }
    }
    const int place = level > 0 ? Roam__FindPlace(level) : -1;
    if (place >= 0) {
        roam->inventory_panel = false;
        Roam__Teleport(roam, place);
        return;
    }
    Cube_GiveBack(roam->character); // (not opened: what went in comes back)
}

// The Throne of Destruction (single player): come near Baal and his five waves of minions come one after another, each
// led by its own: Colenzo the Annihilator and his Warped Shamans, Achmel the Cursed and the Unravelers, Bartuc the
// Bloody and the Council, Ventar the Unholy and the Venom Lords, Lister the Tormentor and Baal's own minions. When the
// last is beaten Baal leaves for the Worldstone Chamber and its portal opens.
static void Roam__BaalWaves(SceneRoam *roam, const uint64_t now) {
    static const struct {
        const char *leader;
        const char *id;
        int         count;
    } waves[5] = {
        {"Colenzo the Annihilator", "fallenshaman6",  5},
        {"Achmel the Cursed",       "unraveler6",     4},
        {"Bartuc the Bloody",       "baalhighpriest", 3},
        {"Ventar the Unholy",       "venomlord",      3},
        {"Lister the Tormentor",    "baalminion1",    4},
    };
    if (!roam->game || roam->online || roam->area_level != 131 || roam->baal_wave >= 6) {
        return;
    }
    int throne = -1;
    for (int i = 0; i < roam->unit_count && throne < 0; i++) {
        throne = strcmp(roam->units[i].id, "baalthrone") == 0 && Roam__IsAlive(&roam->units[i]) ? i : -1;
    }
    if (throne < 0) {
        return;
    }
    Unit *baal     = &roam->units[throne];
    baal->speed    = 0.0f; // (he sits it out)
    baal->provoked = false;
    if (roam->baal_wave == 0) { // (not begun: begins when you come near)
        if (Roam__Distance(baal->x, baal->y, roam->player.x, roam->player.y) < 40.0f) {
            roam->baal_wave_at = now + 2000;
            roam->baal_wave    = -1; // (coming)
            Roam__ShowMessage(roam, "Baal: \"Welcome to your doom!\"");
        }
        return;
    }
    if (roam->baal_wave > 0 && roam->baal_wave_at == 0) {
        for (int i = 0; i < roam->unit_count; i++) {
            if (roam->units[i].wave && Roam__IsAlive(&roam->units[i])) {
                return; // this wave's still standing
            }
        }
        roam->baal_wave_at = now + 3000;
        if (roam->baal_wave == 5) {
            roam->baal_wave = 6;
            baal->hp        = 0;
            baal->vanished  = true;
            Roam__SetState(baal, UNIT_DEAD, now);
            Roam__ShowMessage(roam, "Baal: \"Fools! Come to the Worldstone Chamber, if you dare!\" (his portal opens)");
        }
        return;
    }
    if (roam->baal_wave_at == 0 || now < roam->baal_wave_at) {
        return;
    }
    roam->baal_wave    = roam->baal_wave < 0 ? 1 : roam->baal_wave + 1;
    roam->baal_wave_at = 0;
    const int w        = roam->baal_wave - 1;
    for (int k = 0; k <= waves[w].count; k++) {
        float x;
        float y;
        Roam__ArrivalSpot(roam->map, baal->x + (float)Roam__Roll(-6, 6), baal->y + 18.0f + (float)Roam__Roll(0, 6), &x,
                          &y);
        Unit *unit = Roam__AddUnit(roam, "monsters", waves[w].id, x, y);
        if (unit == NULL) {
            continue;
        }
        unit->wave     = true;
        unit->provoked = true;
        if (k == 0) {
            Roam__MakeRank(roam, unit, "@unique");
            snprintf(unit->name, sizeof(unit->name), "%s", waves[w].leader);
        }
    }
    char text[96];
    snprintf(text, sizeof(text), "%s comes with Baal's minions.", waves[w].leader);
    Roam__ShowMessage(roam, text);
}

// Pandemonium's Ubers, in their areas until they're beaten this game: Lilith in Matron's Den, Uber Duriel in the
// Forgotten Sands, Uber Izual in the Furnace of Pain; Uber Mephisto, Diablo and Baal in Uber Tristram. And the Diablo
// Clone when he walks the earth.
static void Roam__PlaceUbers(SceneRoam *roam) {
    static const struct {
        int         level;
        const char *id;
        const char *name;
    } ubers[] = {
        {133, "uberandariel", "Lilith"       },
        {134, "uberduriel",   "Uber Duriel"  },
        {135, "uberizual",    "Uber Izual"   },
        {136, "ubermephisto", "Uber Mephisto"},
        {136, "uberdiablo",   "Uber Diablo"  },
        {136, "uberbaal",     "Uber Baal"    },
    };
    for (size_t u = 0; u < sizeof(ubers) / sizeof(ubers[0]); u++) {
        const MapArea *area = Roam__MapAreaFor(roam, ubers[u].level);
        if (area == NULL || (roam->ubers_killed & (1 << u))) {
            continue;
        }
        const float left = (float)area->x0 * MAP_SUBTILES;
        const float top  = (float)area->y0 * MAP_SUBTILES;
        const float w    = (float)(area->x1 - area->x0 + 1) * MAP_SUBTILES;
        const float h    = (float)(area->y1 - area->y0 + 1) * MAP_SUBTILES;
        float       x    = 0.0f;
        float       y    = 0.0f;
        float       best = -1.0f;
        for (int t = 0; t < 40; t++) { // far from where you come in (in Tristram: round the middle)
            float sx;
            float sy;
            Roam__ArrivalSpot(roam->map, left + (w * (float)Roam__Roll(20, 80) / 100.0f),
                              top + (h * (float)Roam__Roll(20, 80) / 100.0f), &sx, &sy);
            const float far =
                ubers[u].level == 136 || roam->online
                    ? (float)(40 - t)
                    : Roam__Distance(sx, sy, roam->player.x, roam->player.y); // (multiplayer: the same for everyone)
            if (Map_IsWalkable(roam->map, (int)sx, (int)sy) && far > best) {
                best = far;
                x    = sx;
                y    = sy;
            }
        }
        Unit *boss = best >= 0.0f ? Roam__AddUnit(roam, "monsters", ubers[u].id, x, y) : NULL;
        if (boss != NULL) {
            snprintf(boss->name, sizeof(boss->name), "%s", ubers[u].name);
            boss->boss = true;
        }
    }
    if (roam->clone_walks && !roam->in_town && roam->area_level != 136) {
        float x;
        float y;
        Roam__ArrivalSpot(roam->map, roam->player.x + 20.0f, roam->player.y + 10.0f, &x, &y);
        Unit *clone = Roam__AddUnit(roam, "monsters", "uberdiablo", x, y);
        if (clone != NULL) {
            snprintf(clone->name, sizeof(clone->name), "Diablo Clone");
            clone->boss       = true;
            clone->clone      = true;
            roam->clone_walks = false;
            Roam__ShowMessage(roam, "Diablo walks the earth!");
        }
    }
}

// A unique of that base dropped (the Hellfire Torch, Annihilus: their base's only unique at that level)
static void Roam__DropUnique(SceneRoam *roam, const char *code, const int level, const float x, const float y) {
    Item item = Inventory_NewItem(code);
    if (item.code[0] != '\0' && Affixes_RollQuality(&item, level, ITEM_UNIQUE)) {
        Roam__PutOnGround(roam, &item, x, y);
    }
}

// An Uber beaten: marked for the game; the last of Tristram's three leaves the Hellfire Torch, the Diablo Clone
// Annihilus
static void Roam__UberKilled(SceneRoam *roam, const Unit *victim) {
    static const char *ids[] = {"uberandariel", "uberduriel", "uberizual", "ubermephisto", "uberdiablo", "uberbaal"};
    if (victim->clone) {
        Roam__DropUnique(roam, "cm1", 110, victim->x, victim->y); // Annihilus
        return;
    }
    for (int u = 0; u < 6; u++) {
        if (strcmp(victim->id, ids[u]) == 0 && (u < 3 || roam->area_level == 136)) {
            roam->ubers_killed |= 1 << u;
            if (u >= 3 && (roam->ubers_killed & 0x38) == 0x38) {
                Roam__DropUnique(roam, "cm2", 110, victim->x, victim->y); // the Hellfire Torch
            }
        }
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
    if (area == NULL || (!Roam__QuestOpen(roam, 4, 2) && !roam->online)) {
        return; // (multiplayer: always, the same for everyone)
    }
    const float left = (float)area->x0 * MAP_SUBTILES;
    const float top  = (float)area->y0 * MAP_SUBTILES;
    const float w    = (float)(area->x1 - area->x0 + 1) * MAP_SUBTILES;
    const float h    = (float)(area->y1 - area->y0 + 1) * MAP_SUBTILES;
    for (int p = roam->online ? 0 : roam->prisons_broken; p < 5; p++) {
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
        {1,   "cain5",  "akara",   8.0f,   5.0f, 0, 4}, // Deckard Cain, once he's rescued from Tristram
    };
    for (size_t f = 0; f < sizeof(folk) / sizeof(folk[0]); f++) {
        if (Roam__MapAreaFor(roam, folk[f].level) == NULL) {
            continue;
        }
        bool hidden = false; // (multiplayer: put in anyway, out of sight, so everyone has the same people)
        if (folk[f].after_act > 0) {
            const int q = Roam__QuestIndex(folk[f].after_act, folk[f].after_number);
            if (!roam->game || q < 0 || *Roam__QuestState(roam, q) == QUEST_OPEN) {
                if (!roam->online) {
                    continue;
                }
                hidden = true;
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
            unit->npc      = !hidden;
            unit->vanished = hidden;
            unit->hp       = hidden ? 0 : unit->hp;
        }
    }
    // Nihlathak leaves Harrogath once Anya is back (Prison of Ice), for the Halls of Vaught
    const int ice = Roam__QuestIndex(4, 3);
    for (int i = 0; roam->game && ice >= 0 && *Roam__QuestState(roam, ice) != QUEST_OPEN && i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (unit->npc && strcasecmp(unit->id, "nihlathak") == 0 && Roam__MapAreaFor(roam, 109) != NULL) {
            unit->vanished = true;
            unit->hp       = 0;
            unit->npc      = false;
            Roam__SetState(unit, UNIT_DEAD, SDL_GetTicks64());
        }
    }
}

static void Roam__PlaceQuestObjects(SceneRoam *roam) {
    Roam__PlacePrisons(roam);
    Roam__PlaceUbers(roam);
    Roam__PlaceTownFolk(roam);
    // Multiplayer: the Ancients wait out of sight by their altar
    for (int i = 0; roam->online && i < roam->unit_count; i++) {
        if (roam->units[i].is_object && roam->units[i].object.operate_fn == 65) {
            Roam__CallAncients(roam, &roam->units[i], true);
            break;
        }
    }
    // Multiplayer: the bosses the presets leave out are there from the start, for everyone
    for (int q = 0; roam->online && q < QUEST_COUNT; q++) {
        const Quest   *quest = Quests_Get(q);
        const MapArea *area  = quest->goal == QUEST_KILL && quest->spawn ? Roam__MapAreaFor(roam, quest->level) : NULL;
        if (area != NULL && Roam__QuestBoss(roam, quest) < 0) {
            Roam__SpawnQuestBoss(roam, quest, area);
        }
    }
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
            const float far =
                roam->online ? (float)(40 - t) : Roam__Distance(sx, sy, roam->player.x, roam->player.y); // (the same)
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
