// Skills in the game (included by SceneRoam.c: it works on the scene's units and player directly).
//
// Casting: the player plays the skill's animation (skills.txt anim: SC for spells, A1 for bow and melee skills, TH
// for javelins, ...) and half way through the skill happens: missiles fly (Missiles.txt), a ring of them goes out
// (novas), something lands on the spot clicked (Meteor, Blizzard), a buff goes on, a curse falls on the monsters
// there, monsters are summoned to fight for you, or the next blow is a stronger one (Bash, Jab, ...). Auras are on
// while they're the right skill. Passives always count. What a skill adds to the character (defense, damage, speed,
// resistances, ...) goes into Character's bonus numbers, which its own sums use.

#define SKILL_DOT_PULSE_MS 1000  // auras pulse (and poison ticks) about this often
#define SKILL_HOLD_MS      120   // holding the button: cast again this soon after the last one ends
#define SKILL_CHAIN_RANGE  12.0f // Chain Lightning jumps to monsters this close
#define SKILL_SUMMON_LEASH 18.0f // summons come back to you from further than this
#define SKILL_CURSE_RADIUS 6.0f  // sub-tiles, when the skill doesn't say

static bool Roam__HasMode(SceneRoam *roam, const Unit *unit, const char *mode);
static void Roam__GainExperience(SceneRoam *roam, const Unit *victim);
static void Roam__SendKilled(SceneRoam *roam, int index, int killer);
static void Roam__ApplyCharacter(SceneRoam *roam);
static int  Roam__FindHireling(const SceneRoam *roam);

// ---------------------------------------------------------------------------------------------------------------------
// What skills add to the character

// The level a skill works at: its points and what worn items add (+skills)
static int Roam__SkillPoints(const SceneRoam *roam, const int skill) {
    if (roam->character == NULL || skill < 0 || skill >= CHARACTER_SKILLS) {
        return 0;
    }
    return roam->character->skill_levels[skill] + (roam->game ? Affixes_SkillBonus(roam->character, skill) : 0);
}

// A stat a skill gives (skills.txt's names for them) onto the character's bonus numbers
static void Roam__AddSkillStat(Character *character, const char *stat, const int value) {
    static const struct {
        const char    *name;
        CharacterBonus bonus;
    } stats[] = {
        {"skill_armor_percent",          BONUS_DEFENSE_PCT         },
        {"item_armor_percent",           BONUS_DEFENSE_PCT         },
        {"damagepercent",                BONUS_DAMAGE_PCT          },
        {"item_tohit_percent",           BONUS_ATTACK_PCT          },
        {"tohit",                        BONUS_ATTACK              },
        {"item_maxhp_percent",           BONUS_LIFE_PCT            },
        {"item_maxmana_percent",         BONUS_MANA_PCT            },
        {"skill_staminapercent",         BONUS_STAMINA_PCT         },
        {"skill_passive_staminapercent", BONUS_STAMINA_PCT         },
        {"velocitypercent",              BONUS_SPEED_PCT           },
        {"attackrate",                   BONUS_ATTACK_SPEED        },
        {"fireresist",                   BONUS_RESIST_FIRE         },
        {"coldresist",                   BONUS_RESIST_COLD         },
        {"lightresist",                  BONUS_RESIST_LIGHTNING    },
        {"poisonresist",                 BONUS_RESIST_POISON       },
        {"maxfireresist",                BONUS_MAX_RESIST_FIRE     },
        {"maxcoldresist",                BONUS_MAX_RESIST_COLD     },
        {"maxlightresist",               BONUS_MAX_RESIST_LIGHTNING},
        {"maxpoisonresist",              BONUS_MAX_RESIST_POISON   },
        {"manarecoverybonus",            BONUS_MANA_REGEN_PCT      },
        {"toblock",                      BONUS_BLOCK               },
        {"firemindam",                   BONUS_FIRE_MIN            },
        {"firemaxdam",                   BONUS_FIRE_MAX            },
        {"coldmindam",                   BONUS_COLD_MIN            },
        {"coldmaxdam",                   BONUS_COLD_MAX            },
        {"lightmindam",                  BONUS_LIGHTNING_MIN       },
        {"lightmaxdam",                  BONUS_LIGHTNING_MAX       },
        {"poisonmindam",                 BONUS_POISON_MIN          },
        {"poisonmaxdam",                 BONUS_POISON_MAX          },
        {"strength",                     BONUS_STRENGTH            },
        {"dexterity",                    BONUS_DEXTERITY           },
        {"thorns_percent",               BONUS_THORNS_PCT          },
        {"passive_critical_strike",      BONUS_CRITICAL            },
        {"passive_dodge",                BONUS_DODGE               },
        {"passive_avoid",                BONUS_AVOID               },
        {"passive_evade",                BONUS_AVOID               },
        {"skill_pierce",                 BONUS_PIERCE              },
        {"passive_fire_mastery",         BONUS_FIRE_SKILL_PCT      },
        {"passive_ltng_mastery",         BONUS_LIGHTNING_SKILL_PCT },
        {"passive_cold_pierce",          BONUS_COLD_PIERCE         },
    };
    for (size_t i = 0; i < sizeof(stats) / sizeof(stats[0]); i++) {
        if (strcmp(stats[i].name, stat) == 0) {
            character->bonus[stats[i].bonus] += value;
            return;
        }
    }
}

// The weapon masteries count with their own kind of weapon only
static bool Roam__MasteryApplies(const Character *character, const char *skill) {
    static const struct {
        const char *skill;
        const char *type;
    } masteries[] = {
        {"Sword Mastery",    "swor"},
        {"Axe Mastery",      "axe" },
        {"Mace Mastery",     "blun"},
        {"Pole Arm Mastery", "pole"},
        {"Spear Mastery",    "spea"},
        {"Throwing Mastery", "thro"},
        {"Claw Mastery",     "h2h" },
    };
    for (size_t i = 0; i < sizeof(masteries) / sizeof(masteries[0]); i++) {
        if (strcmp(masteries[i].skill, skill) == 0) {
            const ItemBase *weapon = Inventory_Weapon(character);
            return weapon != NULL && Items_IsA(weapon, masteries[i].type);
        }
    }
    return true;
}

static void Roam__AddSkillStats(Character *character, const SkillRow *s, const int level, const bool aura_stats,
                                const bool passive_stats) {
    if (aura_stats) {
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            if (s->aura_stat[a][0] != '\0') {
                Roam__AddSkillStat(character, s->aura_stat[a], SkillData_Eval(character, s, level, s->aura_calc[a]));
            }
        }
    }
    if (passive_stats) {
        for (int p = 0; p < SKILL_PASSIVE_STATS; p++) {
            if (s->passive_stat[p][0] == '\0') {
                continue;
            }
            int value = SkillData_Eval(character, s, level, s->passive_calc[p]);
            if (strncmp(s->passive_stat[p], "passive_mastery", 15) == 0) {
                // A mastery's attack rating and damage: percent, onto the character's
                value = Roam__MasteryApplies(character, s->id) ? value : 0;
                character->bonus[strstr(s->passive_stat[p], "_th") != NULL ? BONUS_ATTACK_PCT : BONUS_DAMAGE_PCT] +=
                    value;
                continue;
            }
            Roam__AddSkillStat(character, s->passive_stat[p], value);
        }
    }
}

// The skill on the right (or left) button that's an aura: on while it's there, like the game's
static const SkillRow *Roam__ActiveAura(const SceneRoam *roam, int *level) {
    const int hands[2] = {roam->character->right_skill, roam->character->left_skill};
    for (int h = 0; h < 2; h++) {
        const SkillRow *s = SkillData_ForCharacter(roam->character, hands[h]);
        if (s != NULL && s->kind == SKILL_KIND_AURA && Roam__SkillPoints(roam, hands[h]) > 0) {
            *level = Roam__SkillPoints(roam, hands[h]);
            return s;
        }
    }
    return NULL;
}

