// SceneRoam: talking to the town folk (included by SceneRoam.c). Click one and walk up: a menu of what they do:
// trade (their store, from the item tables and npc.txt), gamble, repair, identify (Deckard Cain), travel to the next
// town (Warriv, Meshif, Tyrael), hire, and the healers (Akara, Fara, Ormus, Jamella, Malah) heal you as you come to
// them, the game's way.

#define NPC_MENU_W   440
#define NPC_MENU_ROW 26
#define NPC_MENU_Y   140
#define NPC_RANGE    4.0f // how close you stay to them while trading

// NPC_OFFER + n: hire offer n; NPC_QUEST + n: quest n's reward
enum {
    NPC_TALK  = 9,
    NPC_TRADE = 0,
    NPC_TRAVEL,
    NPC_HIRE,
    NPC_REVIVE,
    NPC_CANCEL,
    NPC_GAMBLE,
    NPC_REPAIR,
    NPC_IDENTIFY,
    NPC_PERSONALIZE, // Anya, after the Prison of Ice: an item named after you
    NPC_OFFER = 10,
    NPC_QUEST = 40
};
static int  Roam__QuestsFor(SceneRoam *roam, const Unit *unit, int *out, int most);
static bool Roam__PrisonOfIceDone(SceneRoam *roam);
static void Roam__ClaimQuest(SceneRoam *roam, int index);

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
static void Roam__NpcSpeechNames(const Unit *unit, char *text_name, size_t text_size, char *sound_name,
                                 size_t sound_size);
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

static void Roam__OpenShop(SceneRoam *roam, const int npc, const bool gamble) {
    ShopStock *stock = NULL;
    if (gamble) {
        if (roam->gamble == NULL) {
            roam->gamble = malloc(sizeof(ShopStock));
            FAIL_IF_NULL(roam->gamble);
        }
        Shop_FillGamble(roam->gamble, roam->units[npc].id, roam->character->level, Roam__Difficulty(roam));
        stock = roam->gamble;
    } else {
        stock = Roam__ShopFor(roam, &roam->units[npc]);
    }
    roam->shop_npc   = npc;
    roam->open_stock = stock;
    GameInventory_SetStash(roam->inventory_ui, false);
    GameInventory_SetShop(roam->inventory_ui, stock, Roam__Difficulty(roam));
    roam->inventory_panel = true;
    roam->skill_panel     = false;
    roam->char_panel      = false;
    roam->panel           = PANEL_NONE;
}

// Come up to them: healers heal, then the menu of what they do
static bool Roam__QuestTalk(SceneRoam *roam, const Unit *unit);

