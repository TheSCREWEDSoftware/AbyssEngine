// Included by SceneRoam.c: multiplayer hostility, corpses the others see, and letting them loot yours.
//
// Hostility (games with PvP on): the party screen's Hostile button declares it to one player; ten seconds later (the
// game's countdown, shown on the button) the two of you can hurt each other outside town. Either one declaring it is
// enough; both must be level 9 and not in the same party. Peace is made at once.
//
// Corpses: while your gear lies on your corpse, the others on that map see it ("Name's corpse"). The party screen's
// Loot button lets one player take what's on it: walking up to it, everything drops on the ground there (the server's
// items, the first to reach each gets it).

// The game lets players fight at all (its PvP setting)
static bool Roam__PvPGame(void) { return NetClient_Settings() != NULL && NetClient_Settings()->pvp; }

// The two of us can fight: the game allows it, we're out of town, and one of us went hostile long enough ago
static bool Roam__HostileTo(const SceneRoam *roam, const int id) {
    if (!Roam__PvP(roam) || id <= 0 || id > NET_MAX_PLAYERS) {
        return false;
    }
    const uint64_t now = SDL_GetTicks64();
    return (roam->hostile_mine[id] != 0 && now >= roam->hostile_mine[id]) ||
           (roam->hostile_theirs[id] != 0 && now >= roam->hostile_theirs[id]);
}

// Anyone at all we could fight right now (the mouse looks for players to attack only then)
static bool Roam__AnyHostile(const SceneRoam *roam) {
    for (int o = 0; o < roam->other_count; o++) {
        if (Roam__HostileTo(roam, roam->other_ids[o])) {
            return true;
        }
    }
    return false;
}

static void Roam__DeclareHostile(SceneRoam *roam, const NetPlayer *player) {
    const bool on = roam->hostile_mine[player->id] == 0;
    if (on && (roam->character->level < NET_HOSTILE_LVL || player->level < NET_HOSTILE_LVL)) {
        Roam__ShowMessage(roam, "You both have to be level 9 to go hostile.");
        return;
    }
    if (on && Roam__InMyParty(player)) {
        Roam__ShowMessage(roam, "Leave the party first.");
        return;
    }
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)player->id);
    NetWriter_U8(writer, on ? 1 : 0);
    NetClient_Send(MSG_HOSTILE, writer);
    free(writer);
}

// The server says someone went hostile (or made peace)
static void Roam__HostileMessage(SceneRoam *roam, NetReader *reader) {
    const int  from   = NetReader_U8(reader);
    const int  target = NetReader_U8(reader);
    const bool on     = NetReader_U8(reader) != 0;
    const int  me     = NetClient_MyId();
    if (reader->failed || from <= 0 || from > NET_MAX_PLAYERS || target <= 0 || target > NET_MAX_PLAYERS) {
        return;
    }
    const uint64_t   at   = on ? SDL_GetTicks64() + NET_HOSTILE_MS : 0;
    const NetPlayer *them = NetClient_Player(from == me ? target : from);
    const char      *name = them != NULL ? them->name : "Someone";
    char             text[96];
    if (from == me) {
        roam->hostile_mine[target] = at;
        snprintf(text, sizeof(text), on ? "You declared hostility to %s." : "You are at peace with %s.", name);
    } else if (target == me) {
        roam->hostile_theirs[from] = at;
        snprintf(text, sizeof(text), on ? "%s has declared hostility towards you." : "%s is at peace with you.", name);
    } else {
        return;
    }
    Roam__ShowMessage(roam, text);
}

// The Hostile button's words: "Hostile", the countdown, or "Make Peace"
static void Roam__HostileLabel(const SceneRoam *roam, const int id, char *text, const size_t size) {
    const uint64_t at  = roam->hostile_mine[id];
    const uint64_t now = SDL_GetTicks64();
    if (at == 0) {
        snprintf(text, size, "Hostile");
    } else if (now < at) {
        snprintf(text, size, "Hostile in %d", (int)((at - now + 999) / 1000));
    } else {
        snprintf(text, size, "Make Peace");
    }
}

// Loot: letting them take what's on your corpse (or not)
static void Roam__ToggleLoot(SceneRoam *roam, const NetPlayer *player) {
    const bool allow               = !roam->loot_allowed[player->id];
    roam->loot_allowed[player->id] = allow;
    NetWriter *writer              = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)player->id);
    NetWriter_U8(writer, allow ? LOOT_ALLOW : LOOT_DENY);
    NetClient_Send(MSG_LOOT, writer);
    free(writer);
    char text[96];
    snprintf(text, sizeof(text), allow ? "%s may loot your corpse." : "%s may no longer loot your corpse.",
             player->name);
    Roam__ShowMessage(roam, text);
}

static void Roam__PutOnGroundAt(SceneRoam *roam, const Item *item, int map_key, float x, float y);
static void Roam__SendCorpse(SceneRoam *roam, bool there);