// Passives, the buffs on, the aura: their stats onto the character (each frame; cheap sums)
static void Roam__UpdateSkillBonuses(SceneRoam *roam, const uint64_t now) {
    if (!roam->game) {
        return;
    }
    Character *character = roam->character;
    int        before[BONUS_COUNT];
    memcpy(before, character->bonus, sizeof(before));
    memset(character->bonus, 0, sizeof(character->bonus));
    const SkillClass *skills = Skills_ForClass(character->class_id);
    for (int k = 0; k < skills->count && k < CHARACTER_SKILLS; k++) {
        const int       level = character->skill_levels[k] + Affixes_SkillBonus(character, k);
        const SkillRow *s     = level > 0 ? SkillData_Get(skills->skills[k].id) : NULL;
        if (s != NULL && s->kind == SKILL_KIND_PASSIVE) {
            Roam__AddSkillStats(character, s, level, false, true);
        }
    }
    // Buffs: their stats while they last (Bone Armor and Cyclone Armor soak up damage instead)
    int kept = 0;
    for (int b = 0; b < roam->buff_count; b++) {
        RoamBuff *buff = &roam->buffs[b];
        if (buff->until != 0 && now >= buff->until) {
            continue;
        }
        roam->buffs[kept++] = *buff;
        if (strcmp(buff->skill->aura_stat[0], "bonearmor") != 0) {
            Roam__AddSkillStats(character, buff->skill, buff->level, true, false);
        }
    }
    roam->buff_count           = kept;
    int             aura_level = 0;
    const SkillRow *aura       = Roam__ActiveAura(roam, &aura_level);
    if (aura != NULL) {
        Roam__AddSkillStats(character, aura, aura_level, true, true);
    }
    // Your hireling's aura (the act 2 mercenaries: Prayer, Defiance, Blessed Aim, Might, ...), while they're alive and
    // near you
    const int hireling = character->hireling > 0 && !character->hireling_dead ? Roam__FindHireling(roam) : -1;
    if (hireling >= 0 && Roam__IsAlive(&roam->units[hireling]) &&
        Roam__Distance(roam->units[hireling].x, roam->units[hireling].y, roam->player.x, roam->player.y) <= 30.0f) {
        HireSkill skills[HIRE_SKILLS];
        const int count = Hirelings_Skills(character->hireling - 1, character->hireling_level, skills, HIRE_SKILLS);
        for (int s = 0; s < count; s++) {
            const SkillRow *row = SkillData_Get(skills[s].name);
            if (row != NULL && row->kind == SKILL_KIND_AURA) {
                Roam__AddSkillStats(character, row, skills[s].level, true, true);
            }
        }
    }
    if (memcmp(before, character->bonus, sizeof(before)) != 0) {
        Roam__ApplyCharacter(roam); // defense, attack rating, damage, life and mana
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Hurting monsters with skills

// A monster's resistance, less what the curse on it takes off (Amplify Damage, Lower Resist, Conviction) and Cold
// Mastery
static int Roam__MonsterResist(const SceneRoam *roam, const Unit *unit, const Element element, const uint64_t now) {
    int resist = unit->resist[element];
    if (unit->curse != NULL && now < unit->curse_until) {
        static const char *const names[ELEMENT_COUNT] = {"damageresist", "fireresist", "coldresist", "lightresist",
                                                         "poisonresist"};
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            if (strcmp(unit->curse->aura_stat[a], names[element]) == 0 ||
                (element == ELEMENT_COLD && strcmp(unit->curse->id, "Lower Resist") == 0 &&
                 strcmp(unit->curse->aura_stat[a], "fireresist") == 0) ||
                (element == ELEMENT_POISON && strcmp(unit->curse->id, "Lower Resist") == 0 &&
                 strcmp(unit->curse->aura_stat[a], "fireresist") == 0)) {
                resist += SkillData_Eval(roam->character, unit->curse, unit->curse_level, unit->curse->aura_calc[a]);
                break;
            }
        }
    }
    if (element == ELEMENT_COLD && roam->game && resist < 100) {
        resist -= roam->character->bonus[BONUS_COLD_PIERCE];
    }
    return resist;
}

// Skill damage onto a monster: each kind against its resistance, cold slows it, poison works over time. ours: the
// kill is ours (experience)
static void Roam__SkillHit(SceneRoam *roam, Unit *unit, const int damage[ELEMENT_COUNT], const int cold_frames,
                           const int poison_frames, const bool ours, const uint64_t now) {
    if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally) {
        return;
    }
    int total = 0;
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        if (damage[e] <= 0) {
            continue;
        }
        const int resisted = Roam__Resisted(damage[e], Roam__MonsterResist(roam, unit, (Element)e, now));
        if (e == ELEMENT_POISON && poison_frames > 0) {
            // Over its length: the rest comes a little at a time
            unit->poison_left = (float)resisted;
            unit->poison_rate = (float)resisted * 25.0f / (float)poison_frames;
            unit->poison_ours = ours;
            continue;
        }
        if (e == ELEMENT_COLD && cold_frames > 0 && resisted > 0) {
            unit->chill_until = now + (uint64_t)(cold_frames * 40);
        }
        total += resisted;
    }
    unit->provoked = true;
    if (total <= 0) {
        return;
    }
    if (!Roam__RunsMonsters(roam)) {
        // Multiplayer: the map's owner takes it off
        NetWriter *writer = malloc(sizeof(NetWriter));
        if (writer != NULL) {
            NetWriter_Begin(writer);
            NetWriter_I32(writer, Roam__MapKey(roam));
            NetWriter_U16(writer, (uint16_t)(unit - roam->units));
            NetWriter_I32(writer, total);
            NetClient_Send(MSG_HIT_MONSTER, writer);
            free(writer);
        }
        Roam__UnitSound(roam, unit, MONSTER_SOUND_HIT);
        return;
    }
    const bool was_alive = Roam__IsAlive(unit);
    Roam__Damage(roam, unit, total, now);
    if (was_alive && !Roam__IsAlive(unit) && ours) {
        if (roam->online) {
            Roam__SendKilled(roam, (int)(unit - roam->units), NetClient_MyId());
        } else {
            Roam__GainExperience(roam, unit);
        }
    }
}

// Poison ticking (ours or an ally's: the kill is ours)
static void Roam__UpdateAilments(SceneRoam *roam, Unit *unit, const float seconds, const uint64_t now) {
    if (unit->poison_left <= 0.0f || !Roam__IsAlive(unit) || !Roam__RunsMonsters(roam)) {
        return;
    }
    const float amount  = fminf(unit->poison_left, unit->poison_rate * seconds);
    unit->poison_left  -= amount;
    unit->poison_carry += amount;
    if (unit->poison_carry >= 1.0f) {
        const int  whole      = (int)unit->poison_carry;
        const bool was_alive  = Roam__IsAlive(unit);
        unit->poison_carry   -= (float)whole;
        unit->hp              = unit->hp - whole > 0 ? unit->hp - whole : 0;
        if (unit->hp <= 0) {
            unit->hp = 1;
            Roam__Damage(roam, unit, 1, now); // dies the usual way
            if (was_alive && !Roam__IsAlive(unit) && unit->poison_ours) {
                if (roam->online) {
                    Roam__SendKilled(roam, (int)(unit - roam->units), NetClient_MyId());
                } else {
                    Roam__GainExperience(roam, unit);
                }
            }
        }
    }
}

// How fast a monster goes: halved while chilled, slowed by Decrepify / Holy Freeze
static float Roam__UnitSpeedFactor(const SceneRoam *roam, const Unit *unit, const uint64_t now) {
    float factor = now < unit->chill_until ? 0.5f : 1.0f;
    if (unit->curse != NULL && now < unit->curse_until) {
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            if (strcmp(unit->curse->aura_stat[a], "velocitypercent") == 0) {
                factor *= fmaxf(0.1f, 1.0f + ((float)SkillData_Eval(roam->character, unit->curse, unit->curse_level,
                                                                    unit->curse->aura_calc[a]) /
                                              100.0f));
            }
        }
    }
    return factor;
}

// A monster's blow, after its curse (Weaken: less damage) and against the player's skills (Bone Armor soaks it up,
// Thorns gives some back)
static int Roam__MonsterDamageFactor(const SceneRoam *roam, const Unit *unit, const uint64_t now) {
    int percent = 100;
    if (unit->curse != NULL && now < unit->curse_until) {
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            if (strcmp(unit->curse->aura_stat[a], "damagepercent") == 0) {
                percent += SkillData_Eval(roam->character, unit->curse, unit->curse_level, unit->curse->aura_calc[a]);
            }
        }
    }
    return percent > 0 ? percent : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Missiles

static const char *Roam__Palette(const SceneRoam *roam) {
    static const char *const palettes[5] = {PALETTE_ACT_1, PALETTE_ACT_2, PALETTE_ACT_3, PALETTE_ACT_4, PALETTE_ACT_5};
    const int                act         = Roam__Act(roam);
    return palettes[act >= 0 && act < 5 ? act : 0];
}

// The picture's direction for a way across the ground (the game's directions are interleaved like the units')
static int Roam__MissileDirection(const float dx, const float dy, const int directions) {
    static const int order_16[16] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};
    const float      sx           = (dx - dy) * 16.0f; // on screen
    const float      sy           = (dx + dy) * 8.0f;
    float            angle        = atan2f(-sx, sy); // 0 = down the screen, clockwise
    if (angle < 0.0f) {
        angle += 2.0f * 3.14159265f;
    }
    if (directions >= 32) {
        const int t = (int)lroundf(angle / (2.0f * 3.14159265f / 32.0f)) % 32;
        return (t % 2 == 0) ? order_16[t / 2] * 2 % 32 : 16 + (t / 2);
    }
    const int t = (int)lroundf(angle / (2.0f * 3.14159265f / 16.0f)) % 16;
    if (directions == 16) {
        return order_16[t];
    }
    if (directions == 8) {
        static const int order_8[8] = {4, 0, 5, 1, 6, 2, 7, 3};
        return order_8[((t + 1) / 2) % 8];
    }
    if (directions == 4) {
        static const int order_4[4] = {0, 1, 2, 3}; // south-west, north-west, north-east, south-east
        return order_4[(((t + 16 - 2 + 2) % 16) / 4) % 4];
    }
    return 0;
}

static Missile *Roam__SpawnMissile(SceneRoam *roam, const char *name, const float x, const float y, float dx, float dy,
                                   const bool ours) {
    const MissileInfo *info = MissileData_Get(name);
    if (info == NULL) {
        return NULL;
    }
    Missile *missile = NULL;
    for (int m = 0; m < MAX_MISSILES && missile == NULL; m++) {
        missile = roam->missiles[m].active ? NULL : &roam->missiles[m];
    }
    if (missile == NULL) {
        return NULL; // full: the oldest keep flying
    }
    const float length = sqrtf((dx * dx) + (dy * dy));
    if (length < 0.0001f) {
        dx = 1.0f;
        dy = 1.0f;
    }
    memset(missile, 0, sizeof(Missile));
    missile->active = true;
    missile->info   = info;
    missile->sprite = MissileSprite_Get(info->cel_file, Roam__Palette(roam), info->glow);
    missile->x      = x;
    missile->y      = y;
    missile->dx     = dx / (length < 0.0001f ? 1.41421f : length);
    missile->dy     = dy / (length < 0.0001f ? 1.41421f : length);
    missile->speed  = info->speed;
    missile->life   = info->seconds > 0.05f ? info->seconds : 0.5f;
    missile->ours   = ours;
    if (info->travel_sound[0] != '\0') {
        Sounds_Play(info->travel_sound, SOUND_KIND_SFX);
    }
    return missile;
}

