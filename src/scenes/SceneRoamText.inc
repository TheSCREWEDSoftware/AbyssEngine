// SceneRoam: text mode (included at the end of SceneRoam.c; AbyssEngine --text). The game draws itself as characters
// for a terminal: the sub-tiles around you as a grid ('#' blocked, '.' ground, '>' a way out, letters for monsters,
// '@' you), a few lines of life, mana, messages and help below, and keys do what the mouse does in the game. The same
// game underneath: areas, seeds, combat, quests, saving.

#define TEXT_CELL_MAX 16 // bytes one grid cell can take with its colour

typedef struct TextCell {
    char ch;
    int  color; // ANSI: 30-37, +60 bright; 0 = default
} TextCell;

static int Roam__TextNearest(const SceneRoam *roam, const float range, const int kind) {
    // kind 0: a monster to fight; 1: something to use or someone to talk to
    int   best      = -1;
    float best_dist = range;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        const float d    = Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y);
        if (d >= best_dist || !Roam__IsAlive(unit)) {
            continue;
        }
        const bool fits = kind == 0 ? Roam__IsCombatant(unit) && !Roam__IsPeaceful(roam, unit) && !unit->ally
                                    : Roam__CanUse(unit) || (roam->game && Roam__CanTalk(roam, unit));
        if (fits) {
            best      = i;
            best_dist = d;
        }
    }
    return best;
}

static int Roam__TextNearestItem(const SceneRoam *roam, const float range) {
    int   best      = -1;
    float best_dist = range;
    for (int g = 0; g < roam->ground_count; g++) {
        const GroundItem *ground = &roam->ground[g];
        const float       d      = Roam__Distance(ground->x, ground->y, roam->player.x, roam->player.y);
        if (ground->map_key == Roam__MapKey(roam) && d < best_dist) {
            best      = g;
            best_dist = d;
        }
    }
    return best;
}

static TextCell Roam__TextUnit(const SceneRoam *roam, const Unit *unit) {
    TextCell cell = {'?', 37};
    if (unit->is_object) {
        switch (unit->object.operate_fn) {
        case 23:
            cell = (TextCell){'W', 96};
            break;
        case 32:
            cell = (TextCell){'S', 93};
            break;
        case 2:
            cell = (TextCell){'!', 95};
            break;
        case 8:
            cell = (TextCell){unit->used ? '\'' : '+', 33};
            break;
        default:
            cell = (TextCell){unit->used ? '_' : '=', 33};
            break;
        }
        return cell;
    }
    const char letter = unit->name[0] != '\0' ? unit->name[0] : unit->id[0];
    if (unit->ally) {
        return (TextCell){(char)(unit->hireling ? 'h' : 'm'), 92};
    }
    if (unit->npc) {
        return (TextCell){(char)(letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter), 36};
    }
    if (!Roam__IsAlive(unit)) {
        return (TextCell){'%', 31};
    }
    const char upper = (char)(letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter);
    switch (unit->rank) {
    case RANK_CHAMPION:
        return (TextCell){upper, 94};
    case RANK_UNIQUE:
    case RANK_SUPER_UNIQUE:
        return (TextCell){upper, 93};
    default:
        (void)roam;
        return (TextCell){upper, 91};
    }
}

