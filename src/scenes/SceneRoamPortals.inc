// Included by SceneRoam.c: Town Portals.
//
// Reading a scroll or tome of Town Portal outside town opens a blue portal (objects.txt "TP", OperateFn 15) beside
// you, and its other end in the act's town. Through it from outside: to town, by the portal. Through it from town:
// back to where you opened it, and the portal closes, like the game. One portal at a time: a new one closes the old.
//
// Multiplayer: everyone sees everyone's portals (MSG_PORTAL); your party can go through yours both ways, and it stays
// open for them (it closes when you come back through it).

#define PORTAL_TOKEN "tp"

// Our player id (0 offline): the slot of portals[] that's ours is always 0, but the town ends are spread out by id
static int Roam__MyPortalId(const SceneRoam *roam) { return roam->online ? NetClient_MyId() : 0; }

// Shows a portal: opening, then open
static Unit *Roam__AddPortalUnit(SceneRoam *roam, const int owner, const float x, const float y, const uint64_t now) {
    float spot_x;
    float spot_y;
    Roam__ArrivalSpot(roam->map, x, y, &spot_x, &spot_y);
    Unit *unit = Roam__AddUnit(roam, "objects", PORTAL_TOKEN, spot_x, spot_y);
    if (unit == NULL) {
        return NULL;
    }
    unit->object.selectable = true;
    unit->object.operate_fn = 15;
    unit->portal_owner      = (uint8_t)owner;
    if (owner > 0) {
        const NetPlayer *player = NetClient_Player(owner);
        if (player != NULL) {
            snprintf(unit->name, sizeof(unit->name), "%s's Town Portal", player->name);
        }
    } else {
        snprintf(unit->name, sizeof(unit->name), "Town Portal");
    }
    if (unit->object.has_mode[1] && MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
        Roam__SetState(unit, UNIT_OPERATE, now);
    } else {
        Roam__FinishOperate(roam, unit, now);
    }
    unit->used = false; // (it stays usable once open)
    return unit;
}

// One of owner's portals shown on this map near (x, y)
static bool Roam__PortalNear(const SceneRoam *roam, const int owner, const float x, const float y) {
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 15 && !unit->vanished && unit->portal_owner == owner &&
            Roam__Distance(unit->x, unit->y, x, y) < 2.0f) {
            return true;
        }
    }
    return false;
}

// A portal's ends on this map (after arriving anywhere, or when someone opens one): by the town's arrival spot (each
// player's a little apart), and where it was opened (an act's outdoors and its town can be one map)
static void Roam__PlacePortal(SceneRoam *roam, const int owner) {
    RoamPortal *portal = &roam->portals[owner];
    if (!roam->game || !portal->open || roam->map == NULL || roam->map->seed != portal->seed) {
        return;
    }
    const uint64_t now = SDL_GetTicks64();
    if (roam->area_level == town_level_ids[portal->act] &&
        !(portal->town_set && Roam__PortalNear(roam, owner, portal->town_x, portal->town_y))) {
        const int   id   = owner > 0 ? owner : Roam__MyPortalId(roam);
        const float side = (float)((id % 6) * 2);
        Unit       *unit = Roam__AddPortalUnit(roam, owner, roam->spawn_x + 3.0f + side, roam->spawn_y + 3.0f, now);
        if (unit != NULL) {
            portal->town_x   = unit->x;
            portal->town_y   = unit->y;
            portal->town_set = true;
        }
    }
    if (Map_FindArea(roam->map, portal->level_id) != NULL && !Roam__PortalNear(roam, owner, portal->x, portal->y)) {
        Roam__AddPortalUnit(roam, owner, portal->x, portal->y, now);
    }
}

static void Roam__PlacePortals(SceneRoam *roam) {
    for (int owner = 0; owner <= NET_MAX_PLAYERS; owner++) {
        if (roam->portals[owner].open) {
            roam->portals[owner].town_set = false; // (a new map: placed again)
            Roam__PlacePortal(roam, owner);
        }
    }
}

// Owner's portal units on this map go away (a new portal, or it closed)
static void Roam__HidePortals(SceneRoam *roam, const int owner) {
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 15 && unit->portal_owner == owner &&
            strcmp(unit->id, PORTAL_TOKEN) == 0) {
            unit->vanished          = true;
            unit->used              = true;
            unit->object.selectable = false;
        }
    }
}

// Multiplayer: our portal, to the others
static void Roam__SendPortal(SceneRoam *roam) {
    if (!roam->online) {
        return;
    }
    const RoamPortal *portal = &roam->portals[0];
    NetWriter        *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, portal->open ? 1 : 0);
    NetWriter_U8(writer, (uint8_t)portal->act);
    NetWriter_I32(writer, portal->level_id);
    NetWriter_U32(writer, portal->seed);
    NetWriter_F32(writer, portal->x);
    NetWriter_F32(writer, portal->y);
    NetClient_Send(MSG_PORTAL, writer);
    free(writer);
}