// Just to be seen: an explosion, a nova's ring, another player's bolt
static Missile *Roam__SpawnEffect(SceneRoam *roam, const char *name, const float x, const float y, const float dx,
                                  const float dy) {
    Missile *effect = Roam__SpawnMissile(roam, name, x, y, dx, dy, true);
    if (effect != NULL) {
        effect->harmless = true;
    }
    return effect;
}

static bool Roam__MissileHasHit(const Missile *missile, const int unit) {
    for (int h = 0; h < missile->hit_count; h++) {
        if (missile->hits[h] == unit) {
            return true;
        }
    }
    return false;
}

static void Roam__MissileNoteHit(Missile *missile, const int unit) {
    if (missile->hit_count < 8) {
        missile->hits[missile->hit_count++] = unit;
    } else {
        missile->hits[7] = unit;
    }
}

// The missile's damage on everything within its radius of (x, y) (an explosion, a nova, Meteor)
static void Roam__MissileBlast(SceneRoam *roam, Missile *missile, const float x, const float y, const uint64_t now) {
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally ||
            Roam__Distance(unit->x, unit->y, x, y) > missile->radius) {
            continue;
        }
        Roam__SkillHit(roam, unit, missile->damage, missile->cold_frames, missile->poison_frames, true, now);
    }
}

// The monster nearest a spot that hasn't been hit by this missile yet (Chain Lightning's next jump)
static int Roam__NextChainTarget(const SceneRoam *roam, const Missile *missile, const float x, const float y) {
    int   best     = -1;
    float distance = SKILL_CHAIN_RANGE;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally || Roam__MissileHasHit(missile, i)) {
            continue;
        }
        const float d = Roam__Distance(unit->x, unit->y, x, y);
        if (d < distance) {
            distance = d;
            best     = i;
        }
    }
    return best;
}

// A missile reaches a monster: it can miss (arrows), then its damage, then it explodes, goes on, or jumps on
static bool Roam__MissileHitsUnit(SceneRoam *roam, Missile *missile, const int index, const uint64_t now) {
    Unit *unit = &roam->units[index];
    Roam__MissileNoteHit(missile, index);
    if (missile->uses_ar && roam->game) {
        const int ar =
            roam->player.attack_rating + (int)((float)roam->player.attack_rating * missile->ar_bonus / 100.0f);
        if (Roam__Roll(1, 100) > Character_HitChance(ar, roam->player.level, unit->defense, unit->level)) {
            return false; // missed: flies on
        }
    }
    if (missile->radius > 0.0f) {
        Roam__MissileBlast(roam, missile, unit->x, unit->y, now);
    } else {
        Roam__SkillHit(roam, unit, missile->damage, missile->cold_frames, missile->poison_frames, true, now);
    }
    if (missile->info->hit_sound[0] != '\0') {
        Sounds_Play(missile->info->hit_sound, SOUND_KIND_SFX);
    }
    if (missile->info->explode_into[0] != '\0') {
        Roam__SpawnEffect(roam, missile->info->explode_into, unit->x, unit->y, missile->dx, missile->dy);
    }
    if (missile->chain > 0) {
        // Chain Lightning: on to the next monster near this one
        const int next = Roam__NextChainTarget(roam, missile, unit->x, unit->y);
        if (next >= 0) {
            missile->chain--;
            missile->x    = unit->x;
            missile->y    = unit->y;
            missile->dx   = roam->units[next].x - unit->x;
            missile->dy   = roam->units[next].y - unit->y;
            const float l = sqrtf((missile->dx * missile->dx) + (missile->dy * missile->dy));
            missile->dx   = l > 0.0f ? missile->dx / l : 1.0f;
            missile->dy   = l > 0.0f ? missile->dy / l : 0.0f;
            missile->age  = 0.0f;
            return false;
        }
    }
    if (missile->pierce > 0 ||
        (roam->game && Roam__Roll(1, 100) <= roam->character->bonus[BONUS_PIERCE] && missile->uses_ar)) {
        missile->pierce = missile->pierce > 0 ? missile->pierce - 1 : 0;
        return false;
    }
    return true; // spent
}

// A monster's missile reaching the player (dodged with Avoid / Evade)
static void Roam__MissileHitsPlayer(SceneRoam *roam, Missile *missile, const uint64_t now) {
    if (roam->game && Roam__Roll(1, 100) <= roam->character->bonus[BONUS_AVOID]) {
        return;
    }
    if (Roam__Blocks(roam, true)) {
        return; // a shield stops missiles too
    }
    int damage = missile->damage[ELEMENT_PHYSICAL];
    for (int e = ELEMENT_FIRE; e < ELEMENT_COUNT; e++) {
        const int resist  = roam->game ? Character_Resist(roam->character, (Element)e, Roam__ResistPenalty(roam)) : 0;
        damage           += Roam__Resisted(missile->damage[e], resist);
    }
    if (damage > 0) {
        Roam__Damage(roam, &roam->player, damage, now);
    }
}

// An area skill's spot goes off: everything near it is hit, and what it turns into appears (Meteor's fire)
static void Roam__MissileGoOff(SceneRoam *roam, Missile *missile, const uint64_t now) {
    if (!missile->harmless && missile->radius > 0.0f) {
        Roam__MissileBlast(roam, missile, missile->x, missile->y, now);
    }
    if (missile->info->hit_missile[0] != '\0') {
        Roam__SpawnEffect(roam, missile->info->hit_missile, missile->x, missile->y, missile->dx, missile->dy);
    } else if (missile->info->explode_into[0] != '\0') {
        Roam__SpawnEffect(roam, missile->info->explode_into, missile->x, missile->y, missile->dx, missile->dy);
    }
    if (missile->info->hit_sound[0] != '\0') {
        Sounds_Play(missile->info->hit_sound, SOUND_KIND_SFX);
    }
}

static void Roam__UpdateMissiles(SceneRoam *roam, const float seconds, const uint64_t now) {
    for (int m = 0; m < MAX_MISSILES; m++) {
        Missile *missile = &roam->missiles[m];
        if (!missile->active) {
            continue;
        }
        missile->age += seconds;
        // An area skill waits, then goes off where it is (repeating for a while: Blizzard's shards)
        if (missile->delay > 0.0f) {
            if (missile->age >= missile->delay) {
                if (missile->ticks > 0.0f) {
                    if (missile->age >= missile->tick_at) {
                        missile->tick_at = missile->age + missile->ticks;
                        Roam__MissileGoOff(roam, missile, now);
                    }
                } else {
                    Roam__MissileGoOff(roam, missile, now);
                    missile->active = false;
                    continue;
                }
            }
            if (missile->age >= missile->life) {
                missile->active = false;
            }
            continue;
        }
        if (missile->age >= missile->life) {
            missile->active = false;
            continue;
        }
        if (missile->info->explosion || missile->speed <= 0.0f) {
            continue; // stands where it is while it plays
        }
        missile->speed =
            fminf(missile->info->max_speed, fmaxf(0.0f, missile->speed + (missile->info->accel * seconds)));
        const float nx = missile->x + (missile->dx * missile->speed * seconds);
        const float ny = missile->y + (missile->dy * missile->speed * seconds);
        missile->x     = nx;
        missile->y     = ny;
        if (!missile->harmless) {
            const float reach = 0.8f + ((float)missile->info->size * 0.4f);
            if (missile->ours) {
                for (int i = 0; i < roam->unit_count && missile->active; i++) {
                    const Unit *unit = &roam->units[i];
                    if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally ||
                        Roam__MissileHasHit(missile, i) || Roam__Distance(unit->x, unit->y, nx, ny) > reach) {
                        continue;
                    }
                    if (Roam__MissileHitsUnit(roam, missile, i, now)) {
                        missile->active = false;
                    }
                }
            } else if (Roam__IsAlive(&roam->player) &&
                       Roam__Distance(roam->player.x, roam->player.y, nx, ny) <= reach) {
                Roam__MissileHitsPlayer(roam, missile, now);
                if (missile->info->explode_into[0] != '\0') {
                    Roam__SpawnEffect(roam, missile->info->explode_into, nx, ny, missile->dx, missile->dy);
                }
                missile->active = false;
            }
        } else if (missile->remote) {
            const float reach = 0.8f + ((float)missile->info->size * 0.4f);
            for (int i = 0; i < roam->unit_count && missile->active; i++) {
                const Unit *unit = &roam->units[i];
                if (Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally &&
                    Roam__Distance(unit->x, unit->y, nx, ny) <= reach) {
                    if (missile->info->explode_into[0] != '\0') {
                        Roam__SpawnEffect(roam, missile->info->explode_into, nx, ny, missile->dx, missile->dy);
                    }
                    missile->active = false;
                }
            }
        }
        // Walls stop it (it bursts there); it flies over chests and the like
        if (missile->active && !Map_IsWalkable(roam->map, (int)nx, (int)ny) &&
            !Map_IsObjectBlocked(roam->map, (int)nx, (int)ny)) {
            if ((!missile->harmless || missile->remote) && missile->info->explode_into[0] != '\0') {
                Roam__SpawnEffect(roam, missile->info->explode_into, nx, ny, missile->dx, missile->dy);
            }
            missile->active = false;
        }
    }
}

