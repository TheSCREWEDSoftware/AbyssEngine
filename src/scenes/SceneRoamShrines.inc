// Included by SceneRoam.c: shrines and wells.
//
// A shrine (objects.txt OperateFn 2) does one of the game's shrine effects (shrines.txt by Code). Its Parm0 says which
// kind: 1 a health shrine, 2 a mana shrine, 3 any of the others (picked by their rarity). A booster lasts its
// "Duration in frames" (25 a second) and only one is on at a time, like the game. Wells (OperateFn 22) fill life,
// mana and stamina.

typedef struct ShrineInfo {
    int         code;
    const char *name;
    const char *state; // states.txt, shown on the character while it lasts
} ShrineInfo;

static const ShrineInfo shrine_infos[] = {
    {1,  "Refilling Shrine",        NULL                     },
    {2,  "Health Shrine",           NULL                     },
    {3,  "Mana Shrine",             NULL                     },
    {6,  "Armor Shrine",            "shrine_armor"           },
    {7,  "Combat Shrine",           "shrine_combat"          },
    {8,  "Fire Resist Shrine",      "shrine_resist_fire"     },
    {9,  "Cold Resist Shrine",      "shrine_resist_cold"     },
    {10, "Lightning Resist Shrine", "shrine_resist_lightning"},
    {11, "Poison Resist Shrine",    "shrine_resist_poison"   },
    {12, "Skill Shrine",            "shrine_skill"           },
    {13, "Mana Recharge Shrine",    "shrine_mana_regen"      },
    {14, "Stamina Shrine",          "shrine_stamina"         },
    {15, "Experience Shrine",       "shrine_experience"      },
    {18, "Gem Shrine",              NULL                     },
    {19, "Fire Shrine",             NULL                     },
    {20, "Monster Shrine",          NULL                     },
    {17, "Portal Shrine",           NULL                     },
    {21, "Exploding Shrine",        NULL                     },
    {22, "Poison Shrine",           NULL                     },
};
#define SHRINE_INFO_COUNT (int)(sizeof(shrine_infos) / sizeof(shrine_infos[0]))

static const ShrineInfo *Roam__ShrineInfo(const int code) {
    for (int i = 0; i < SHRINE_INFO_COUNT; i++) {
        if (shrine_infos[i].code == code) {
            return &shrine_infos[i];
        }
    }
    return NULL;
}

// shrines.txt's row for a code: its two arguments, how long it lasts (frames), how rare it is and how many minutes
// before the shrine can be used again (0: never)
static int  shrine_reset_minutes = 0; // (of the last row looked up)
static bool Roam__ShrineRow(const int code, int *arg0, int *arg1, int *frames, int *rarity) {
    static DataTable *table = NULL;
    if (table == NULL) {
        table = DataTable_Load("/data/global/excel/shrines.txt");
        if (table == NULL) {
            return false;
        }
    }
    const int c_code = DataTable_GetColumnIndex(table, "Code");
    for (int r = 0; r < DataTable_GetRowCount(table); r++) {
        if (DataTable_GetInt(table, r, c_code) == code) {
            *arg0                = DataTable_GetInt(table, r, DataTable_GetColumnIndex(table, "Arg0"));
            *arg1                = DataTable_GetInt(table, r, DataTable_GetColumnIndex(table, "Arg1"));
            *frames              = DataTable_GetInt(table, r, DataTable_GetColumnIndex(table, "Duration in frames"));
            *rarity              = DataTable_GetInt(table, r, DataTable_GetColumnIndex(table, "rarity"));
            shrine_reset_minutes = DataTable_GetInt(table, r, DataTable_GetColumnIndex(table, "reset time in minutes"));
            return true;
        }
    }
    return false;
}

// Which shrine this one is: the health and mana kinds are themselves; the rest one of the others, the rarer ones
// less often. Online, the Monster Shrine is left out (the host runs the monsters).
static int Roam__PickShrine(const SceneRoam *roam, const int kind) {
    if (kind == 1) {
        return 2;
    }
    if (kind == 2) {
        return 3;
    }
    int codes[SHRINE_INFO_COUNT];
    int weights[SHRINE_INFO_COUNT];
    int count = 0;
    int total = 0;
    for (int i = 0; i < SHRINE_INFO_COUNT; i++) {
        const int code = shrine_infos[i].code;
        int       arg0, arg1, frames, rarity;
        if (code == 2 || code == 3 || (code == 20 && roam->online && !Roam__RunsMonsters(roam)) ||
            !Roam__ShrineRow(code, &arg0, &arg1, &frames, &rarity)) {
            continue;
        }
        codes[count]    = code;
        weights[count]  = rarity >= 1 && rarity <= 3 ? 4 - rarity : 1;
        total          += weights[count++];
    }
    if (total <= 0) {
        return 1;
    }
    int roll = (int)(((unsigned)rand() + (unsigned)SDL_GetTicks64()) % (unsigned)total); // (the builder re-seeds rand)
    for (int i = 0; i < count; i++) {
        if (roll < weights[i]) {
            return codes[i];
        }
        roll -= weights[i];
    }
    return codes[count - 1];
}