static void Roam__TalkTo(SceneRoam *roam, const int npc) {
    Unit *unit = &roam->units[npc];
    Roam__QuestTalk(roam, unit); // (Meshif and the Jade Figurine)
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, roam->player.x, roam->player.y);
    if (Roam__NpcHeals(unit) && roam->game) {
        roam->player.hp          = roam->player.max_hp;
        roam->mana               = (float)Character_MaxMana(roam->character);
        roam->stamina            = (float)Character_MaxStamina(roam->character);
        roam->player.poison_left = 0.0f;
    }
    // Their greeting (speech: "akara_greeting_1" and the like)
    char text_name[24];
    char sound_name[24];
    char greeting[48];
    Roam__NpcSpeechNames(unit, text_name, sizeof(text_name), sound_name, sizeof(sound_name));
    snprintf(greeting, sizeof(greeting), "%s_greeting_1", sound_name);
    if (roam->speech_text[0] == '\0') {
        Sounds_PlaySpeech(greeting);
    }
    roam->npc_option_count = 0;
    int       waiting[3];
    const int rewards = Roam__QuestsFor(roam, unit, waiting, 3);
    for (int r = 0; r < rewards; r++) {
        roam->npc_options[roam->npc_option_count++] = NPC_QUEST + waiting[r];
    }
    if (Roam__NpcHasTalk(roam, unit)) {
        roam->npc_options[roam->npc_option_count++] = NPC_TALK;
    }
    if (Shop_Vendor(unit->id) != NULL) {
        roam->npc_options[roam->npc_option_count++] = NPC_TRADE;
    }
    if (roam->game && Shop_Gambles(unit->id)) {
        roam->npc_options[roam->npc_option_count++] = NPC_GAMBLE;
    }
    if (roam->game && Shop_Repairs(unit->id)) {
        roam->npc_options[roam->npc_option_count++] = NPC_REPAIR;
    }
    if (roam->game && Shop_Identifies(unit->id)) {
        roam->npc_options[roam->npc_option_count++] = NPC_IDENTIFY;
    }
    if (roam->game && strcasecmp(unit->id, "drehya") == 0 && !roam->character->personalized &&
        Roam__PrisonOfIceDone(roam)) {
        roam->npc_options[roam->npc_option_count++] = NPC_PERSONALIZE;
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

// ---------------------------------------------------------------------------------------------------------------------
// Talking: their introduction the first time (this game), then gossip, as the game's text (string tables:
// AkaraIntroGossip1, AkaraGossip3, CainAct2Gossip1, ...) and speech (sounds.txt: akara_act1_intro, akara_act1_gossip_3,
// ...)

// Their names in the string keys ("Akara", "CainAct2" tried first) and in sounds.txt ("akara")
static void Roam__NpcSpeechNames(const Unit *unit, char *text_name, const size_t text_size, char *sound_name,
                                 const size_t sound_size) {
    char base[24];
    int  n = 0;
    for (const char *c = unit->id; *c != '\0' && n < (int)sizeof(base) - 1; c++) {
        if (*c != '-' && !(*c >= '0' && *c <= '9')) {
            base[n++] = *c;
        }
    }
    base[n] = '\0';
    snprintf(sound_name, sound_size, "%s", base);
    if (strcasecmp(base, "greiz") == 0) {
        snprintf(text_name, text_size, "Griez"); // (the strings spell him so)
    } else if (strcasecmp(base, "qualkehk") == 0) {
        snprintf(text_name, text_size, "QualKehk");
    } else {
        snprintf(text_name, text_size, "%c%s", toupper((unsigned char)base[0]), base + 1);
    }
}

static const char *Roam__NpcString(const char *key) { return StringTable_Find(key); }

// The gossip lines' key ("AkaraGossip" or "CainAct2Gossip") and how many there are
static int Roam__NpcGossip(const SceneRoam *roam, const Unit *unit, char *prefix, const size_t size) {
    char      text_name[24];
    char      sound_name[24];
    char      key[64];
    const int act = Roam__Act(roam) + 1;
    Roam__NpcSpeechNames(unit, text_name, sizeof(text_name), sound_name, sizeof(sound_name));
    for (int form = 0; form < 2; form++) {
        if (form == 0) {
            snprintf(prefix, size, "%sAct%dGossip", text_name, act);
        } else {
            snprintf(prefix, size, "%sGossip", text_name);
        }
        int count = 0;
        for (;; count++) {
            snprintf(key, sizeof(key), "%s%d", prefix, count + 1);
            if (Roam__NpcString(key) == NULL) {
                break;
            }
        }
        if (count > 0) {
            return count;
        }
    }
    return 0;
}

static bool Roam__NpcHasTalk(const SceneRoam *roam, const Unit *unit) {
    char prefix[64];
    return unit->npc && !unit->is_object && Roam__NpcGossip(roam, unit, prefix, sizeof(prefix)) > 0;
}

static void Roam__CloseSpeech(SceneRoam *roam) {
    if (roam->speech_text[0] != '\0') {
        Sounds_StopSpeech();
    }
    roam->speech_text[0] = '\0';
    roam->speech_npc     = -1;
}

static bool Roam__NpcHeard(SceneRoam *roam, const char *key, const bool mark) {
    for (int h = 0; h < roam->npc_heard_count; h++) {
        if (strcasecmp(roam->npc_heard[h], key) == 0) {
            return true;
        }
    }
    if (mark && roam->npc_heard_count < (int)(sizeof(roam->npc_heard) / sizeof(roam->npc_heard[0]))) {
        snprintf(roam->npc_heard[roam->npc_heard_count++], sizeof(roam->npc_heard[0]), "%s", key);
    }
    return false;
}

static uint8_t *Roam__QuestState(SceneRoam *roam, int index);

// What they say about this act's quests: the one you're on (the giver's first words "A1Q1InitAkara", then
// "AfterInit" / "EarlyReturn"), or one done ("Successful"); each once a game. The speech is "akara_act1_q1_init" and so
// on
static const char *Roam__NpcQuestLine(SceneRoam *roam, const char *text_name, const char *sound_name, const int act,
                                      char *sound, const size_t sound_size) {
    if (!roam->game) {
        return NULL;
    }
    for (int q = 0; q < QUEST_COUNT; q++) {
        const Quest *quest = Quests_Get(q);
        if (quest->act != act - 1) {
            continue;
        }
        const uint8_t state = *Roam__QuestState(roam, q);
        static const struct {
            const char *key;
            const char *sound;
            bool        done; // for a finished quest
            bool        once; // (each once a game: after that, gossip)
        } lines[] = {
            {"Init",        "init",    false, true},
            {"EarlyReturn", "early",   false, true},
            {"AfterInit",   "after",   false, true},
            {"Successful",  "success", true,  true},
        };
        for (size_t l = 0; l < sizeof(lines) / sizeof(lines[0]); l++) {
            if (lines[l].done != (state != QUEST_OPEN)) {
                continue;
            }
            char key[64];
            snprintf(key, sizeof(key), "A%dQ%d%s%s", act, quest->number, lines[l].key, text_name);
            const char *text = Roam__NpcString(key);
            if (text == NULL || (lines[l].once && Roam__NpcHeard(roam, key, false))) {
                continue;
            }
            if (lines[l].once) {
                Roam__NpcHeard(roam, key, true);
            }
            snprintf(sound, sound_size, "%s_act%d_q%d_%s", sound_name, act, quest->number, lines[l].sound);
            return text;
        }
    }
    return NULL;
}

// Their introduction (the first time this game; one for your class when they have it), what they have to say about
// the quest you're on, or a piece of gossip
static void Roam__NpcSpeak(SceneRoam *roam, const int npc) {
    const Unit *unit = &roam->units[npc];
    char        text_name[24];
    char        sound_name[24];
    Roam__NpcSpeechNames(unit, text_name, sizeof(text_name), sound_name, sizeof(sound_name));
    const int                act                                = Roam__Act(roam) + 1;
    static const char *const class_short[CHARACTER_CLASS_COUNT] = {"Ama", "Sor", "Nec", "Pal", "Bar", "Dru", "Ass"};
    const char              *cls   = roam->character != NULL ? class_short[roam->character->class_id] : "";
    bool                     heard = false;
    for (int h = 0; h < roam->npc_heard_count && !heard; h++) {
        heard = strcasecmp(roam->npc_heard[h], unit->id) == 0;
    }
    const char *text = NULL;
    char        sound[64];
    sound[0] = '\0';
    if (!heard) {
        // The introduction: by act, by class, in the forms the strings use
        const char *forms[] = {"%sAct%dIntro%sGossip1", "%sAct%dIntroGossip1", "%sActIntro%sGossip1",
                               "%sActIntroGossip1",     "%sIntro%sGossip1",    "%sIntroGossip1"};
        char        key[64];
        for (size_t f = 0; f < sizeof(forms) / sizeof(forms[0]) && text == NULL; f++) {
            switch (f) {
            case 0:
                snprintf(key, sizeof(key), forms[f], text_name, act, cls);
                break;
            case 1:
                snprintf(key, sizeof(key), forms[f], text_name, act);
                break;
            case 2:
            case 4:
                snprintf(key, sizeof(key), forms[f], text_name, cls);
                break;
            default:
                snprintf(key, sizeof(key), forms[f], text_name);
                break;
            }
            text = Roam__NpcString(key);
        }
        if (text != NULL) {
            char lower[8];
            snprintf(lower, sizeof(lower), "%c%c%c", tolower((unsigned char)cls[0]), cls[1], cls[2]);
            snprintf(sound, sizeof(sound), "%s_act%d_intro_%s", sound_name, act, lower);
            if (!Sounds_Exists(sound)) {
                snprintf(sound, sizeof(sound), "%s_act%d_intro", sound_name, act);
            }
            char id[40];
            snprintf(id, sizeof(id), "%s", unit->id);
            Roam__NpcHeard(roam, id, true);
        }
    }
    if (text == NULL) {
        text = Roam__NpcQuestLine(roam, text_name, sound_name, act, sound, sizeof(sound));
    }
    if (text == NULL) {
        char      prefix[64];
        const int count = Roam__NpcGossip(roam, unit, prefix, sizeof(prefix));
        if (count == 0) {
            return;
        }
        const int pick = Roam__Roll(1, count);
        char      key[80];
        snprintf(key, sizeof(key), "%s%d", prefix, pick);
        text = Roam__NpcString(key);
        snprintf(sound, sizeof(sound), "%s_act%d_gossip_%d", sound_name, act, pick);
    }
    if (text == NULL) {
        return;
    }
    // (some start with a number on its own line: the game's scrolling speed)
    const char *start = text;
    while (*start >= '0' && *start <= '9') {
        start++;
    }
    text = start != text && (*start == '\n' || *start == '\r') ? start + 1 : text;
    snprintf(roam->speech_text, sizeof(roam->speech_text), "%s", text);
    const int ms     = Sounds_PlaySpeech(sound);
    roam->speech_ms  = ms > 0 ? ms : 2500 + ((int)strlen(text) * 55); // (no speech: about reading speed)
    roam->speech_at  = SDL_GetTicks64();
    roam->speech_npc = npc;
    if (roam->text_mode) {
        Roam__ShowMessage(roam, text); // (text mode has no box: the message line)
    }
}

// Clicks close it, and so does walking away; true when it took the click
static bool Roam__UpdateSpeech(SceneRoam *roam, const bool click) {
    if (roam->speech_text[0] == '\0') {
        return false;
    }
    const Unit *unit =
        roam->speech_npc >= 0 && roam->speech_npc < roam->unit_count ? &roam->units[roam->speech_npc] : NULL;
    if ((roam->speech_npc != SPEECH_REPLAY &&
         (unit == NULL || Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > NPC_RANGE + 4.0f)) ||
        SDL_GetTicks64() > roam->speech_at + (uint64_t)roam->speech_ms + 1500) {
        Roam__CloseSpeech(roam);
        return false;
    }
    if (click || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        Roam__CloseSpeech(roam);
        return true;
    }
    return false;
}

// The box at the top: their words, rolling up as they're spoken (like the game's)
static void Roam__DrawSpeech(SceneRoam *roam) {
    if (roam->speech_text[0] == '\0' || roam->item_label == NULL) {
        return;
    }
    // Wrapped to the box (the strings have their own line breaks too)
    enum { SPEECH_LINES = 48 };
    // (beside the quest log when it's open: a quest played again from it)
    const int SPEECH_W = roam->quest_panel ? 420 : 500;
    const int centre   = roam->quest_panel ? GAME_PANEL_W + ((800 - GAME_PANEL_W) / 2) : 400;
    char      lines[SPEECH_LINES][160];
    int       count = 0;
    char      word_line[160];
    word_line[0]  = '\0';
    const char *c = roam->speech_text;
    while (*c != '\0' && count < SPEECH_LINES) {
        // Next word (or a line break)
        if (*c == '\n' || (*c == '\\' && c[1] == 'n')) {
            snprintf(lines[count++], sizeof(lines[0]), "%s", word_line);
            word_line[0]  = '\0';
            c            += *c == '\n' ? 1 : 2;
            continue;
        }
        const char *end = c;
        while (*end != '\0' && *end != ' ' && *end != '\n' && !(*end == '\\' && end[1] == 'n')) {
            end++;
        }
        char attempt[320];
        snprintf(attempt, sizeof(attempt), "%s%s%.*s", word_line, word_line[0] != '\0' ? " " : "", (int)(end - c), c);
        int w = 0;
        Label_SetText(roam->item_label, attempt);
        Label_GetSize(roam->item_label, &w, NULL);
        if (w > SPEECH_W && word_line[0] != '\0') {
            snprintf(lines[count++], sizeof(lines[0]), "%s", word_line);
            snprintf(word_line, sizeof(word_line), "%.*s", (int)(end - c), c);
        } else {
            snprintf(word_line, sizeof(word_line), "%.159s", attempt);
        }
        c = *end == ' ' ? end + 1 : end;
    }
    if (word_line[0] != '\0' && count < SPEECH_LINES) {
        snprintf(lines[count++], sizeof(lines[0]), "%s", word_line);
    }
    const int      line_h  = 18;
    const int      visible = 9;
    const SDL_Rect box     = {centre - (SPEECH_W / 2) - 14, 60, SPEECH_W + 28, (visible * line_h) + 16};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(sdl_renderer, &box);
    // Rolling: still for the first second, then up so the last line shows as the speech ends
    const uint64_t elapsed = SDL_GetTicks64() - roam->speech_at;
    const int      extra   = count > visible ? (count - visible) * line_h : 0;
    const int      run     = roam->speech_ms > 1000 ? roam->speech_ms - 1000 : 1;
    int            offset  = elapsed > 1000 ? (int)((long)(elapsed - 1000) * extra / run) : 0;
    offset                 = offset > extra ? extra : offset;
    SDL_Rect clip          = {box.x, box.y + 8, box.w, visible * line_h};
    SDL_RenderSetClipRect(sdl_renderer, &clip);
    Label_SetColor(roam->item_label, 0xFF, 0xFF, 0xFF, 0xFF);
    for (int l = 0; l < count; l++) {
        const int y = box.y + 8 + (l * line_h) - offset;
        if (y < box.y - line_h || y > box.y + box.h) {
            continue;
        }
        Label_SetText(roam->item_label, lines[l]);
        int w = 0;
        Label_GetSize(roam->item_label, &w, NULL);
        Label_Draw(roam->item_label, centre - (w / 2), y);
    }
    SDL_RenderSetClipRect(sdl_renderer, NULL);
}

// The Prison of Ice is done (Anya's back): she names an item after you, once
static bool Roam__PrisonOfIceDone(SceneRoam *roam) {
    for (int q = 0; q < QUEST_COUNT; q++) {
        const Quest *quest = Quests_Get(q);
        if (quest->act == 4 && quest->number == 3) {
            return *Roam__QuestState(roam, q) >= QUEST_REWARD;
        }
    }
    return false;
}

// The weapon in hand (or else the body armor) gets your name: magic or better, not named already
static void Roam__Personalize(SceneRoam *roam) {
    Character       *c       = roam->character;
    static const int order[] = {EQUIP_RIGHT_ARM, EQUIP_LEFT_ARM, EQUIP_TORSO, EQUIP_HEAD};
    Item            *item    = NULL;
    for (size_t s = 0; s < sizeof(order) / sizeof(order[0]) && item == NULL; s++) {
        Item *worn = &c->equipped[order[s]];
        if (worn->code[0] != '\0' && worn->quality != ITEM_NORMAL && !worn->unidentified && worn->owner[0] == '\0') {
            item = worn;
        }
    }
    if (item == NULL) {
        Roam__ShowMessage(roam,
                          "Wear the magic, rare, set or unique weapon (or armor) you want named, then come back.");
        return;
    }
    snprintf(item->owner, sizeof(item->owner), "%s", c->name);
    c->personalized = true;
    char line1[160];
    char line2[96];
    Affixes_Name(item, line1, sizeof(line1), line2, sizeof(line2));
    char text[200];
    snprintf(text, sizeof(text), "It's yours now: %s.", line1);
    Roam__ShowMessage(roam, text);
    Sounds_Play("cursor_repair_item", SOUND_KIND_SFX);
    Roam__StoreCharacter(roam);
}

static bool Roam__CanTalk(const SceneRoam *roam, const Unit *unit) {
    bool gives = false;
    for (int q = 0; q < QUEST_COUNT && !gives; q++) {
        gives = Quests_Get(q)->giver != NULL && strcasecmp(Quests_Get(q)->giver, unit->id) == 0;
    }
    return unit->npc && !unit->is_object &&
           (Shop_Vendor(unit->id) != NULL || Roam__NpcTravel(unit) >= 0 || Roam__NpcHeals(unit) ||
            Roam__NpcHasTalk(roam, unit) || Hirelings_Sells(unit->id) || Shop_Gambles(unit->id) ||
            Shop_Repairs(unit->id) || Shop_Identifies(unit->id) || gives);
}

static SDL_Rect Roam__NpcMenuRow(const int row) {
    const SDL_Rect rect = {400 - (NPC_MENU_W / 2) + 10, NPC_MENU_Y + 34 + (row * NPC_MENU_ROW), NPC_MENU_W - 20,
                           NPC_MENU_ROW - 4};
    return rect;
}

static const char *Roam__NpcOptionText(SceneRoam *roam, const int option) {
    static char text[160];
    if (option >= NPC_QUEST) {
        snprintf(text, sizeof(text), "Quest: %s (your reward)", Quests_Name(option - NPC_QUEST));
        return text;
    }
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
    case NPC_TALK:
        return "Talk";
    case NPC_TRADE:
        return "Trade";
    case NPC_GAMBLE:
        return "Gamble";
    case NPC_REPAIR: {
        const int cost = Shop_RepairCost(roam->units[roam->npc_menu].id, roam->character);
        if (cost > 0) {
            snprintf(text, sizeof(text), "Repair everything: %d gold", cost);
        } else {
            snprintf(text, sizeof(text), "Repair (nothing needs it)");
        }
        return text;
    }
    case NPC_IDENTIFY:
        return "Identify Items";
    case NPC_PERSONALIZE:
        return "Personalize an item (your reward)";
    case NPC_TRAVEL: {
        const int travel = Roam__NpcTravel(&roam->units[roam->npc_menu]);
        return travel >= 0 ? npc_travel[travel].label : "Travel";
    }
    default:
        return "Cancel";
    }
}