typedef struct MissileDraw {
    MissileSprite *sprite;
    int            direction;
    int            frame;
} MissileDraw;

static void Roam__DrawMissile(void *context, const int screen_x, const int screen_y, const float zoom) {
    const MissileDraw *draw = context;
    MissileSprite_Draw(draw->sprite, draw->direction, draw->frame, screen_x, screen_y, zoom);
}

// The missiles as things to draw among the units (sorted with them by where they are)
static int Roam__PrepareMissiles(SceneRoam *roam, MapDrawable *drawables, const int max) {
    static MissileDraw draws[MAX_MISSILES];
    int                count = 0;
    for (int m = 0; m < MAX_MISSILES && count < max; m++) {
        Missile *missile = &roam->missiles[m];
        if (!missile->active || missile->sprite == NULL || (missile->delay > 0.0f && missile->age < 0.0f)) {
            continue;
        }
        const int frames = MissileSprite_Frames(missile->sprite);
        int       frame  = (int)(missile->age * missile->info->frame_rate);
        frame = missile->info->loop ? frame % (frames > 0 ? frames : 1) : (frame < frames ? frame : frames - 1);
        draws[m].sprite = missile->sprite;
        draws[m].direction =
            Roam__MissileDirection(missile->dx, missile->dy, MissileSprite_Directions(missile->sprite));
        draws[m].frame            = frame;
        drawables[count].x        = missile->x;
        drawables[count].y        = missile->y;
        drawables[count].draw     = Roam__DrawMissile;
        drawables[count].context  = &draws[m];
        drawables[count].on_floor = missile->info->explosion;
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------
// Casting

// Does the skill use the weapon (bow and javelin skills, the Amazon's arrows): its damage is the weapon's plus the
// skill's
static bool Roam__SkillUsesWeapon(const SkillRow *s) {
    return strcmp(s->range, "rng") == 0 && (strcmp(s->anim, "A1") == 0 || strcmp(s->anim, "TH") == 0 ||
                                            strcmp(s->anim, "a1") == 0 || strcmp(s->anim, "th") == 0);
}

// A ranged weapon in hand: its basic attack fires (arrows, bolts, javelins, throwing axes and knives)
static const char *Roam__WeaponMissile(const SceneRoam *roam) {
    if (!roam->game) {
        return NULL;
    }
    const ItemBase *weapon = Inventory_Weapon(roam->character);
    if (weapon == NULL) {
        return NULL;
    }
    if (Items_IsA(weapon, "bow")) {
        return "arrow";
    }
    if (Items_IsA(weapon, "xbow")) {
        return "bolt";
    }
    if (Items_IsA(weapon, "jave")) {
        return "javelin";
    }
    if (Items_IsA(weapon, "taxe")) {
        return "throwaxe";
    }
    if (Items_IsA(weapon, "tkni")) {
        return "throwknife";
    }
    return NULL;
}

// The damage of one use of a skill: its own (by level, with synergies and masteries), plus the weapon's when it uses
// it
static void Roam__SkillDamage(const SceneRoam *roam, const SkillRow *s, const int level, int damage[ELEMENT_COUNT],
                              int *cold_frames, int *poison_frames) {
    memset(damage, 0, sizeof(int) * ELEMENT_COUNT);
    *cold_frames   = 0;
    *poison_frames = 0;
    if (s == NULL || Roam__SkillUsesWeapon(s)) {
        Roam__RollPlayerDamage(roam, damage); // the weapon's blow (arrows: the bow's)
    }
    if (s == NULL) {
        return;
    }
    int low  = 0;
    int high = 0;
    SkillData_PhysicalDamage(roam->character, s, level, &low, &high);
    damage[ELEMENT_PHYSICAL] += high > 0 ? Roam__Roll(low, high) : 0;
    int frames                = 0;
    SkillData_ElementalDamage(roam->character, s, level, &low, &high, &frames);
    if (s->element >= 0 && high > 0) {
        int rolled = Roam__Roll(low, high);
        if (s->element == ELEMENT_FIRE) {
            rolled += rolled * roam->character->bonus[BONUS_FIRE_SKILL_PCT] / 100; // Fire Mastery
        } else if (s->element == ELEMENT_LIGHTNING) {
            rolled += rolled * roam->character->bonus[BONUS_LIGHTNING_SKILL_PCT] / 100;
        }
        damage[s->element] += rolled;
        *cold_frames        = s->element == ELEMENT_COLD ? frames : 0;
        *poison_frames      = s->element == ELEMENT_POISON ? frames : 0;
    }
}

// Fills a missile's damage for a skill
static void Roam__ArmMissile(const SceneRoam *roam, Missile *missile, const SkillRow *s, const int level) {
    Roam__SkillDamage(roam, s, level, missile->damage, &missile->cold_frames, &missile->poison_frames);
    missile->skill    = s;
    missile->uses_ar  = s == NULL || Roam__SkillUsesWeapon(s);
    missile->ar_bonus = s != NULL ? (float)SkillData_ToHit(s, level) : 0.0f;
    // Fire Ball, Exploding Arrow, Meteor, ...: they burst over an area
    missile->radius = missile->info->hit_func == 1 || missile->info->hit_func == 14 ? 3.0f : 0.0f;
    if (s != NULL && strcmp(s->id, "Glacial Spike") == 0) {
        missile->radius = 3.0f;
    }
}

// Missiles fired at a spot: one, or a fan of them (Multiple Shot, Teeth), or scattered (Charged Bolt)
static void Roam__FireMissilesFrom(SceneRoam *roam, const char *name, const SkillRow *s, const int level,
                                   const int count, const float spread_degrees, const bool scatter, const float px,
                                   const float py, const float tx, const float ty, const bool harmless) {
    const float base  = atan2f(ty - py, tx - px);
    const float step  = count > 1 ? spread_degrees * 3.14159265f / 180.0f / (float)(count - 1) : 0.0f;
    const float first = base - (step * (float)(count - 1) / 2.0f);
    for (int n = 0; n < count; n++) {
        float angle = first + (step * (float)n);
        if (scatter) {
            angle = base + (((float)Roam__Roll(-60, 60)) * 3.14159265f / 180.0f);
        }
        Missile *missile = Roam__SpawnMissile(roam, name, px, py, cosf(angle), sinf(angle), true);
        if (missile != NULL && harmless) {
            missile->harmless = true; // someone else's: they do its damage
            missile->remote   = !missile->info->pierce;
        } else if (missile != NULL) {
            Roam__ArmMissile(roam, missile, s, level);
            if (s != NULL && s->do_func == 26) {
                missile->chain = SkillData_Eval(roam->character, s, level, s->calc[0]); // Chain Lightning's jumps
            }
        }
    }
}

static void Roam__FireMissiles(SceneRoam *roam, const char *name, const SkillRow *s, const int level, const int count,
                               const float spread_degrees, const bool scatter, const float tx, const float ty) {
    Roam__FireMissilesFrom(roam, name, s, level, count, spread_degrees, scatter, roam->player.x, roam->player.y, tx, ty,
                           false);
}

// The radius a skill works in (sub-tiles), from its range calc
static float Roam__SkillRadius(const SceneRoam *roam, const SkillRow *s, const int level, const float fallback) {
    const int range = s->aura_range[0] != '\0' ? SkillData_Eval(roam->character, s, level, s->aura_range) : 0;
    return range > 0 ? fminf((float)range * 1.5f, 30.0f) : fallback;
}

// A ring of missiles round the player, everything near them hit once (Frost Nova, Nova, Poison Nova, War Cry)
static void Roam__Nova(SceneRoam *roam, const SkillRow *s, const int level, const uint64_t now) {
    const char        *name   = s->missile[0] != '\0' ? s->missile : s->missile_a;
    const MissileInfo *info   = MissileData_Get(name);
    const float        radius = info != NULL ? fminf(fmaxf(info->speed * info->seconds * 0.6f, 4.0f), 14.0f) : 8.0f;
    for (int n = 0; n < 24 && info != NULL; n++) {
        const float angle = (float)n * 2.0f * 3.14159265f / 24.0f;
        Roam__SpawnEffect(roam, name, roam->player.x, roam->player.y, cosf(angle), sinf(angle));
    }
    int damage[ELEMENT_COUNT];
    int cold   = 0;
    int poison = 0;
    Roam__SkillDamage(roam, s, level, damage, &cold, &poison);
    bool any = false;
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        any = any || damage[e] > 0;
    }
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally ||
            Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > radius) {
            continue;
        }
        if (any) {
            Roam__SkillHit(roam, unit, damage, cold, poison, true, now);
        }
        if (s->aura_length[0] != '\0' && s->aura_stat[0][0] != '\0') {
            // Battle Cry: lowers their defense and damage for a while
            unit->curse       = s;
            unit->curse_level = level;
            unit->curse_until = now + (uint64_t)SkillData_Eval(roam->character, s, level, s->aura_length) * 40;
        }
    }
}