// Theirs, about looting: they let us (or stopped), or they're taking what's on our corpse
static void Roam__LootMessage(SceneRoam *roam, NetReader *reader) {
    const int from = NetReader_U8(reader);
    NetReader_U8(reader); // (to: us)
    const int        op     = NetReader_U8(reader);
    const NetPlayer *player = NetClient_Player(from);
    Character       *c      = roam->character;
    if (reader->failed || player == NULL || from <= 0 || from > NET_MAX_PLAYERS) {
        return;
    }
    char text[96];
    if (op == LOOT_ALLOW || op == LOOT_DENY) {
        roam->loot_from[from] = op == LOOT_ALLOW;
        snprintf(text, sizeof(text),
                 op == LOOT_ALLOW ? "%s lets you loot their corpse." : "%s stopped letting you loot their corpse.",
                 player->name);
        Roam__ShowMessage(roam, text);
        return;
    }
    if (op != LOOT_TAKE || !roam->loot_allowed[from] || !c->has_corpse || roam->corpse_key == 0) {
        return;
    }
    // Everything on it, on the ground where it lies
    for (int s = 0; s < EQUIP_COUNT + BELT_MAX; s++) {
        Item *item = s < EQUIP_COUNT ? &c->corpse_equipped[s] : &c->corpse_belt[s - EQUIP_COUNT];
        if (item->code[0] != '\0') {
            Roam__PutOnGroundAt(roam, item, roam->corpse_key, roam->corpse.x + (float)((s % 3) - 1) * 0.6f,
                                roam->corpse.y + (float)(((s / 3) % 3) - 1) * 0.6f);
            memset(item, 0, sizeof(Item));
        }
    }
    c->has_corpse = false;
    Roam__SendCorpse(roam, false);
    roam->corpse_key = 0;
    snprintf(text, sizeof(text), "%s looted your corpse.", player->name);
    Roam__ShowMessage(roam, text);
    Roam__StoreCharacter(roam);
}

// Ours, every couple of seconds while it's there (and once when it's gone): for the others on that map to see
static void Roam__SendCorpse(SceneRoam *roam, const bool there) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_I32(writer, roam->corpse_key);
    NetWriter_F32(writer, roam->corpse.x);
    NetWriter_F32(writer, roam->corpse.y);
    NetWriter_U8(writer, there ? 1 : 0);
    NetClient_Send(MSG_CORPSE, writer);
    free(writer);
}

static void Roam__KeepCorpseSeen(SceneRoam *roam, const uint64_t now) {
    const bool there = roam->game && roam->character->has_corpse && roam->corpse_key != 0;
    if (there && now - roam->corpse_sent >= 2000) {
        roam->corpse_sent = now;
        Roam__SendCorpse(roam, true);
    }
    // Others' corpses not heard of for a while are gone (they left, or took it back)
    for (int i = 0; i < REMOTE_CORPSES; i++) {
        if (roam->remote_corpses[i].id != 0 && now - roam->remote_corpses[i].heard > 6000) {
            roam->remote_corpses[i].id = 0;
        }
    }
}

// Someone's corpse: where it is, drawn in what they wore
static void Roam__CorpseMessage(SceneRoam *roam, NetReader *reader, const uint64_t now) {
    const int   from  = NetReader_U8(reader);
    const int   map   = NetReader_I32(reader);
    const float x     = NetReader_F32(reader);
    const float y     = NetReader_F32(reader);
    const bool  there = NetReader_U8(reader) != 0;
    if (reader->failed || x != x || y != y || map != Roam__MapKey(roam)) {
        return;
    }
    int slot = -1;
    int free = -1;
    for (int i = 0; i < REMOTE_CORPSES; i++) {
        slot = roam->remote_corpses[i].id == from ? i : slot;
        free = free < 0 && roam->remote_corpses[i].id == 0 ? i : free;
    }
    if (!there) {
        if (slot >= 0) {
            roam->remote_corpses[slot].id = 0;
        }
        return;
    }
    slot = slot >= 0 ? slot : free;
    if (slot < 0) {
        return;
    }
    RemoteCorpse *corpse = &roam->remote_corpses[slot];
    const Unit   *other  = NULL;
    if (corpse->id != from) {
        const NetPlayer *who = NetClient_Player(from);
        if (Roam__OtherById(roam, from, &other)) {
            corpse->unit = *other; // their figure and what they wear
        } else if (who != NULL) {  // (they're elsewhere now, back in town: their class's figure in what they wear)
            memset(&corpse->unit, 0, sizeof(Unit));
            snprintf(corpse->unit.base, sizeof(corpse->unit.base), "chars");
            snprintf(corpse->unit.id, sizeof(corpse->unit.id), "%s", Character_ClassToken(who->class_id));
            corpse->unit.idle_mode = "nu";
            corpse->unit.has_looks = who->has_looks;
            corpse->unit.looks     = who->looks;
        } else {
            return;
        }
        corpse->unit.frame_time = now;
    }
    corpse->id              = from;
    corpse->heard           = now;
    corpse->unit.x          = x;
    corpse->unit.y          = y;
    corpse->unit.state      = UNIT_DEAD;
    corpse->unit.hp         = 0;
    const NetPlayer *player = NetClient_Player(from);
    snprintf(corpse->unit.name, sizeof(corpse->unit.name), "%s's corpse", player != NULL ? player->name : "Someone");
}

// Walking up to a corpse we may loot: we ask them for its things (once every few seconds)
static void Roam__LootNearCorpse(SceneRoam *roam, const uint64_t now) {
    for (int i = 0; i < REMOTE_CORPSES; i++) {
        RemoteCorpse *corpse = &roam->remote_corpses[i];
        if (corpse->id == 0 || !roam->loot_from[corpse->id] || !Roam__IsAlive(&roam->player) ||
            now < corpse->asked + 3000 ||
            Roam__Distance(corpse->unit.x, corpse->unit.y, roam->player.x, roam->player.y) > 1.8f) {
            continue;
        }
        corpse->asked     = now;
        NetWriter *writer = malloc(sizeof(NetWriter));
        FAIL_IF_NULL(writer);
        NetWriter_Begin(writer);
        NetWriter_U8(writer, (uint8_t)corpse->id);
        NetWriter_U8(writer, LOOT_TAKE);
        NetClient_Send(MSG_LOOT, writer);
        free(writer);
    }
}
