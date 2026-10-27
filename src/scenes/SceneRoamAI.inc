// SceneRoam: monsters that do more than walk at you (included by SceneRoam.c): archers keep their distance and shoot
// (monstats MissA1), casters throw their spells (Skill1/Skill2 with a missile: the Fallen Shamans' fire, Andariel's
// poison, Mephisto's bolts, novas), the Fallen run when one of theirs dies, and a pack comes together when one of it is
// hit.

#define AI_SHOOT_RANGE 9.0f
#define AI_CAST_RANGE  10.0f
#define AI_PACK_RANGE  8.0f

// A monster's missile at a spot: it hurts us (not the monsters)
static void Roam__SendShot(SceneRoam *roam, const char *name, float x, float y, float tx, float ty, const int *damage);

static void Roam__MonsterMissile(SceneRoam *roam, const Unit *unit, const char *name, const float tx, const float ty,
                                 const int *damage) {
    Missile *missile = Roam__SpawnMissile(roam, name, unit->x, unit->y, tx - unit->x, ty - unit->y, false);
    if (missile != NULL) {
        memcpy(missile->damage, damage, sizeof(missile->damage));
    }
    Roam__SendShot(roam, name, unit->x, unit->y, tx, ty, damage); // (the others see it, and it can hit them)
}

// Its blow's damage (and element), for a missile
static void Roam__MonsterDamage(const Unit *unit, int *damage) {
    memset(damage, 0, sizeof(int) * ELEMENT_COUNT);
    damage[ELEMENT_PHYSICAL] = Roam__Roll(unit->damage_min, unit->damage_max);
    if (unit->element > 0 && unit->element < ELEMENT_COUNT && Roam__Roll(1, 100) <= unit->element_chance) {
        damage[unit->element] = Roam__Roll(unit->element_min, unit->element_max);
    }
}

// Archers: in range and in sight, they stand and shoot (backing off when you're on top of them). True when it did
// something this frame.
static bool Roam__MonsterShoots(SceneRoam *roam, Unit *unit, const float tx, const float ty, const float distance,
                                const uint64_t now, const float seconds) {
    if (unit->shot[0] == '\0' || distance > AI_SHOOT_RANGE || !Map_HasClearLine(roam->map, unit->x, unit->y, tx, ty)) {
        return false;
    }
    Roam__ClearPath(unit);
    if (distance < 3.0f) {
        // Too close: a step back, away from you
        const float bx = unit->x + (unit->x - tx);
        const float by = unit->y + (unit->y - ty);
        if (Roam__Step(roam->map, unit, bx, by, unit->speed, seconds)) {
            Roam__SetState(unit, UNIT_WALK, now);
            return true;
        }
    }
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, tx, ty);
    if (now >= unit->next_attack) {
        unit->next_attack = now + (ATTACK_COOLDOWN * 3 / 2);
        int damage[ELEMENT_COUNT];
        Roam__MonsterDamage(unit, damage);
        Roam__MonsterMissile(roam, unit, unit->shot, tx, ty, damage);
        Roam__SetState(unit, UNIT_ATTACK, now);
        unit->ranged_swing = true; // (its blow mustn't land as well)
    } else {
        Roam__SetState(unit, UNIT_IDLE, now);
    }
    return true;
}