// The skill lands on the spot clicked, after a moment (Meteor falls, Blizzard's shards come down for a while)
static void Roam__AreaSkill(SceneRoam *roam, const SkillRow *s, const int level, const float tx, const float ty,
                            const uint64_t now) {
    if (s->do_func == 55) {
        // Corpse Explosion: the nearest corpse to the spot bursts, hurting what's round it by part of its life
        int   corpse   = -1;
        float distance = 4.0f;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            if (unit->state == UNIT_DEAD && !unit->vanished && !unit->is_object &&
                Roam__Distance(unit->x, unit->y, tx, ty) < distance) {
                distance = Roam__Distance(unit->x, unit->y, tx, ty);
                corpse   = i;
            }
        }
        if (corpse < 0) {
            Roam__ShowMessage(roam, "Corpse Explosion needs a corpse.");
            return;
        }
        Unit     *body                 = &roam->units[corpse];
        Missile   blast                = {0};
        const int percent              = Roam__Roll(s->params[0], s->params[1] > s->params[0] ? s->params[1] : 100);
        blast.damage[ELEMENT_PHYSICAL] = body->max_hp * percent / 200;
        blast.damage[ELEMENT_FIRE]     = body->max_hp * percent / 200;
        blast.radius                   = Roam__SkillRadius(roam, s, level, 4.0f);
        Roam__MissileBlast(roam, &blast, body->x, body->y, now);
        body->vanished = true;
        Roam__SpawnEffect(roam, "corpseexplosion", body->x, body->y, 1.0f, 1.0f);
        return;
    }
    const char *name = s->missile[0] != '\0' ? s->missile : s->missile_a;
    Missile    *spot = Roam__SpawnMissile(roam, name, tx, ty, 1.0f, 1.0f, true);
    if (spot == NULL) {
        // No such missile: it just lands
        Missile blast  = {0};
        int     cold   = 0;
        int     poison = 0;
        Roam__SkillDamage(roam, s, level, blast.damage, &cold, &poison);
        blast.cold_frames   = cold;
        blast.poison_frames = poison;
        blast.radius        = 4.0f;
        Roam__MissileBlast(roam, &blast, tx, ty, now);
        return;
    }
    Roam__ArmMissile(roam, spot, s, level);
    spot->speed  = 0.0f;
    spot->radius = Roam__SkillRadius(roam, s, level, 4.0f);
    if (strcmp(s->id, "Blizzard") == 0 || strcmp(s->id, "Eruption") == 0) {
        // Comes down for a few seconds, a little at a time
        spot->delay  = 0.3f;
        spot->ticks  = 0.5f;
        spot->life   = 4.0f;
        spot->radius = 5.0f;
        for (int e = 0; e < ELEMENT_COUNT; e++) {
            spot->damage[e] /= 4;
        }
    } else if (strcmp(s->id, "Fist of the Heavens") == 0) {
        spot->delay  = 0.2f;
        spot->radius = 2.5f;
    } else {
        spot->delay = fminf(spot->life, 1.0f); // Meteor takes a moment to fall
        spot->life  = spot->delay + 0.1f;
    }
}

static void Roam__AddBuff(SceneRoam *roam, const SkillRow *s, const int level, const uint64_t now) {
    // The Druid's shapes: one at a time, and casting the one you're in changes you back
    for (int b = 0; b < roam->buff_count && s->do_func == 116; b++) {
        if (roam->buffs[b].skill->do_func == 116) {
            const bool same = roam->buffs[b].skill == s;
            roam->buffs[b]  = roam->buffs[--roam->buff_count];
            Roam__ApplyCharacter(roam);
            if (same) {
                return; // back to yourself
            }
            break;
        }
    }
    const int frames = s->aura_length[0] != '\0' ? SkillData_Eval(roam->character, s, level, s->aura_length) : 0;
    RoamBuff *buff   = NULL;
    for (int b = 0; b < roam->buff_count && buff == NULL; b++) {
        buff = roam->buffs[b].skill == s ? &roam->buffs[b] : NULL;
    }
    // The three Sorceress armors replace each other, like the game
    for (int b = 0; b < roam->buff_count && buff == NULL; b++) {
        if (s->do_func == 18 && roam->buffs[b].skill->do_func == 18 &&
            strstr(roam->buffs[b].skill->id, "Armor") != NULL && strstr(s->id, "Armor") != NULL) {
            buff = &roam->buffs[b];
        }
    }
    if (buff == NULL && roam->buff_count < MAX_BUFFS) {
        buff = &roam->buffs[roam->buff_count++];
    }
    if (buff == NULL) {
        return;
    }
    buff->skill = s;
    buff->level = level;
    buff->until = frames > 0 ? now + ((uint64_t)frames * 40) : 0;
    if (strcmp(s->aura_stat[0], "bonearmor") == 0) {
        roam->bone_armor = (float)SkillData_Eval(roam->character, s, level, s->aura_calc[0]) / 256.0f;
        buff->until      = 0; // until it's soaked up its fill
    }
}

// A curse on the monsters round the spot (one curse each: the newest wins)
static void Roam__Curse(SceneRoam *roam, const SkillRow *s, const int level, const float tx, const float ty,
                        const uint64_t now) {
    const float    radius = Roam__SkillRadius(roam, s, level, SKILL_CURSE_RADIUS);
    const int      frames = s->aura_length[0] != '\0' ? SkillData_Eval(roam->character, s, level, s->aura_length) : 250;
    const uint64_t until  = now + ((uint64_t)(frames > 0 ? frames : 250) * 40);
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally ||
            Roam__Distance(unit->x, unit->y, tx, ty) > radius) {
            continue;
        }
        unit->curse       = s;
        unit->curse_level = level;
        unit->curse_until = until;
        unit->provoked    = true;
    }
}

static int Roam__CountAllies(const SceneRoam *roam, const SkillRow *s, int *oldest) {
    int count = 0;
    *oldest   = -1;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->ally && unit->summoned_by == s && Roam__IsAlive(unit)) {
            count++;
            *oldest = *oldest < 0 ? i : *oldest;
        }
    }
    return count;
}

// Monsters that fight for you (skeletons, golems, the Valkyrie, wolves, the Druid's spirits, hydras, sentries)
static void Roam__Summon(SceneRoam *roam, const SkillRow *s, const int level, const float tx, const float ty,
                         const uint64_t now) {
    const int most   = s->pet_max[0] != '\0' ? SkillData_Eval(roam->character, s, level, s->pet_max) : 1;
    int       oldest = -1;
    if (Roam__CountAllies(roam, s, &oldest) >= (most > 0 ? most : 1) && oldest >= 0) {
        // Full: the first one goes
        Unit *gone     = &roam->units[oldest];
        gone->hp       = 0;
        gone->vanished = true;
        Roam__SetState(gone, UNIT_DEAD, now);
    }
    float x = tx;
    float y = ty;
    if (!Map_IsWalkable(roam->map, (int)x, (int)y) || Roam__Distance(x, y, roam->player.x, roam->player.y) > 12.0f) {
        x = roam->player.x + 2.0f;
        y = roam->player.y + 2.0f;
    }
    Roam__ArrivalSpot(roam->map, x, y, &x, &y);
    char id[40];
    snprintf(id, sizeof(id), "%s", s->summon);
    for (char *c = id; *c != '\0'; c++) {
        *c = (char)tolower((unsigned char)*c); // monstats ids are lower case ("ClayGolem" is claygolem)
    }
    Unit *unit = Roam__AddUnit(roam, "monsters", id, x, y);
    if (unit == NULL) {
        char text[96];
        snprintf(text, sizeof(text), "%s: can't summon here.", s->id);
        Roam__ShowMessage(roam, text);
        return;
    }
    unit->ally        = true;
    unit->summoned_by = s;
    unit->npc         = false;
    unit->experience  = 0;
    unit->treasure[0] = '\0';
    // Stronger with the skill's level (and its masteries), roughly like the game's summon tables
    const int grow = 100 + ((level - 1) * 15);
    const int clvl = roam->character->level;
    // (some, like the Valkyrie, have token numbers in monstats: the game sets them from the skill)
    unit->max_hp         = unit->max_hp > 20 + (clvl * 8) ? unit->max_hp : 20 + (clvl * 8);
    unit->damage_min     = unit->damage_min > 2 + clvl ? unit->damage_min : 2 + clvl;
    unit->damage_max     = unit->damage_max > 4 + (clvl * 2) ? unit->damage_max : 4 + (clvl * 2);
    unit->defense        = unit->defense > clvl * 6 ? unit->defense : clvl * 6;
    unit->max_hp         = unit->max_hp * grow / 100;
    unit->hp             = unit->max_hp;
    unit->damage_min     = unit->damage_min * grow / 100;
    unit->damage_max     = unit->damage_max * grow / 100;
    unit->level          = roam->character->level;
    unit->attack_rating += SkillData_Eval(roam->character, s, level, s->aura_calc[1]) > 0
                               ? SkillData_Eval(roam->character, s, level, s->aura_calc[1])
                               : roam->character->level * 5;
    // Hydras, sentries and the like don't stay
    if (strcmp(s->range, "rng") == 0 || strstr(s->summon, "hydra") != NULL || strstr(s->summon, "sentry") != NULL) {
        unit->ally_until = now + 12000;
    } else if (strcmp(s->summon, "druidhawk") == 0) {
        unit->ally_until = now + 30000;
    }
}

// Which missile a skill fires, how many and how spread (Multiple Shot's fan, Charged Bolt's scatter)
static const char *Roam__MissileVolley(const SceneRoam *roam, const SkillRow *s, const int level, int *count,
                                       float *spread, bool *scatter) {
    const char *name = s->missile[0] != '\0' ? s->missile : s->missile_a;
    if (Roam__SkillUsesWeapon(s) && strcmp(name, "arrow") == 0 && Roam__WeaponMissile(roam) != NULL) {
        name = Roam__WeaponMissile(roam);
    }
    *count   = 1;
    *spread  = 0.0f;
    *scatter = false;
    if (s->do_func == 8) { // Multiple Shot, Teeth
        *count  = SkillData_Eval(roam->character, s, level, s->calc[0]);
        *spread = fminf(8.0f * (float)(*count - 1), 70.0f);
    } else if (s->do_func == 17) { // Charged Bolt
        *count   = SkillData_Eval(roam->character, s, level, s->calc[0]);
        *scatter = true;
    } else if (s->do_func == 12) { // Strafe: a spray at what's around
        *count  = SkillData_Eval(roam->character, s, level, s->calc[0]);
        *spread = 90.0f;
    } else if (s->do_func == 19 || s->do_func == 117) { // Inferno, Arctic Blast, Firestorm: a short burst
        *count  = 3;
        *spread = 20.0f;
    }
    *count = *count < 1 ? 1 : (*count > 24 ? 24 : *count);
    return name;
}