static void Roam__NpcChoose(SceneRoam *roam, const int option) {
    const int npc = roam->npc_menu;
    if (option >= NPC_QUEST) {
        Roam__ClaimQuest(roam, option - NPC_QUEST); // (the free hireling looks at who's being talked to)
        roam->npc_menu = -1;
        return;
    }
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
    case NPC_TALK:
        Roam__NpcSpeak(roam, npc);
        break;
    case NPC_TRADE:
        Roam__OpenShop(roam, npc, false);
        break;
    case NPC_GAMBLE:
        Roam__OpenShop(roam, npc, true);
        break;
    case NPC_REPAIR: {
        const int cost = Shop_RepairCost(roam->units[npc].id, roam->character);
        if (cost <= 0) {
            Roam__ShowMessage(roam, "Nothing needs repairing.");
        } else if (cost > roam->character->gold) {
            Roam__ShowMessage(roam, "You don't have enough gold for the repairs.");
        } else {
            roam->character->gold -= cost;
            Shop_RepairAll(roam->character);
            Roam__ApplyCharacter(roam); // broken things work again
            Sounds_Play("cursor_repair_item", SOUND_KIND_SFX);
            Roam__ShowMessage(roam, "Everything is repaired.");
            if (roam->online && NetClient_ServerKeepsCharacters()) {
                Roam__StoreCharacter(roam);
            }
        }
        break;
    }
    case NPC_PERSONALIZE:
        Roam__Personalize(roam);
        break;
    case NPC_IDENTIFY: {
        const int count = Shop_IdentifyAll(roam->character);
        if (count > 0) {
            Roam__ApplyCharacter(roam);
            Sounds_Play("cursor_identify_item", SOUND_KIND_SFX);
            if (roam->online && NetClient_ServerKeepsCharacters()) {
                Roam__StoreCharacter(roam);
            }
        }
        char text[80];
        snprintf(text, sizeof(text), count > 0 ? "Identified %d item%s." : "You have nothing to identify.", count,
                 count == 1 ? "" : "s");
        Roam__ShowMessage(roam, text);
        break;
    }
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
    // What they wear: the weapon's damage (and their own a little), armor's defense, magic's life and resistances
    const Item     *weapon = &c->hireling_items[HIRE_WEAPON];
    const ItemBase *wbase  = Item_Works(weapon) ? Items_Find(weapon->code) : NULL;
    if (wbase != NULL) {
        const bool two   = wbase->max_damage <= 0 || (wbase->two_handed && wbase->two_hand_max > 0);
        const int  low   = two ? wbase->two_hand_min : wbase->min_damage;
        const int  high  = two ? wbase->two_hand_max : wbase->max_damage;
        const int  pct   = Affixes_ItemTotal(weapon, MOD_DAMAGE_PCT);
        stats.damage_min = low + (low * pct / 100) + Affixes_ItemTotal(weapon, MOD_DAMAGE_MIN) + (stats.damage_min / 2);
        stats.damage_max =
            high + (high * pct / 100) + Affixes_ItemTotal(weapon, MOD_DAMAGE_MAX) + (stats.damage_max / 2);
    }
    for (int s = 0; s < HIRE_SLOTS; s++) {
        const Item *item = &c->hireling_items[s];
        if (!Item_Works(item)) {
            continue;
        }
        stats.defense       += Character_ItemDefense(item) + Affixes_ItemTotal(item, MOD_DEFENSE);
        stats.hp            += Affixes_ItemTotal(item, MOD_LIFE);
        stats.resist        += Affixes_ItemTotal(item, MOD_RES_ALL);
        stats.attack_rating += Affixes_ItemTotal(item, MOD_ATTACK_RATING);
    }
    stats.resist        = stats.resist > RESIST_MAX ? RESIST_MAX : stats.resist;
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

static void Roam__KeepHirelingScreen(SceneRoam *roam);

// A click on their portrait opens their screen (with the inventory), or closes it
static bool Roam__UpdateHirelingPortrait(SceneRoam *roam, const int mouse_x, const int mouse_y, const bool click) {
    const int       index = roam->game ? Roam__FindHireling(roam) : -1;
    const SDL_Rect  box   = {8, 26, 120, 30};
    const SDL_Point mouse = {mouse_x, mouse_y};
    if (!click || roam->hide_portraits || index < 0 || !Roam__IsAlive(&roam->units[index]) ||
        !SDL_PointInRect(&mouse, &box)) {
        return false;
    }
    Roam__ToggleHireling(roam);
    return true;
}

// The hireling's screen, open or shut (their portrait, or the Hireling screen key)
static void Roam__ToggleHireling(SceneRoam *roam) {
    const int index = roam->game ? Roam__FindHireling(roam) : -1;
    if (index < 0 || !Roam__IsAlive(&roam->units[index])) {
        Roam__ShowMessage(roam, "You have no hireling.");
        return;
    }
    roam->hire_open = !roam->hire_open;
    if (roam->hire_open) {
        Roam__CloseShop(roam);
        GameInventory_SetStash(roam->inventory_ui, false);
        roam->inventory_panel = true;
        roam->skill_panel     = false;
        roam->char_panel      = false;
        roam->panel           = PANEL_NONE;
    }
    Roam__KeepHirelingScreen(roam);
    Sounds_Play("cursor_button_click", SOUND_KIND_UI);
}

// While it's open: their numbers, as they are now; it closes with the inventory, or when they're gone
static void Roam__KeepHirelingScreen(SceneRoam *roam) {
    const int index = roam->game ? Roam__FindHireling(roam) : -1;
    if (!roam->hire_open || index < 0 || !Roam__IsAlive(&roam->units[index]) || !roam->inventory_panel ||
        roam->char_panel || GameInventory_ShopOpen(roam->inventory_ui) || GameInventory_StashOpen(roam->inventory_ui) ||
        GameInventory_CubeOpen(roam->inventory_ui)) {
        roam->hire_open = false;
        GameInventory_SetHireling(roam->inventory_ui, NULL);
        return;
    }
    const Unit      *unit = &roam->units[index];
    const Character *c    = roam->character;
    HirelingView    *view = &roam->hire_view;
    snprintf(view->name, sizeof(view->name), "%s", unit->name);
    snprintf(view->title, sizeof(view->title), "%s", Hirelings_Title(c->hireling - 1));
    view->row        = c->hireling - 1;
    view->level      = c->hireling_level;
    view->hp         = unit->hp;
    view->max_hp     = unit->max_hp;
    view->damage_min = unit->damage_min;
    view->damage_max = unit->damage_max;
    view->defense    = unit->defense;
    view->resist     = unit->resist[ELEMENT_FIRE];
    view->strength   = Hirelings_Strength(c->hireling - 1, c->hireling_level);
    view->dexterity  = Hirelings_Dexterity(c->hireling - 1, c->hireling_level);
    if (!GameInventory_HirelingOpen(roam->inventory_ui)) {
        GameInventory_SetHireling(roam->inventory_ui, view);
    }
}

// Any portraits in the top left (the hireling's, the party's)?
static bool Roam__PortraitsShown(SceneRoam *roam) {
    if (roam->hide_portraits || !roam->game) {
        return false;
    }
    const int index = Roam__FindHireling(roam);
    if (index >= 0 && Roam__IsAlive(&roam->units[index])) {
        return true;
    }
    for (int o = 0; o < roam->other_count && roam->online; o++) {
        if (Roam__InMyParty(NetClient_Player(roam->other_ids[o]))) {
            return true;
        }
    }
    return false;
}

// Party members' portraits, under the hireling's: name, level and life (Show portraits, Z, hides them)
static void Roam__DrawPartyPortraits(SceneRoam *roam, int y) {
    for (int o = 0; o < roam->other_count && roam->online && !roam->hide_portraits; o++) {
        const NetPlayer *player = NetClient_Player(roam->other_ids[o]);
        if (!Roam__InMyParty(player)) {
            continue;
        }
        const SDL_Rect box = {8, y, 120, 30};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
        SDL_RenderFillRect(sdl_renderer, &box);
        const int      pct  = player->life < 0 ? 0 : (player->life > 100 ? 100 : player->life);
        const SDL_Rect life = {box.x + 4, box.y + 20, (box.w - 8) * pct / 100, 5};
        SDL_SetRenderDrawColor(sdl_renderer, pct > 50 ? 30 : 200, pct > 50 ? 180 : (pct > 25 ? 160 : 30), 30, 255);
        SDL_RenderFillRect(sdl_renderer, &life);
        char text[48];
        snprintf(text, sizeof(text), "%s  %d", player->name, player->level);
        Label_SetText(roam->item_label, text);
        Label_SetColor(roam->item_label, 0xC7, 0xB3, 0x77, 0xFF);
        Label_Draw(roam->item_label, box.x + 4, box.y + 3);
        y += 34;
    }
}

// Their portrait's place: top left, name and life
static void Roam__DrawHireling(SceneRoam *roam) {
    const int index = roam->game ? Roam__FindHireling(roam) : -1;
    if (index < 0 || !Roam__IsAlive(&roam->units[index]) || roam->hide_portraits) {
        Roam__DrawPartyPortraits(roam, 26);
        return;
    }
    Roam__DrawPartyPortraits(roam, 60);
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
    // What the one you had wore comes back to you
    for (int s = 0; s < HIRE_SLOTS && c->hireling > 0; s++) {
        if (c->hireling_items[s].code[0] != '\0' &&
            (Inventory_Add(c, &c->hireling_items[s]) || Stash_Add(c, &c->hireling_items[s]))) {
            memset(&c->hireling_items[s], 0, sizeof(Item));
        }
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