// Casters: now and then, in range and in sight, a bolt (or a nova round them)
static bool Roam__MonsterCasts(SceneRoam *roam, Unit *unit, const float tx, const float ty, const float distance,
                               const uint64_t now) {
    const SkillRow *spell = unit->spell;
    if (spell == NULL || now < unit->next_spell || distance > AI_CAST_RANGE ||
        !Map_HasClearLine(roam->map, unit->x, unit->y, tx, ty)) {
        return false;
    }
    unit->next_spell = now + 1500 + (uint64_t)Roam__Roll(0, 2500);
    if (Roam__Roll(1, 100) > 45) {
        return false; // not this time: it fights the usual way
    }
    if (unit->spell_count > 1) { // (one of its spells this time)
        const int pick    = Roam__Roll(0, unit->spell_count - 1);
        unit->spell       = unit->spells[pick];
        unit->spell_level = unit->spell_levels[pick];
        spell             = unit->spell;
    }
    // The spell's own damage at its level (skills.txt EMin/EMax and their per-level growth), else the monster's blow
    int damage[ELEMENT_COUNT] = {0};
    if (spell->element > 0 && spell->element < ELEMENT_COUNT && spell->element_max > 0) {
        const int grow         = unit->spell_level > 1 ? unit->spell_level - 1 : 0;
        const int low          = spell->element_min + (spell->element_min_level[0] * grow);
        const int high         = spell->element_max + (spell->element_max_level[0] * grow);
        damage[spell->element] = Roam__Roll(low, high > low ? high : low);
    } else {
        Roam__MonsterDamage(unit, damage);
    }
    const char *name = spell->missile[0] != '\0' ? spell->missile : spell->missile_a;
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, tx, ty);
    Roam__ClearPath(unit);
    if (spell->kind == SKILL_KIND_NOVA) {
        for (int n = 0; n < 16; n++) {
            const float angle = (float)n * 2.0f * 3.14159265f / 16.0f;
            Roam__MonsterMissile(roam, unit, name, unit->x + cosf(angle), unit->y + sinf(angle), damage);
        }
    } else {
        Roam__MonsterMissile(roam, unit, name, tx, ty, damage);
    }
    Roam__SetState(unit, UNIT_ATTACK, now);
    unit->ranged_swing = true;
    unit->next_attack  = now + ATTACK_COOLDOWN;
    return true;
}

// Nests, eggs and mothers (monstats spawn): an egg hatches its young when you come near and is gone; a nest, a
// mother or a maggot queen makes one now and then while you're about, up to a dozen in all. Suicide minions blow up
// next to you. Single Player (in multiplayer everyone's monsters have to match). True when it did something
static bool Roam__MonsterSpawns(SceneRoam *roam, Unit *unit, const float distance, const uint64_t now) {
    if (roam->online || !roam->game) {
        return false;
    }
    if (unit->suicide && distance < 2.0f) { // fire all round, and it's gone
        int damage = Roam__ElementTaken(roam, ELEMENT_FIRE, Roam__Roll(unit->damage_min * 2, unit->damage_max * 3));
        damage     = Roam__AbsorbDamage(roam, damage);
        if (damage > 0 && Roam__IsAlive(&roam->player)) {
            Roam__Damage(roam, &roam->player, damage, now);
        }
        Roam__SpawnEffect(roam, "fireexplode", unit->x, unit->y, 0.0f, 1.0f);
        Roam__Damage(roam, unit, unit->hp, now);
        return true;
    }
    if (unit->spawner == SPAWNER_NONE || unit->spawn[0] == '\0' || now < unit->next_raise) {
        return false;
    }
    const bool egg = unit->spawner == SPAWNER_EGG;
    if ((egg && distance > 8.0f) || (!egg && (distance > 18.0f || unit->spawned >= 12))) {
        return false;
    }
    unit->next_raise     = now + 5000 + (uint64_t)Roam__Roll(0, 4000);
    const int      count = egg ? Roam__Roll(2, 3) : 1;
    const MapArea *area  = Map_AreaAt(roam->map, unit->x, unit->y);
    for (int n = 0; n < count && roam->unit_count < MAX_UNITS - 8; n++) {
        float x = unit->x;
        float y = unit->y;
        Roam__ArrivalSpot(roam->map, unit->x + (float)Roam__Roll(-1, 1), unit->y + 1.0f, &x, &y);
        init_area_level = MonsterStats_AreaLevel(area != NULL ? area->level_id : roam->area_level);
        Unit *young     = Roam__AddUnit(roam, "monsters", unit->spawn, x, y);
        init_area_level = 0;
        if (young != NULL) {
            young->provoked = true;
            unit->spawned++;
        }
    }
    if (egg) {
        Roam__Damage(roam, unit, unit->hp, now); // hatched: the egg is done
    } else {
        Roam__SetState(unit, UNIT_ATTACK, now); // (laying)
        unit->ranged_swing = true;
    }
    return true;
}