// Multiplayer: the others see the skill (they're told where from and where at; the damage is ours to do)
static void Roam__SendCast(SceneRoam *roam, const SkillRow *s, const float tx, const float ty) {
    if (!roam->online) {
        return;
    }
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_I32(writer, Roam__MapKey(roam));
    NetWriter_U16(writer, s != NULL ? (uint16_t)s->row : 0xFFFF);
    NetWriter_U8(writer, (uint8_t)(s != NULL ? roam->cast_level : 0));
    NetWriter_F32(writer, roam->player.x);
    NetWriter_F32(writer, roam->player.y);
    NetWriter_F32(writer, tx);
    NetWriter_F32(writer, ty);
    NetClient_Send(MSG_CAST, writer);
    free(writer);
}

// Multiplayer: a missile fired in our game by a monster we run (damage: it hurts whoever it reaches in their game too)
// or by our hireling (damage NULL: only to see, the hit is done already). MSG_CAST with the skill 0xFFFE.
static void Roam__SendShot(SceneRoam *roam, const char *name, const float x, const float y, const float tx,
                           const float ty, const int *damage) {
    if (!roam->online || name == NULL) {
        return;
    }
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_I32(writer, Roam__MapKey(roam));
    NetWriter_U16(writer, 0xFFFE);
    NetWriter_U8(writer, 0);
    NetWriter_F32(writer, x);
    NetWriter_F32(writer, y);
    NetWriter_F32(writer, tx);
    NetWriter_F32(writer, ty);
    NetWriter_String(writer, name);
    NetWriter_U8(writer, damage != NULL ? 1 : 0);
    for (int e = 0; e < ELEMENT_COUNT && damage != NULL; e++) {
        NetWriter_I32(writer, damage[e]);
    }
    NetClient_Send(MSG_CAST, writer);
    free(writer);
}

// ... and seen: a monster's missile hurts us here when it reaches us; a hireling's is only to see
static void Roam__ShowShot(SceneRoam *roam, NetReader *reader, const float x, const float y, const float tx,
                           const float ty) {
    char name[40];
    NetReader_String(reader, name, sizeof(name));
    const bool hurts                 = NetReader_U8(reader) != 0;
    int        damage[ELEMENT_COUNT] = {0};
    for (int e = 0; e < ELEMENT_COUNT && hurts; e++) {
        damage[e] = NetReader_I32(reader);
    }
    if (reader->failed || MissileData_Get(name) == NULL) {
        return;
    }
    if (!hurts) {
        Roam__FireMissilesFrom(roam, name, NULL, 0, 1, 0.0f, false, x, y, tx, ty, true);
        return;
    }
    Missile *missile = Roam__SpawnMissile(roam, name, x, y, tx - x, ty - y, false);
    if (missile != NULL) {
        memcpy(missile->damage, damage, sizeof(missile->damage));
    }
}

// Another player's skill, to be seen: its missiles, ring or landing spot, doing nothing
static void Roam__ShowCast(SceneRoam *roam, const int row, const int level, const float x, const float y,
                           const float tx, const float ty) {
    const SkillRow *s = row == 0xFFFF ? NULL : SkillData_ByRow(row);
    if (s == NULL) {
        Roam__FireMissilesFrom(roam, "arrow", NULL, 0, 1, 0.0f, false, x, y, tx, ty, true);
        return;
    }
    if (s->kind == SKILL_KIND_MISSILE) {
        int         count   = 1;
        float       spread  = 0.0f;
        bool        scatter = false;
        const char *name    = s->missile[0] != '\0' ? s->missile : s->missile_a;
        if (s->do_func == 8 || s->do_func == 12) {
            count  = level > 0 ? 2 + level / 2 : 3;
            spread = fminf(8.0f * (float)(count - 1), 70.0f);
        } else if (s->do_func == 17) {
            count   = 3 + level / 3;
            scatter = true;
        }
        Roam__FireMissilesFrom(roam, name, s, level, count, spread, scatter, x, y, tx, ty, true);
    } else if (s->kind == SKILL_KIND_NOVA) {
        const char *name = s->missile[0] != '\0' ? s->missile : s->missile_a;
        for (int n = 0; n < 24; n++) {
            const float angle = (float)n * 2.0f * 3.14159265f / 24.0f;
            Roam__SpawnEffect(roam, name, x, y, cosf(angle), sinf(angle));
        }
    } else if (s->kind == SKILL_KIND_AREA) {
        Missile *spot = Roam__SpawnEffect(roam, s->missile[0] != '\0' ? s->missile : s->missile_a, tx, ty, 1.0f, 1.0f);
        if (spot != NULL) {
            spot->speed = 0.0f;
            spot->delay = fminf(spot->life, 1.0f);
            spot->life  = spot->delay + 0.1f;
        }
    }
}

// The skills that don't fit the kinds above, by what they do (skills.txt srvdofunc). True when it's one of them.
static bool Roam__SpecialSkill(SceneRoam *roam, const SkillRow *s, const int level, const float tx, const float ty,
                               const uint64_t now) {
    Unit *player = &roam->player;
    Unit *target =
        roam->cast_target >= 0 && roam->cast_target < roam->unit_count ? &roam->units[roam->cast_target] : NULL;
    if (target != NULL && (!Roam__IsAlive(target) || target->is_object || target->npc || target->ally)) {
        target = NULL;
    }
    int damage[ELEMENT_COUNT];
    int cold   = 0;
    int poison = 0;
    switch (s->do_func) {
    case 20: { // Static Field: a quarter of what's left of each monster's life around you
        const float radius = 5.0f + ((float)level * 0.7f);
        Roam__SpawnEffect(roam, "staticfield", player->x, player->y, 1.0f, 0.0f);
        for (int i = 0; i < roam->unit_count; i++) {
            Unit *unit = &roam->units[i];
            if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally || unit->hp <= 1 ||
                Roam__Distance(unit->x, unit->y, player->x, player->y) > radius) {
                continue;
            }
            memset(damage, 0, sizeof(damage));
            damage[ELEMENT_LIGHTNING] = unit->hp / 4 > 0 ? unit->hp / 4 : 1;
            Roam__SkillHit(roam, unit, damage, 0, 0, true, now);
        }
        return true;
    }
    case 21: // Telekinesis
    case 33: // Psychic Hammer
        if (target != NULL) {
            Roam__SkillDamage(roam, s, level, damage, &cold, &poison);
            if (s->do_func == 33 && damage[ELEMENT_PHYSICAL] <= 0) {
                damage[ELEMENT_PHYSICAL] = Roam__Roll(1 + level, 4 + (level * 2));
            }
            Roam__SkillHit(roam, target, damage, cold, poison, true, now);
        }
        return true;
    case 51: { // Mind Blast: everything round the spot is struck
        for (int i = 0; i < roam->unit_count; i++) {
            Unit *unit = &roam->units[i];
            if (Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally &&
                Roam__Distance(unit->x, unit->y, tx, ty) <= 4.0f) {
                Roam__SkillDamage(roam, s, level, damage, &cold, &poison);
                damage[ELEMENT_PHYSICAL] += Roam__Roll(1 + level, 3 + (level * 2));
                Roam__SkillHit(roam, unit, damage, cold, poison, true, now);
            }
        }
        return true;
    }
    case 52:   // Dragon Flight: there in a blink, and a kick
    case 67:   // Charge: a rush and a blow
    case 78: { // Leap Attack: a leap and a blow
        if (target == NULL) {
            return true;
        }
        float x;
        float y;
        Roam__ArrivalSpot(roam->map, target->x - ((target->x - player->x) * 0.15f),
                          target->y - ((target->y - player->y) * 0.15f), &x, &y);
        if (Map_IsWalkable(roam->map, (int)x, (int)y) && Roam__Distance(x, y, player->x, player->y) <= 40.0f) {
            player->x = player->target_x = x;
            player->y = player->target_y = y;
            roam->moving                 = false;
            Roam__ClearPath(player);
        }
        roam->melee_skill = s;
        roam->melee_level = level;
        Roam__PlayerStrikes(roam, target, now);
        roam->melee_skill = NULL;
        return true;
    }
    case 76: { // Whirlwind: spinning through to the spot, each monster on the way struck
        const float dx   = tx - player->x;
        const float dy   = ty - player->y;
        const float len  = sqrtf((dx * dx) + (dy * dy));
        const float step = 1.0f;
        float       x    = player->x;
        float       y    = player->y;
        for (float d = 0.0f; d < fminf(len, 20.0f); d += step) {
            const float nx = player->x + (dx / (len > 0.0f ? len : 1.0f) * d);
            const float ny = player->y + (dy / (len > 0.0f ? len : 1.0f) * d);
            if (!Map_IsWalkable(roam->map, (int)nx, (int)ny)) {
                break;
            }
            x = nx;
            y = ny;
        }
        bool struck[MAX_UNITS];
        memset(struck, 0, sizeof(struck));
        roam->melee_skill = s;
        roam->melee_level = level;
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            const float px = player->x + ((x - player->x) * t);
            const float py = player->y + ((y - player->y) * t);
            for (int i = 0; i < roam->unit_count; i++) {
                Unit *unit = &roam->units[i];
                if (!struck[i] && Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally &&
                    Roam__IsCombatant(unit) && Roam__Distance(unit->x, unit->y, px, py) <= 3.0f) {
                    struck[i] = true;
                    Roam__PlayerStrikes(roam, unit, now);
                }
            }
        }
        roam->melee_skill = NULL;
        player->x = player->target_x = x;
        player->y = player->target_y = y;
        roam->moving                 = false;
        Roam__ClearPath(player);
        return true;
    }
    case 58: { // Revive: the corpse nearest the spot fights for you a while
        int   best = -1;
        float near = 6.0f;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            const float d    = Roam__Distance(unit->x, unit->y, tx, ty);
            if (!unit->is_object && !unit->npc && !unit->ally && unit->state == UNIT_DEAD && !unit->vanished &&
                !unit->boss && unit->rank != RANK_UNIQUE && unit->rank != RANK_SUPER_UNIQUE && unit->max_hp > 0 &&
                d < near) {
                near = d;
                best = i;
            }
        }
        if (best < 0) {
            Roam__ShowMessage(roam, "Revive needs a corpse.");
            return true;
        }
        Unit *revived        = &roam->units[best];
        revived->ally        = true;
        revived->summoned_by = s;
        revived->hp          = revived->max_hp * 2;
        revived->max_hp      = revived->hp;
        revived->ally_until  = now + 180000; // three minutes
        revived->experience  = 0;
        revived->treasure[0] = '\0';
        Roam__SetState(revived, UNIT_IDLE, now);
        return true;
    }
    default:
        return false;
    }
}

