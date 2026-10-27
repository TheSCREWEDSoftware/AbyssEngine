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

static void Roam__CheckQuests(SceneRoam *roam, const uint64_t now) {
    if (!roam->game || roam->character == NULL || roam->map == NULL || now < roam->quest_check_at) {
        return;
    }
    roam->quest_check_at = now + QUEST_CHECK_MS;
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
    case REWARD_RING: {
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