// Raising the dead and healing (MonStats AI): Fallen Shamans bring back the Fallen, Fetish Shamans the fetishes,
// Greater Mummies the undead near them; Zakarum priests (Cantors) heal the hurt around them. True when it did
static bool Roam__MonsterRaises(SceneRoam *roam, Unit *unit, const uint64_t now) {
    const bool shaman = strncmp(unit->id, "fallenshaman", 12) == 0;
    const bool fetish = strncmp(unit->id, "fetishshaman", 12) == 0;
    const bool mummy  = strncmp(unit->id, "unraveler", 9) == 0 || strcmp(unit->id, "radament") == 0;
    const bool priest = strncmp(unit->id, "cantor", 6) == 0;
    if ((!shaman && !fetish && !mummy && !priest) || now < unit->next_raise || !Roam__RunsMonsters(roam)) {
        return false;
    }
    unit->next_raise = now + 1000; // (looks again in a second)
    int best         = -1;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *other = &roam->units[i];
        if (other == unit || other->is_object || other->npc || other->ally || other->turned || other->boss ||
            (other->rank != RANK_NORMAL && other->rank != RANK_MINION) ||
            Roam__Distance(other->x, other->y, unit->x, unit->y) > 9.0f) {
            continue;
        }
        if (priest) { // a hurt one, under half its life
            if (Roam__IsAlive(other) && other->hp * 2 < other->max_hp && strcmp(other->base, "monsters") == 0) {
                best = i;
                break;
            }
            continue;
        }
        const bool dead = other->state == UNIT_DEAD && !other->vanished && other->max_hp > 0;
        const bool kinds =
            shaman   ? (strncmp(other->id, "fallen", 6) == 0 && strncmp(other->id, "fallenshaman", 12) != 0)
            : fetish ? (strncmp(other->id, "fetish", 6) == 0 && strncmp(other->id, "fetishshaman", 12) != 0)
                     : other->undead;
        if (dead && kinds) {
            best = i;
            break;
        }
    }
    if (best < 0) {
        return false;
    }
    Unit *other         = &roam->units[best];
    other->hp           = other->max_hp;
    other->poison_left  = 0.0f;
    other->bleed_left   = 0.0f;
    other->frozen_until = 0;
    if (!priest) {
        Roam__SetState(other, UNIT_IDLE, now); // back on its feet
        other->provoked = true;
    }
    char sound[40];
    snprintf(sound, sizeof(sound), "%s",
             shaman || fetish ? "fallenshaman_resurrect"
             : mummy          ? "greatermummy_resurrect_1"
                              : "overseer_healcast_1");
    Sounds_PlayAt(sound, SOUND_KIND_SFX, unit->x - roam->player.x, unit->y - roam->player.y);
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, other->x, other->y);
    Roam__ClearPath(unit);
    Roam__SetState(unit, UNIT_ATTACK, now); // (its spell's swing)
    unit->ranged_swing = true;
    unit->next_attack  = now + ATTACK_COOLDOWN;
    unit->next_raise   = now + (priest ? 4000 : 6000) + (uint64_t)Roam__Roll(0, 3000);
    return true;
}