// The skill's effect, half way through the animation
static void Roam__ApplyCast(SceneRoam *roam, const uint64_t now) {
    const SkillRow *s     = roam->cast_skill;
    const int       level = roam->cast_level;
    const float     tx    = roam->cast_x;
    const float     ty    = roam->cast_y;
    roam->cast_fired      = true;
    if (s == NULL || s->kind == SKILL_KIND_MISSILE || s->kind == SKILL_KIND_NOVA || s->kind == SKILL_KIND_AREA) {
        Roam__SendCast(roam, s, tx, ty);
    }
    if (s == NULL) {
        // The basic attack with a bow or javelin: one missile
        const char *name = Roam__WeaponMissile(roam);
        if (name != NULL) {
            Roam__FireMissiles(roam, name, NULL, 0, 1, 0.0f, false, tx, ty);
        }
        return;
    }
    if (s->do_sound[0] != '\0') {
        Sounds_Play(s->do_sound, SOUND_KIND_SFX);
    }
    switch (s->kind) {
    case SKILL_KIND_MISSILE: {
        int         count   = 1;
        float       spread  = 0.0f;
        bool        scatter = false;
        const char *name    = Roam__MissileVolley(roam, s, level, &count, &spread, &scatter);
        Roam__FireMissiles(roam, name, s, level, count, spread, scatter, tx, ty);
        break;
    }
    case SKILL_KIND_NOVA:
        Roam__Nova(roam, s, level, now);
        break;
    case SKILL_KIND_AREA:
        Roam__AreaSkill(roam, s, level, tx, ty, now);
        break;
    case SKILL_KIND_BUFF:
        Roam__AddBuff(roam, s, level, now);
        break;
    case SKILL_KIND_CURSE:
        Roam__Curse(roam, s, level, tx, ty, now);
        break;
    case SKILL_KIND_SUMMON:
        Roam__Summon(roam, s, level, tx, ty, now);
        break;
    case SKILL_KIND_TELEPORT:
        if (Map_IsWalkable(roam->map, (int)tx, (int)ty) &&
            Roam__Distance(tx, ty, roam->player.x, roam->player.y) <= 45.0f) {
            Roam__SpawnEffect(roam, "teleport", roam->player.x, roam->player.y, 1.0f, 1.0f);
            roam->player.x = roam->player.target_x = tx;
            roam->player.y = roam->player.target_y = ty;
            roam->moving                           = false;
            Roam__ClearPath(&roam->player);
        }
        break;
    default:
        Roam__SpecialSkill(roam, s, level, tx, ty, now);
        break;
    }
}

// The animation a skill plays (skills.txt anim), if the character has it
static void Roam__CastMode(SceneRoam *roam, const SkillRow *s, char *mode) {
    const char *wanted = s != NULL ? s->anim : "a1";
    char        lower[4];
    snprintf(lower, sizeof(lower), "%c%c", (char)tolower((unsigned char)wanted[0]),
             (char)tolower((unsigned char)wanted[1]));
    if (strcmp(lower, "sq") == 0 || lower[0] == '\0') {
        snprintf(lower, sizeof(lower), "%s", s != NULL && strcmp(s->range, "h2h") == 0 ? "a1" : "sc");
    }
    if (MapUnits_GetFigure(roam->units_cache, roam->player.base, roam->player.id, lower) == NULL) {
        snprintf(lower, sizeof(lower), "%s",
                 MapUnits_GetFigure(roam->units_cache, roam->player.base, roam->player.id, "sc") != NULL ? "sc" : "a1");
    }
    snprintf(mode, 4, "%s", lower);
}

// Using a skill on the spot (or monster) under the mouse. Returns true when something happened (or was refused with
// a message), false when the click should do its usual thing (walk, attack).
static bool Roam__UseSkill(SceneRoam *roam, const int skill, const float x, const float y, const int hovered,
                           const uint64_t now) {
    Unit *player = &roam->player;
    if (!roam->game || !Roam__IsAlive(player) || player->state == UNIT_CAST || player->state == UNIT_ATTACK ||
        player->state == UNIT_HIT) {
        return true;
    }
    const SkillRow *s = skill == SKILL_ATTACK ? NULL : SkillData_ForCharacter(roam->character, skill);
    if (skill == SKILL_ATTACK) {
        if (Roam__WeaponMissile(roam) == NULL) {
            return false; // a plain blow: walk up and hit
        }
    } else if (s == NULL || Roam__SkillPoints(roam, skill) <= 0) {
        return false;
    } else if (s->kind == SKILL_KIND_PASSIVE || s->kind == SKILL_KIND_AURA) {
        return false; // always on / on while it's chosen: the click walks
    } else if (s->kind == SKILL_KIND_OTHER && s->do_func != 20 && s->do_func != 21 && s->do_func != 33 &&
               s->do_func != 51 && s->do_func != 52 && s->do_func != 67 && s->do_func != 76 && s->do_func != 78 &&
               s->do_func != 58) {
        char text[96];
        snprintf(text, sizeof(text), "%s isn't in the engine yet.", s->id);
        Roam__ShowMessage(roam, text);
        return true; // (and no mana spent)
    }
    const int level = skill == SKILL_ATTACK ? 0 : Roam__SkillPoints(roam, skill);
    if (roam->in_town && s != NULL && s->kind != SKILL_KIND_TELEPORT) {
        Roam__ShowMessage(roam, "You can't use that in town.");
        return true;
    }
    const float cost = s != NULL ? SkillData_ManaCost(s, level) : 0.0f;
    if (roam->mana < cost) {
        Roam__ShowMessage(roam, "Not enough mana.");
        Sounds_Play("cursor_cantdo", SOUND_KIND_UI);
        return true;
    }
    if (s != NULL && s->kind == SKILL_KIND_MELEE) {
        // A stronger blow at the monster: walk up and swing with it
        if (hovered < 0 || !Roam__IsCombatant(&roam->units[hovered]) || roam->units[hovered].ally) {
            return false;
        }
        roam->mana            -= cost;
        roam->melee_skill      = s;
        roam->melee_level      = level;
        roam->attack_target    = hovered;
        roam->attack_in_place  = false;
        roam->use_target       = -1;
        roam->item_target      = -1;
        return true;
    }
    roam->mana          -= cost;
    roam->cast_skill     = s;
    roam->cast_level     = level;
    roam->cast_target    = hovered;
    roam->cast_x         = hovered >= 0 ? roam->units[hovered].x : x;
    roam->cast_y         = hovered >= 0 ? roam->units[hovered].y : y;
    roam->cast_fired     = false;
    roam->moving         = false;
    roam->attack_target  = -1;
    Roam__ClearPath(player);
    Roam__TurnTowards(&player->turn, player->x, player->y, roam->cast_x, roam->cast_y);
    Roam__CastMode(roam, s, player->cast_mode);
    Roam__SetState(player, UNIT_CAST, now);
    if (s != NULL && s->cast_sound[0] != '\0') {
        Sounds_Play(s->cast_sound, SOUND_KIND_SFX);
    } else if (s == NULL) {
        Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
    }
    return true;
}

// The player's cast animation under way: the effect half way, then back to standing
static void Roam__UpdateCast(SceneRoam *roam, const uint64_t now) {
    Unit *player = &roam->player;
    if (player->state != UNIT_CAST) {
        return;
    }
    Figure   *figure = Roam__Figure(roam, player);
    const int frames = figure != NULL ? Figure_GetFramesPerDirection(figure) : 1;
    if (!roam->cast_fired && player->frame >= frames / 2) {
        Roam__ApplyCast(roam, now);
    }
}