// Multiplayer: someone's portal opened or closed
static void Roam__ReceivePortal(SceneRoam *roam, NetReader *reader) {
    const int      owner = NetReader_U8(reader);
    const bool     open  = NetReader_U8(reader) != 0;
    const int      act   = NetReader_U8(reader);
    const int      level = NetReader_I32(reader);
    const uint32_t seed  = NetReader_U32(reader);
    const float    x     = NetReader_F32(reader);
    const float    y     = NetReader_F32(reader);
    if (reader->failed || owner <= 0 || owner > NET_MAX_PLAYERS || owner == NetClient_MyId() || act >= TOWN_COUNT) {
        return;
    }
    Roam__HidePortals(roam, owner);
    RoamPortal *portal = &roam->portals[owner];
    memset(portal, 0, sizeof(*portal));
    portal->open     = open;
    portal->act      = act;
    portal->level_id = level;
    portal->seed     = seed;
    portal->x        = x;
    portal->y        = y;
    Roam__PlacePortal(roam, owner);
}

// Multiplayer: the portals of players who've left go
static void Roam__ForgetGonePortals(SceneRoam *roam) {
    for (int owner = 1; owner <= NET_MAX_PLAYERS; owner++) {
        if (roam->portals[owner].open && NetClient_Player(owner) == NULL) {
            roam->portals[owner].open = false;
            Roam__HidePortals(roam, owner);
        }
    }
}

// A Town Portal read: the portal opens beside you
static void Roam__OpenTownPortal(SceneRoam *roam) {
    if (!roam->game || roam->in_town || roam->map == NULL) {
        return;
    }
    Roam__HidePortals(roam, 0);
    RoamPortal    *portal = &roam->portals[0];
    const uint64_t now    = SDL_GetTicks64();
    portal->open          = true;
    portal->act           = Roam__Act(roam);
    portal->place         = roam->place;
    portal->level_id      = roam->area_level;
    portal->seed          = roam->map->seed;
    portal->town_set      = false;
    // In front of you (the way you face), on the ground
    const float angle = ((float)roam->player.turn / 16.0f) * 6.2832f;
    Unit       *unit  = Roam__AddPortalUnit(roam, 0, roam->player.x + (sinf(angle) * 2.0f) + 1.0f,
                                            roam->player.y + (cosf(angle) * 2.0f) + 1.0f, now);
    portal->x         = unit != NULL ? unit->x : roam->player.x;
    portal->y         = unit != NULL ? unit->y : roam->player.y;
    Sounds_Play("player_townportal_cast", SOUND_KIND_SFX);
    Sounds_Play("object_townportal", SOUND_KIND_SFX);
    Roam__SendPortal(roam);
}

// Putting the player down by (x, y)
static void Roam__ArriveAt(SceneRoam *roam, const float x, const float y) {
    Roam__ArrivalSpot(roam->map, x + 1.5f, y + 1.5f, &roam->spawn_x, &roam->spawn_y);
    roam->player.x        = roam->spawn_x;
    roam->player.y        = roam->spawn_y;
    roam->player.target_x = roam->spawn_x;
    roam->player.target_y = roam->spawn_y;
    roam->moving          = false;
    Roam__ClearPath(&roam->player);
    roam->exits_armed = false;
}

// Through a portal: ours, or (multiplayer) one of our party's
static void Roam__TakePortal(SceneRoam *roam, const Unit *unit) {
    const int owner = unit != NULL ? unit->portal_owner : 0;
    if (!roam->game || owner > NET_MAX_PLAYERS || !roam->portals[owner].open) {
        return;
    }
    if (owner > 0 && !Roam__InMyParty(NetClient_Player(owner))) {
        Roam__ShowMessage(roam, "Only their party can go through it.");
        return;
    }
    RoamPortal *portal = &roam->portals[owner];
    Sounds_Play("player_townportal_enter", SOUND_KIND_SFX);
    if (!roam->in_town) {
        Roam__GoToTown(roam, portal->act); // (its end there is placed as you arrive)
        Roam__ArriveAt(roam, portal->town_x, portal->town_y);
        return;
    }
    // Back out (ours closes behind us)
    const int      place = owner == 0 ? portal->place : Roam__FindPlace(portal->level_id);
    const float    x     = portal->x;
    const float    y     = portal->y;
    const uint32_t seed  = portal->seed;
    if (owner == 0) {
        portal->open = false;
        Roam__HidePortals(roam, 0);
        Roam__SendPortal(roam);
    }
    if (place < 0) {
        return;
    }
    Roam__Teleport(roam, place);
    if (roam->map != NULL && roam->map->seed == seed) {
        Roam__ArriveAt(roam, x, y);
    }
}