int Game_TextRender(void *scene_ref, char *out, const int size, int cols, int rows) {
    SceneRoam *roam    = scene_ref;
    roam->text_mode    = true;
    cols               = cols < 40 ? 40 : (cols > 300 ? 300 : cols);
    rows               = rows < 12 ? 12 : (rows > 120 ? 120 : rows);
    const int map_rows = rows - 5;
    TextCell *grid     = calloc((size_t)(cols * map_rows), sizeof(TextCell));
    if (grid == NULL || roam->map == NULL) {
        free(grid);
        return snprintf(out, (size_t)size, "Loading...\n");
    }
    const int left  = (int)roam->player.x - (cols / 2);
    const int top   = (int)roam->player.y - (map_rows / 2);
    const int sub_w = roam->map->width * MAP_SUBTILES;
    const int sub_h = roam->map->height * MAP_SUBTILES;
    for (int r = 0; r < map_rows; r++) {
        for (int c = 0; c < cols; c++) {
            const int x    = left + c;
            const int y    = top + r;
            TextCell *cell = &grid[(r * cols) + c];
            if (x < 0 || y < 0 || x >= sub_w || y >= sub_h) {
                *cell = (TextCell){' ', 0};
            } else if (Map_ExitAt(roam->map, (float)x + 0.5f, (float)y + 0.5f) != NULL) {
                *cell = (TextCell){'>', 95};
            } else if (Map_IsWalkable(roam->map, x, y)) {
                *cell = (TextCell){'.', 90};
            } else {
                *cell = (TextCell){'#', 37};
            }
        }
    }
#define TEXT_PUT(fx, fy, value)                                                                                        \
    do {                                                                                                               \
        const int tc_ = (int)(fx) - left;                                                                              \
        const int tr_ = (int)(fy) - top;                                                                               \
        if (tc_ >= 0 && tr_ >= 0 && tc_ < cols && tr_ < map_rows) {                                                    \
            grid[(tr_ * cols) + tc_] = (value);                                                                        \
        }                                                                                                              \
    } while (0)
    for (int g = 0; g < roam->ground_count; g++) {
        const GroundItem *ground = &roam->ground[g];
        if (ground->map_key == Roam__MapKey(roam)) {
            TEXT_PUT(ground->x, ground->y, ((TextCell){strcmp(ground->item.code, "gld") == 0 ? '$' : '*', 97}));
        }
    }
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if ((unit->is_object && !unit->object.selectable) || unit->vanished) {
            continue;
        }
        TEXT_PUT(unit->x, unit->y, Roam__TextUnit(roam, unit));
    }
    for (int o = 0; o < roam->other_count; o++) {
        TEXT_PUT(roam->others[o].x, roam->others[o].y, ((TextCell){'&', 92}));
    }
    if (roam->corpse_key != 0 && roam->corpse_key == Roam__MapKey(roam)) {
        TEXT_PUT(roam->corpse.x, roam->corpse.y, ((TextCell){'%', 97}));
    }
    TEXT_PUT(roam->player.x, roam->player.y, ((TextCell){'@', 97}));
#undef TEXT_PUT

    // The grid, with a colour code only where it changes
    int at    = snprintf(out, (size_t)size, "\x1b[H");
    int color = -1;
    for (int r = 0; r < map_rows && at < size - (cols * TEXT_CELL_MAX); r++) {
        for (int c = 0; c < cols; c++) {
            const TextCell *cell = &grid[(r * cols) + c];
            if (cell->color != color) {
                at    += snprintf(out + at, (size_t)(size - at), "\x1b[%dm", cell->color);
                color  = cell->color;
            }
            out[at++] = cell->ch;
        }
        at    += snprintf(out + at, (size_t)(size - at), "\x1b[0m\x1b[K\n");
        color  = -1;
    }
    free(grid);

    // The panel: who you are, life and mana, the area; the newest message; what's near; the keys
    const Character   *c                   = roam->character;
    static const char *difficulty_names[3] = {"", " (Nightmare)", " (Hell)"};
    if (c != NULL) {
        at += snprintf(
            out + at, (size_t)(size - at),
            "\x1b[97m%s\x1b[0m, level %d %s   \x1b[91mLife %d/%d\x1b[0m   \x1b[94mMana %d/%d\x1b[0m   Gold %d   "
            "%s%s\x1b[K\n",
            c->name, c->level, Character_ClassName(c->class_id), roam->player.hp, roam->player.max_hp, (int)roam->mana,
            Character_MaxMana(c), c->gold, Roam__AreaName(roam), difficulty_names[Roam__Difficulty(roam)]);
    }
    const uint64_t now  = SDL_GetTicks64();
    at                 += snprintf(out + at, (size_t)(size - at), "%s\x1b[K\n",
                   roam->player.state == UNIT_DEAD ? "You died." : (now < roam->message_until ? roam->message : ""));
    if (roam->npc_menu >= 0) {
        at += snprintf(out + at, (size_t)(size - at), "%s:", roam->units[roam->npc_menu].name);
        for (int o = 0; o < roam->npc_option_count && at < size - 200; o++) {
            at += snprintf(out + at, (size_t)(size - at), "  %d) %s", o + 1,
                           Roam__NpcOptionText(roam, roam->npc_options[o]));
        }
        at += snprintf(out + at, (size_t)(size - at), "\x1b[K\n");
    } else {
        const int near = Roam__TextNearest(roam, 15.0f, 0);
        if (near >= 0) {
            const Unit *unit  = &roam->units[near];
            at               += snprintf(out + at, (size_t)(size - at), "Near: %s (%d%% life)\x1b[K\n", unit->name,
                           unit->max_hp > 0 ? unit->hp * 100 / unit->max_hp : 0);
        } else {
            at += snprintf(out + at, (size_t)(size - at), "\x1b[K\n");
        }
    }
    at += snprintf(out + at, (size_t)(size - at),
                   "\x1b[90mwasd qezc move  f fight  r/l skill  u use/talk  g pick up  1-4 potion  t town  Q quit"
                   "\x1b[0m\x1b[K\n\x1b[J");
    return at;
}