// Auras' pulses and the storms' strikes: Prayer heals, Holy Fire / Freeze / Shock hurt what's near, Sanctuary hurts
// the undead, Thunder Storm strikes a monster nearby
static void Roam__SkillPulse(SceneRoam *roam, const uint64_t now) {
    if (!roam->game || now < roam->aura_tick || !Roam__IsAlive(&roam->player)) {
        return;
    }
    roam->aura_tick       = now + SKILL_DOT_PULSE_MS;
    int             level = 0;
    const SkillRow *aura  = Roam__ActiveAura(roam, &level);
    if (aura != NULL) {
        for (int a = 0; a < SKILL_AURA_STATS; a++) {
            if (strcmp(aura->aura_stat[a], "hitpoints") == 0) {
                const int heal = SkillData_Eval(roam->character, aura, level, aura->aura_calc[a]);
                roam->player.hp =
                    roam->player.hp + heal < roam->player.max_hp ? roam->player.hp + heal : roam->player.max_hp;
            }
        }
        if (aura->do_func == 66 || aura->do_func == 81) {
            int damage[ELEMENT_COUNT];
            int cold   = 0;
            int poison = 0;
            int low    = 0;
            int high   = 0;
            int frames = 0;
            memset(damage, 0, sizeof(damage));
            SkillData_ElementalDamage(roam->character, aura, level, &low, &high, &frames);
            if (aura->element >= 0 && high > 0) {
                damage[aura->element] = Roam__Roll(low, high);
                cold                  = aura->element == ELEMENT_COLD ? 25 : 0;
            }
            const float radius = Roam__SkillRadius(roam, aura, level, 8.0f);
            for (int i = 0; i < roam->unit_count; i++) {
                Unit *unit = &roam->units[i];
                if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally ||
                    Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) > radius) {
                    continue;
                }
                if (aura->do_func == 81) {
                    unit->chill_until = now + SKILL_DOT_PULSE_MS + 100; // Holy Freeze slows everything in it
                }
                if (high > 0) {
                    Roam__SkillHit(roam, unit, damage, cold, poison, true, now);
                }
            }
        }
    }
    // Storms on you strike the nearest monster
    for (int b = 0; b < roam->buff_count; b++) {
        const SkillRow *s = roam->buffs[b].skill;
        if (s->element < 0 || (s->do_func != 29 && s->do_func != 124)) {
            continue;
        }
        int   target   = -1;
        float distance = 15.0f;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            if (Roam__IsAlive(unit) && !unit->is_object && !unit->npc && !unit->ally &&
                Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) < distance) {
                distance = Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y);
                target   = i;
            }
        }
        if (target >= 0) {
            int damage[ELEMENT_COUNT];
            int cold   = 0;
            int poison = 0;
            Roam__SkillDamage(roam, s, roam->buffs[b].level, damage, &cold, &poison);
            Roam__SkillHit(roam, &roam->units[target], damage, cold, poison, true, now);
            Roam__SpawnEffect(roam, "lightningstrike", roam->units[target].x, roam->units[target].y, 0.0f, 1.0f);
        }
    }
}

// Damage the player takes, after Bone Armor / Cyclone Armor soaks some up
static int Roam__AbsorbDamage(SceneRoam *roam, const int damage) {
    if (roam->bone_armor <= 0.0f || damage <= 0) {
        return damage;
    }
    const float soaked  = fminf(roam->bone_armor, (float)damage);
    roam->bone_armor   -= soaked;
    if (roam->bone_armor <= 0.0f) {
        for (int b = 0; b < roam->buff_count; b++) {
            if (strcmp(roam->buffs[b].skill->aura_stat[0], "bonearmor") == 0) {
                roam->buffs[b].until = 1; // gone (cleared next frame)
            }
        }
    }
    return damage - (int)soaked;
}

// ---------------------------------------------------------------------------------------------------------------------
// Summons

// The monster nearest an ally that it should go for (near it, or near the player), -1 if none
static int Roam__AllyTarget(const SceneRoam *roam, const Unit *ally) {
    int   best     = -1;
    float distance = 14.0f;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || unit->is_object || unit->npc || unit->ally || !Roam__IsCombatant(unit)) {
            continue;
        }
        const float d = Roam__Distance(unit->x, unit->y, ally->x, ally->y);
        if (d < distance && Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) < 20.0f) {
            distance = d;
            best     = i;
        }
    }
    return best;
}

// A summoned monster's turn: fight what's near, otherwise keep close to the player
static void Roam__UpdateAlly(SceneRoam *roam, Unit *ally, const float seconds, const uint64_t now) {
    if (ally->ally_until != 0 && now >= ally->ally_until && Roam__IsAlive(ally)) {
        ally->hp       = 0;
        ally->vanished = true;
        Roam__SetState(ally, UNIT_DEAD, now);
        return;
    }
    if (Roam__Animate(roam, ally, now)) {
        if (ally->state == UNIT_ATTACK) {
            const int target = Roam__AllyTarget(roam, ally);
            if (target >= 0 && Roam__Distance(ally->x, ally->y, roam->units[target].x, roam->units[target].y) <=
                                   fmaxf(ATTACK_RANGE, ally->reach) + 1.0f) {
                Unit *victim = &roam->units[target];
                if (ally->reach > 0.0f) { // an arrow or a bolt on its way (the damage is done now)
                    const char *shot = ally->element == ELEMENT_FIRE   ? "firearrow"
                                       : ally->element == ELEMENT_COLD ? "coldarrow"
                                                                       : "arrow";
                    // A sorcerer's spell (hireling.txt: Fire Ball, Glacial Spike, Charged Bolt, Lightning, ...)
                    if (ally->hireling && roam->character->hireling > 0) {
                        HireSkill skills[HIRE_SKILLS];
                        const int count = Hirelings_Skills(roam->character->hireling - 1,
                                                           roam->character->hireling_level, skills, HIRE_SKILLS);
                        int       pick  = -1;
                        int       seen  = 0;
                        for (int s = 0; s < count; s++) {
                            const SkillRow *row = SkillData_Get(skills[s].name);
                            const char     *name =
                                row != NULL ? (row->missile[0] != '\0' ? row->missile : row->missile_a) : "";
                            if (skills[s].mode == 7 && row != NULL && row->kind == SKILL_KIND_MISSILE &&
                                MissileData_Get(name) != NULL && Roam__Roll(0, seen++) == 0) {
                                pick = s;
                            }
                        }
                        if (pick >= 0) {
                            const SkillRow *row = SkillData_Get(skills[pick].name);
                            shot                = row->missile[0] != '\0' ? row->missile : row->missile_a;
                        }
                    }
                    Roam__FireMissilesFrom(roam, shot, NULL, 0, 1, 0.0f, false, ally->x, ally->y, victim->x, victim->y,
                                           true);
                    Roam__SendShot(roam, shot, ally->x, ally->y, victim->x, victim->y, NULL);
                }
                if (Roam__Roll(1, 100) <=
                    Character_HitChance(ally->attack_rating, ally->level, victim->defense, victim->level)) {
                    int damage[ELEMENT_COUNT] = {0};
                    damage[ELEMENT_PHYSICAL]  = Roam__Roll(ally->damage_min, ally->damage_max);
                    if (ally->element > 0 && ally->element < ELEMENT_COUNT) {
                        damage[ally->element] = Roam__Roll(ally->element_min, ally->element_max);
                    }
                    Roam__SkillHit(roam, victim, damage, 0, 0, true, now);
                }
            }
        }
        Roam__SetState(ally, ally->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
    }
    if (!Roam__IsAlive(ally) || ally->state == UNIT_ATTACK || ally->state == UNIT_HIT) {
        return;
    }
    const float to_player = Roam__Distance(ally->x, ally->y, roam->player.x, roam->player.y);
    if (to_player > 45.0f) {
        // Left far behind (a waypoint, a long run): it catches up
        Roam__ArrivalSpot(roam->map, roam->player.x + 2.0f, roam->player.y + 2.0f, &ally->x, &ally->y);
        Roam__ClearPath(ally);
        return;
    }
    const int target = to_player < SKILL_SUMMON_LEASH ? Roam__AllyTarget(roam, ally) : -1;
    float     goal_x = roam->player.x;
    float     goal_y = roam->player.y;
    if (target >= 0) {
        const Unit *victim = &roam->units[target];
        if (Roam__Distance(ally->x, ally->y, victim->x, victim->y) <= fmaxf(ATTACK_RANGE, ally->reach)) {
            Roam__TurnTowards(&ally->turn, ally->x, ally->y, victim->x, victim->y);
            Roam__ClearPath(ally);
            if (now >= ally->next_attack && Roam__CanAttack(roam, ally)) {
                ally->next_attack = now + ATTACK_COOLDOWN;
                Roam__SetState(ally, UNIT_ATTACK, now);
                Roam__UnitSound(roam, ally, MONSTER_SOUND_ATTACK);
            } else {
                Roam__SetState(ally, UNIT_IDLE, now);
            }
            return;
        }
        goal_x = victim->x;
        goal_y = victim->y;
    } else if (to_player < 4.0f) {
        Roam__SetState(ally, UNIT_IDLE, now);
        return;
    }
    Roam__PlanPath(roam->map, ally, goal_x, goal_y, now, REPATH_MONSTER_MS);
    const bool moved = Roam__FollowPath(roam->map, ally, ally->speed * 1.2f, seconds);
    Roam__SetState(ally, moved ? UNIT_WALK : UNIT_IDLE, now);
}

// The ally nearest a monster (closer than the player it would otherwise go for), -1 if none
static int Roam__AllyNear(const SceneRoam *roam, const Unit *unit, const float than) {
    int   best     = -1;
    float distance = than;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *ally = &roam->units[i];
        if (!ally->ally || !Roam__IsAlive(ally)) {
            continue;
        }
        const float d = Roam__Distance(unit->x, unit->y, ally->x, ally->y);
        if (d < distance) {
            distance = d;
            best     = i;
        }
    }
    return best;
}