// The booster on now (0: none)
static int Roam__ShrineBoost(const SceneRoam *roam, const uint64_t now) {
    return roam->shrine_code != 0 && now < roam->shrine_until ? roam->shrine_code : 0;
}

// The booster's stats onto the character (Roam__UpdateSkillBonuses, each frame)
static void Roam__ShrineBonuses(SceneRoam *roam, const uint64_t now) {
    const int code = Roam__ShrineBoost(roam, now);
    if (code == 0) {
        roam->shrine_code = 0;
        return;
    }
    int *bonus = roam->character->bonus;
    switch (code) {
    case 6:
        bonus[BONUS_DEFENSE_PCT] += roam->shrine_arg0;
        break;
    case 7:
        bonus[BONUS_ATTACK_PCT] += roam->shrine_arg0;
        bonus[BONUS_DAMAGE_PCT] += roam->shrine_arg1;
        break;
    case 8:
    case 9:
    case 10:
    case 11:
        bonus[BONUS_RESIST_FIRE + (code - 8)] += roam->shrine_arg0;
        break;
    case 12:
        bonus[BONUS_ALL_SKILLS] += roam->shrine_arg0;
        break;
    case 13:
        bonus[BONUS_MANA_REGEN_PCT] += roam->shrine_arg0;
        break;
    default:
        break;
    }
}

// An item onto the ground by (x, y) (multiplayer: the server puts it there, for everyone)
static void Roam__DropItemNear(SceneRoam *roam, const Item *item, const float x, const float y) {
    float spot_x;
    float spot_y;
    Roam__FreeSpotNear(roam, x, y, &spot_x, &spot_y);
    if (roam->online) {
        Roam__PutOnGround(roam, item, spot_x, spot_y);
    } else {
        Roam__AddGroundItem(roam, item, Roam__MapKey(roam), spot_x, spot_y, true, 0);
    }
}

// The Gem Shrine: a gem you carry one grade up (gems.txt: seven kinds, five grades each, chipped to perfect), or a
// chipped one when you have none to raise
static void Roam__GemShrine(SceneRoam *roam, const Unit *shrine) {
    static DataTable *gems = NULL;
    if (gems == NULL) {
        gems = DataTable_Load("/data/global/excel/gems.txt");
        if (gems == NULL) {
            return;
        }
    }
    const int c_code     = DataTable_GetColumnIndex(gems, "code");
    int       kinds      = DataTable_GetRowCount(gems) / 5;
    kinds                = kinds > 7 ? 7 : kinds;
    Character *character = roam->character;
    for (int i = 0; i < character->inventory_count; i++) {
        Item *item = &character->inventory[i].item;
        for (int row = 0; row < kinds * 5; row++) {
            if ((row % 5) < 4 && strcmp(item->code, DataTable_GetString(gems, row, c_code)) == 0) {
                snprintf(item->code, sizeof(item->code), "%s", DataTable_GetString(gems, row + 1, c_code));
                Roam__ShowMessage(roam, "Gem Shrine: one of your gems is better now.");
                return;
            }
        }
    }
    if (kinds <= 0) {
        return;
    }
    const Item gem = Inventory_NewItem(DataTable_GetString(gems, (rand() % kinds) * 5, c_code));
    Roam__DropItemNear(roam, &gem, shrine->x, shrine->y);
    Roam__ShowMessage(roam, "Gem Shrine");
}

