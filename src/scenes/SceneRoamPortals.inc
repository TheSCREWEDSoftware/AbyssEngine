// Included by SceneRoam.c: Town Portals.
//
// Reading a scroll or tome of Town Portal outside town opens a blue portal (objects.txt "TP", OperateFn 15) beside
// you, and its other end in the act's town. Through it from outside: to town, by the portal. Through it from town:
// back to where you opened it, and the portal closes, like the game. One portal at a time: a new one closes the old.
// Multiplayer: each player's own (the others don't see it yet).

#define PORTAL_TOKEN "tp"

// Shows the portal: opening, then open
static Unit *Roam__AddPortalUnit(SceneRoam *roam, const float x, const float y, const uint64_t now) {
    float spot_x;
    float spot_y;
    Roam__ArrivalSpot(roam->map, x, y, &spot_x, &spot_y);
    Unit *unit = Roam__AddUnit(roam, "objects", PORTAL_TOKEN, spot_x, spot_y);
    if (unit == NULL) {
        return NULL;
    }
    unit->object.selectable = true;
    unit->object.operate_fn = 15;
    if (unit->object.has_mode[1] && MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
        Roam__SetState(unit, UNIT_OPERATE, now);
    } else {
        Roam__FinishOperate(roam, unit, now);
    }
    unit->used = false; // (it stays usable once open)
    return unit;
}

// A portal shown on this map near (x, y)
static bool Roam__PortalNear(const SceneRoam *roam, const float x, const float y) {
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 15 && !unit->vanished &&
            Roam__Distance(unit->x, unit->y, x, y) < 2.0f) {
            return true;
        }
    }
    return false;
}

// The portal's ends on this map (after arriving anywhere): by the town's arrival spot, and where it was opened (an
// act's outdoors and its town can be one map)
static void Roam__PlacePortals(SceneRoam *roam) {
    if (!roam->game || !roam->portal.open || roam->map == NULL || roam->map->seed != roam->portal.seed) {
        return;
    }
    const uint64_t now = SDL_GetTicks64();
    if (roam->area_level == town_level_ids[roam->portal.act] &&
        !(roam->portal.town_set && Roam__PortalNear(roam, roam->portal.town_x, roam->portal.town_y))) {
        Unit *unit = Roam__AddPortalUnit(roam, roam->spawn_x + 3.0f, roam->spawn_y + 3.0f, now);
        if (unit != NULL) {
            roam->portal.town_x   = unit->x;
            roam->portal.town_y   = unit->y;
            roam->portal.town_set = true;
        }
    }
    if (Map_FindArea(roam->map, roam->portal.level_id) != NULL &&
        !Roam__PortalNear(roam, roam->portal.x, roam->portal.y)) {
        Roam__AddPortalUnit(roam, roam->portal.x, roam->portal.y, now);
    }
}

// The portal's units on this map go away (a new portal, or it closed)
static void Roam__HidePortals(SceneRoam *roam) {
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 15 && strcmp(unit->id, PORTAL_TOKEN) == 0) {
            unit->vanished          = true;
            unit->used              = true;
            unit->object.selectable = false;
        }
    }
}

// A Town Portal read: the portal opens beside you
static void Roam__OpenTownPortal(SceneRoam *roam) {
    if (!roam->game || roam->in_town || roam->map == NULL) {
        return;
    }
    Roam__HidePortals(roam);
    const uint64_t now    = SDL_GetTicks64();
    roam->portal.open     = true;
    roam->portal.act      = Roam__Act(roam);
    roam->portal.place    = roam->place;
    roam->portal.level_id = roam->area_level;
    roam->portal.seed     = roam->map->seed;
    roam->portal.town_set = false;
    // In front of you (the way you face), on the ground
    const float angle = ((float)roam->player.turn / 16.0f) * 6.2832f;
    Unit       *unit  = Roam__AddPortalUnit(roam, roam->player.x + (sinf(angle) * 2.0f) + 1.0f,
                                            roam->player.y + (cosf(angle) * 2.0f) + 1.0f, now);
    roam->portal.x    = unit != NULL ? unit->x : roam->player.x;
    roam->portal.y    = unit != NULL ? unit->y : roam->player.y;
    Sounds_Play("object_portal_open", SOUND_KIND_SFX);
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

// Through the portal
static void Roam__TakePortal(SceneRoam *roam) {
    if (!roam->game || !roam->portal.open) {
        return;
    }
    Sounds_Play("object_portal_enter", SOUND_KIND_SFX);
    if (!roam->in_town) {
        Roam__GoToTown(roam, roam->portal.act); // (its end there is placed as you arrive)
        Roam__ArriveAt(roam, roam->portal.town_x, roam->portal.town_y);
        return;
    }
    // Back out: the portal closes behind you
    roam->portal.open = false;
    Roam__HidePortals(roam);
    Roam__Teleport(roam, roam->portal.place);
    if (roam->map != NULL && roam->map->seed == roam->portal.seed) {
        Roam__ArriveAt(roam, roam->portal.x, roam->portal.y);
    }
}