void Game_TextKey(void *scene_ref, const int key) {
    SceneRoam     *roam   = scene_ref;
    Unit          *player = &roam->player;
    const uint64_t now    = SDL_GetTicks64();
    if (roam->npc_menu >= 0 && key >= '1' && key <= '9') {
        const int option = key - '1';
        if (option < roam->npc_option_count) {
            if (roam->npc_options[option] == NPC_TRADE || roam->npc_options[option] == NPC_GAMBLE) {
                roam->npc_menu = -1;
                Roam__ShowMessage(roam, "Trading needs the game's screen (not in text mode yet).");
            } else {
                Roam__NpcChoose(roam, roam->npc_options[option]);
            }
        }
        return;
    }
    static const struct {
        int   key;
        float dx;
        float dy;
    } steps[] = {
        {'w', 0,  -1},
        {'s', 0,  1 },
        {'a', -1, 0 },
        {'d', 1,  0 },
        {'q', -1, -1},
        {'e', 1,  -1},
        {'z', -1, 1 },
        {'c', 1,  1 }
    };
    for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++) {
        if (key == steps[s].key && Roam__IsAlive(player)) {
            roam->npc_menu      = -1;
            roam->attack_target = -1;
            roam->use_target    = -1;
            roam->item_target   = -1;
            player->target_x    = player->x + (steps[s].dx * 4.0f);
            player->target_y    = player->y + (steps[s].dy * 4.0f);
            roam->moving        = true;
            Roam__ClearPath(player);
            return;
        }
    }
    switch (key) {
    case 'f': {
        const int target = Roam__TextNearest(roam, 20.0f, 0);
        if (target >= 0) {
            roam->attack_target   = target;
            roam->attack_in_place = false;
        } else {
            Roam__ShowMessage(roam, "Nothing to fight near you.");
        }
        break;
    }
    case 'r':
    case 'l': {
        const int target = Roam__TextNearest(roam, 20.0f, 0);
        const int skill  = key == 'r' ? roam->character->right_skill : roam->character->left_skill;
        if (target >= 0 && roam->game) {
            Roam__UseSkill(roam, skill, roam->units[target].x, roam->units[target].y, target, now);
        } else if (roam->game) {
            Roam__UseSkill(roam, skill, player->x + 3.0f, player->y, -1, now);
        }
        break;
    }
    case 'u': {
        const int target = Roam__TextNearest(roam, 12.0f, 1);
        if (target >= 0) {
            roam->use_target = target;
        } else {
            Roam__ShowMessage(roam, "Nothing to use or talk to near you.");
        }
        break;
    }
    case 'g': {
        const int item = Roam__TextNearestItem(roam, 15.0f);
        if (item >= 0) {
            roam->item_target = item;
        } else {
            Roam__ShowMessage(roam, "Nothing to pick up near you.");
        }
        break;
    }
    case '1':
    case '2':
    case '3':
    case '4': {
        Item drunk;
        if (roam->game && GameInventory_DrinkBelt(roam->character, key - '1', &drunk)) {
            Roam__Drink(roam, &drunk);
        }
        break;
    }
    case 't':
        if (roam->game && !roam->in_town && Roam__IsAlive(player)) {
            Roam__GoToTown(roam, roam->character->act);
        }
        break;
    case 'Q':
        if (roam->game) {
            Roam__ExitGame(roam);
        }
        break;
    default:
        break;
    }
}