// The rarer monsters' ways: a curse on you (Succubus witches, the Summoner, Doom Knights: Amplify Damage, Weaken,
// Decrepify, or one of them), teleporting next to you (Zakarum priests, imps), leaping at you (Sand Leapers).
// target_id: 0 our own player, else another player's id (their game is told: MSG_AFFLICT)
static bool Roam__MonsterSpecial(SceneRoam *roam, Unit *unit, const float tx, const float ty, const float distance,
                                 const int target_id, const uint64_t now) {
    if (now < unit->next_special) {
        return false;
    }
    const bool mine = target_id == 0;
    if (unit->hex != NULL && distance <= AI_CAST_RANGE &&
        (!mine || (Roam__IsAlive(&roam->player) && (roam->player.curse == NULL || now >= roam->player.curse_until))) &&
        Map_HasClearLine(roam->map, unit->x, unit->y, tx, ty)) {
        unit->next_special    = now + 5000 + (uint64_t)Roam__Roll(0, 4000);
        const SkillRow *curse = unit->hex2 != NULL && Roam__Roll(0, 1) == 1 ? unit->hex2 : unit->hex;
        if (unit->hex_any) {
            static const char *const curses[3] = {"Amplify Damage", "Weaken", "Decrepify"};
            const SkillRow          *picked    = SkillData_Get(curses[Roam__Roll(0, 2)]);
            curse                              = picked != NULL ? picked : curse;
        }
        const int ms = 6000 + (unit->hex_level * 1000);
        if (mine) {
            roam->player.curse       = curse;
            roam->player.curse_level = unit->hex_level;
            roam->player.curse_until = now + (uint64_t)ms;
        } else {
            Roam__SendAfflict(target_id, curse, unit->hex_level, ms);
        }
        Roam__TurnTowards(&unit->turn, unit->x, unit->y, tx, ty);
        Roam__ClearPath(unit);
        Roam__SetState(unit, UNIT_ATTACK, now);
        unit->ranged_swing = true;
        unit->next_attack  = now + ATTACK_COOLDOWN;
        if (curse->cast_sound[0] != '\0') {
            Sounds_PlayAt(curse->cast_sound, SOUND_KIND_SFX, unit->x - roam->player.x, unit->y - roam->player.y);
        }
        return true;
    }
    if (unit->move_skill == MOVE_SKILL_TELEPORT && distance > 5.0f && distance < AI_CAST_RANGE * 2.0f) {
        // Next to you, in a blink
        unit->next_special = now + 3000 + (uint64_t)Roam__Roll(0, 3000);
        const float angle  = (float)Roam__Roll(0, 359) * 0.01745f;
        float       x;
        float       y;
        Roam__ArrivalSpot(roam->map, tx + (cosf(angle) * 2.5f), ty + (sinf(angle) * 2.5f), &x, &y);
        Roam__SpawnEffect(roam, "teleport", unit->x, unit->y, 0.0f, 1.0f);
        unit->x = unit->target_x = x;
        unit->y = unit->target_y = y;
        Roam__ClearPath(unit);
        return true;
    }
    if (unit->move_skill == MOVE_SKILL_BURROW && unit->state == UNIT_IDLE &&
        (distance > 4.0f || Roam__Roll(1, 3) == 1)) {
        // Down under the ground for a moment, to come up by you (Roam__Burrowed)
        unit->burrowed     = true;
        unit->next_special = now + 1200 + (uint64_t)Roam__Roll(0, 1800);
        Roam__ClearPath(unit);
        return true;
    }
    if (unit->move_skill == MOVE_SKILL_LEAP && distance > 3.0f && distance < 9.0f &&
        Map_HasClearLine(roam->map, unit->x, unit->y, tx, ty)) {
        // At you, through the air: lands a step short
        unit->next_special = now + 4000 + (uint64_t)Roam__Roll(0, 3000);
        const float step   = (distance - 1.2f) / distance;
        float       x;
        float       y;
        Roam__ArrivalSpot(roam->map, unit->x + ((tx - unit->x) * step), unit->y + ((ty - unit->y) * step), &x, &y);
        unit->x = unit->target_x = x;
        unit->y = unit->target_y = y;
        Roam__ClearPath(unit);
        return true;
    }
    return false;
}

// A unique's aura (aura enchanted), pulsing once a second on our player near it: Holy Fire burns, Holy Shock
// shocks, Holy Freeze chills (Might strengthens its own blows)
static void Roam__MonsterAura(SceneRoam *roam, Unit *unit, const uint64_t now) {
    if (unit->aura == NULL || !roam->game || now < unit->aura_pulse) {
        return;
    }
    unit->aura_pulse   = now + 1000;
    Unit       *player = &roam->player;
    const char *id     = unit->aura->id;
    const int   level  = unit->aura_level;
    // The other players near it: their games are told
    for (int o = 0; o < roam->other_count && roam->online; o++) {
        const Unit *other = &roam->others[o];
        if (!Roam__OtherAlive(other) || Roam__Distance(unit->x, unit->y, other->x, other->y) > 6.0f) {
            continue;
        }
        if (strcmp(id, "Holy Freeze") == 0) {
            Roam__SendAfflict(roam->other_ids[o], NULL, level, 1100);
        } else if (strcmp(id, "Holy Fire") == 0 || strcmp(id, "Holy Shock") == 0) {
            int damage[ELEMENT_COUNT] = {0};
            damage[strcmp(id, "Holy Fire") == 0 ? ELEMENT_FIRE : ELEMENT_LIGHTNING] =
                Roam__Roll(level * 2, (level * 5) + 2);
            Roam__SendHitPlayer(roam->other_ids[o], false, 100000, unit->level, damage); // (an aura doesn't miss)
        }
    }
    if (!Roam__IsAlive(player) || Roam__Distance(unit->x, unit->y, player->x, player->y) > 6.0f) {
        return;
    }
    if (strcmp(id, "Holy Freeze") == 0) {
        Roam__ChillMe(roam, 1100, now);
    } else if (strcmp(id, "Conviction") == 0) { // your resistances lowered while you're near it
        roam->conviction_until = now + 1100;
        roam->conviction       = 25 + (5 * level) < 85 ? 25 + (5 * level) : 85;
    } else if (strcmp(id, "Holy Fire") == 0 || strcmp(id, "Holy Shock") == 0) {
        const Element element = strcmp(id, "Holy Fire") == 0 ? ELEMENT_FIRE : ELEMENT_LIGHTNING;
        const int     damage  = Roam__ElementTaken(roam, element, Roam__Roll(level * 2, (level * 5) + 2));
        if (damage > 0) {
            Roam__Damage(roam, player, damage, now);
        }
    }
}