// Using a shrine: its effect on you (multiplayer: on whoever the server let use it)
static void Roam__UseShrine(SceneRoam *roam, Unit *shrine, const uint64_t now) {
    if (!roam->game || roam->character == NULL || (roam->online && !roam->use_is_ours)) {
        return;
    }
    const int         code = Roam__PickShrine(roam, shrine->object.parm[0]);
    const ShrineInfo *info = Roam__ShrineInfo(code);
    int               arg0 = 0, arg1 = 0, frames = 0, rarity = 0;
    shrine_reset_minutes = 0;
    Roam__ShrineRow(code, &arg0, &arg1, &frames, &rarity);
    // Some come back after a few minutes, like the game's (Single Player: the server keeps a multiplayer game's)
    shrine->ready_at =
        !roam->online && shrine_reset_minutes > 0 ? SDL_GetTicks64() + ((uint64_t)shrine_reset_minutes * 60000) : 0;
    Character  *character = roam->character;
    const float max_mana  = (float)Character_MaxMana(character);
    char        message[96];
    snprintf(message, sizeof(message), "%s", info != NULL ? info->name : "Shrine");
    switch (code) {
    case 1:
        roam->player.hp = roam->player.max_hp;
        roam->mana      = max_mana;
        break;
    case 2:
        roam->player.hp = roam->player.max_hp;
        break;
    case 3:
        roam->mana = max_mana;
        break;
    case 18:
        Roam__GemShrine(roam, shrine);
        return;
    case 17:
        Roam__OpenTownPortal(roam); // a Town Portal, free
        break;
    case 21:
    case 22: {
        // Arg0 to Arg1 throwing potions (exploding / gas), stronger in later areas
        static const char *const potions[2][3] = {
            {"ops", "opm", "opl"},
            {"gps", "gpm", "gpl"},
        };
        const int grade = roam->area_level >= 75 ? 2 : (roam->area_level >= 40 ? 1 : 0);
        const int count = Roam__Roll(arg0 > 0 ? arg0 : 5, arg1 > arg0 ? arg1 : 10);
        for (int i = 0; i < count; i++) {
            Item potion     = Inventory_NewItem(potions[code - 21][grade]);
            potion.quantity = potion.quantity > 0 ? potion.quantity : 1;
            Roam__DropItemNear(roam, &potion, shrine->x, shrine->y);
        }
        break;
    }
    case 19: {
        // Everything near the shrine loses half the life it has (you too)
        const float radius = 12.0f;
        for (int i = 0; i < roam->unit_count && Roam__RunsMonsters(roam); i++) {
            Unit *unit = &roam->units[i];
            if (Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally && Roam__IsCombatant(unit) &&
                Roam__Distance(unit->x, unit->y, shrine->x, shrine->y) < radius && unit->hp > 1) {
                Roam__Damage(roam, unit, unit->hp / 2, now);
            }
        }
        if (roam->player.hp > 1) {
            roam->player.hp -= roam->player.hp / 2;
        }
        Roam__SpawnEffect(roam, "firebolt", shrine->x, shrine->y, 0.0f, 1.0f);
        break;
    }
    case 20: {
        // The nearest monster becomes a champion
        int   nearest  = -1;
        float distance = 30.0f;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            if (Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally && Roam__IsCombatant(unit) &&
                unit->rank == RANK_NORMAL && Roam__Distance(unit->x, unit->y, shrine->x, shrine->y) < distance) {
                distance = Roam__Distance(unit->x, unit->y, shrine->x, shrine->y);
                nearest  = i;
            }
        }
        if (nearest >= 0) {
            Unit *unit           = &roam->units[nearest];
            unit->rank           = RANK_CHAMPION;
            unit->hp            *= 3;
            unit->max_hp        *= 3;
            unit->damage_min    += unit->damage_min / 4;
            unit->damage_max    += unit->damage_max / 4;
            unit->attack_rating += unit->attack_rating / 4;
            unit->experience    *= 3;
            unit->level         += 2;
        }
        break;
    }
    default:
        // A booster: replaces the one on
        roam->shrine_code  = code;
        roam->shrine_arg0  = arg0;
        roam->shrine_arg1  = arg1;
        roam->shrine_until = now + ((uint64_t)(frames > 0 ? frames : 2400) * 40);
        snprintf(message, sizeof(message), "%s (%d seconds)", info != NULL ? info->name : "Shrine",
                 (frames > 0 ? frames : 2400) / 25);
        break;
    }
    Roam__ShowMessage(roam, message);
}

// Shrines whose time has come round light up again
static void Roam__RechargeShrines(SceneRoam *roam, const uint64_t now) {
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (unit->is_object && unit->used && unit->ready_at != 0 && now >= unit->ready_at) {
            unit->used     = false;
            unit->ready_at = 0;
            snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.idle_mode);
            Roam__SetState(unit, UNIT_IDLE, now);
        }
    }
}

// A well: life, mana and stamina back
static void Roam__UseWell(SceneRoam *roam) {
    if (!roam->game || roam->character == NULL || (roam->online && !roam->use_is_ours)) {
        return;
    }
    roam->player.hp = roam->player.max_hp;
    roam->mana      = (float)Character_MaxMana(roam->character);
    roam->stamina   = (float)Character_MaxStamina(roam->character);
    roam->exhausted = false;
    Roam__ShowMessage(roam, "Well");
}