// A monster under the ground: it comes up by you after a moment (or where it is, if you're not near)
static void Roam__Burrowed(SceneRoam *roam, Unit *unit, const bool player_alive, const uint64_t now) {
    if (now < unit->next_special) {
        return;
    }
    const Unit *player = &roam->player;
    if (player_alive && Roam__Distance(unit->x, unit->y, player->x, player->y) < AGGRO_RANGE * 1.5f) {
        const float angle = (float)Roam__Roll(0, 359) * 0.01745f;
        float       x;
        float       y;
        Roam__ArrivalSpot(roam->map, player->x + (cosf(angle) * 2.0f), player->y + (sinf(angle) * 2.0f), &x, &y);
        unit->x = unit->target_x = x;
        unit->y = unit->target_y = y;
        Roam__TurnTowards(&unit->turn, x, y, player->x, player->y);
    }
    unit->burrowed     = false;
    unit->next_special = now + 5000 + (uint64_t)Roam__Roll(0, 4000);
    Roam__SetState(unit, UNIT_IDLE, now);
}

// The Fallen: when one of theirs falls nearby, the rest scatter for a moment
static bool Roam__MonsterFlees(SceneRoam *roam, Unit *unit, const float tx, const float ty, const uint64_t now,
                               const float seconds) {
    const bool fallen =
        strncmp(unit->id, "fallen", 6) == 0 && strncmp(unit->id, "fallenshaman", 12) != 0 && unit->rank == 0;
    if (!fallen && now >= unit->flee_until) {
        return false; // (anything else only runs from a blow that makes it: Hit Causes Monster to Flee)
    }
    if (now >= unit->flee_until) {
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *other = &roam->units[i];
            if (other != unit && other->state == UNIT_DYING && strncmp(other->id, "fallen", 6) == 0 &&
                Roam__Distance(other->x, other->y, unit->x, unit->y) < 6.0f && other->frame <= 1) {
                unit->flee_until = now + 1800;
                break;
            }
        }
    }
    if (now >= unit->flee_until) {
        return false;
    }
    Roam__ClearPath(unit);
    const float ax = unit->x + (unit->x - tx);
    const float ay = unit->y + (unit->y - ty);
    Roam__TurnTowards(&unit->turn, unit->x, unit->y, ax, ay);
    Roam__SetState(unit, Roam__Step(roam->map, unit, ax, ay, unit->speed * 1.2f, seconds) ? UNIT_WALK : UNIT_IDLE, now);
    return true;
}

// One of a pack is hit: the others near it come too
static void Roam__CallPack(SceneRoam *roam, Unit *unit) {
    if (!unit->provoked || unit->pack_called) {
        return;
    }
    unit->pack_called = true;
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *other = &roam->units[i];
        if (other != unit && !other->provoked && Roam__IsAlive(other) && Roam__IsCombatant(other) && !other->npc &&
            !other->ally && Roam__Distance(other->x, other->y, unit->x, unit->y) <= AI_PACK_RANGE) {
            other->provoked = true;
        }
    }
}
