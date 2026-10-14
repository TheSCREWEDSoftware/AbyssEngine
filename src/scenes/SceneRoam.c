#include "../audio/Sounds.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../drawing/Button.h"
#include "../drawing/ConfirmDialog.h"
#include "../drawing/Cursor.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/LoadingScreen.h"
#include "../drawing/Screenshot.h"
#include "../drawing/Sprite.h"
#include "../drawing/TextBox.h"
#include "../game/Character.h"
#include "../game/Controls.h"
#include "../game/Settings.h"
#include "../managers/InputManager.h"
#include "../map/Automap.h"
#include "../map/LevelTypes.h"
#include "../map/Map.h"
#include "../map/MapRenderer.h"
#include "../map/MapSession.h"
#include "../map/MapUnits.h"
#include "../map/MonsterStats.h"
#include "../map/Objects.h"
#include "../map/Pathfinding.h"
#include "../map/Places.h"
#include "../map/World.h"
#include "CatalogCharacters.h"
#include "SceneCharacterSelect.h"
#include "SceneFreeRoam.h"
#include "SceneGame.h"
#include "SceneMainMenu.h"
#include "SceneMapEditor.h"
#include "SceneOptions.h"
#include "SceneTestingGrounds.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Testing Grounds and Free Roam: a character walking around a map with NPCs and monsters, and optional combat.

#define VIEW_W           800
#define VIEW_H           558
#define BOTTOM_Y         562
#define BUTTON_STEP      132
#define PANEL_X          580
#define PANEL_INNER_X    588
#define PANEL_INNER_W    204
#define CATEGORIES_Y     48
#define PREVIEW_BUTTON_Y 500 // Monsters panel: placement preview toggle
#define LIST_ROWS        20
#define LIST_ROW_H       18
#define SEED_LABEL_Y     418 // Free Roam: the world seed, under the Places list
#define SEED_BOX_Y       432
#define SEED_BUTTONS_Y   460
#define MAX_UNITS        2048 // an act's outdoors joined into one map has many
#define WALK_SPEED       5.0f // sub-tiles per second
#define RUN_SPEED        9.0f
#define ARRIVE_DISTANCE  0.15f

// Combat
#define AGGRO_RANGE       10.0f // sub-tiles
#define ATTACK_RANGE      1.6f
#define ATTACK_COOLDOWN   900  // ms between a monster's attacks
#define USE_RANGE         1.5f // how close to an object's footprint the player must be to open it
#define RESPAWN_DELAY     3000
#define RESPAWN_GRACE     3000 // after coming back, monsters leave you alone for a moment
#define PLAYER_DAMAGE_MIN 6
#define PLAYER_DAMAGE_MAX 14
// Wandering (NPCs in town, idle monsters): pause, then stroll to a random spot near home
#define WANDER_NPC_RADIUS     4.0f // sub-tiles from home
#define WANDER_MONSTER_RADIUS 3.0f
#define WANDER_PAUSE_MIN_MS   2500
#define WANDER_PAUSE_MAX_MS   7000
#define WANDER_SPEED_MAX      3.0f // strolling, not rushing
#define REPATH_PLAYER_MS      250  // while the mouse is held / the target moves
#define REPATH_MONSTER_MS     500
#define REPATH_DISTANCE       1.0f // re-plan when the goal moved this far
#define HOVER_RANGE           2.5f

#define DEFAULT_MAP_SIZE    32
#define DEFAULT_LEVEL_TYPE  "Act 1 - Town"
#define TESTING_GROUNDS_MAP "testing_grounds"
#define START_LEVEL_ID      1 // Rogue Encampment

static const char *player_classes[][3] = {
    // name, token, charstats class
    {"Amazon",      "am", "Amazon"     },
    {"Sorceress",   "so", "Sorceress"  },
    {"Necromancer", "ne", "Necromancer"},
    {"Paladin",     "pa", "Paladin"    },
    {"Barbarian",   "ba", "Barbarian"  },
    {"Druid",       "dz", "Druid"      },
    {"Assassin",    "ai", "Assassin"   },
};
#define PLAYER_CLASS_COUNT (int)(sizeof(player_classes) / sizeof(player_classes[0]))

static bool     combat_enabled = false; // kept between visits
static uint32_t fixed_seed     = 0;     // Free Roam: the world seed typed in (kept between visits), 0 = random

// Lighting: L cycles through these (a pool of light around the player, darkness everywhere else)
static const struct {
    const char *name;
    uint8_t     darkness;
    float       radius; // sub-tiles
} light_levels[] = {
    {"day",   0,   0    },
    {"dusk",  120, 16.0f},
    {"night", 215, 11.0f},
};
#define LIGHT_LEVEL_COUNT (int)(sizeof(light_levels) / sizeof(light_levels[0]))
static int light_level = 0; // kept between visits

typedef enum UnitState { UNIT_IDLE, UNIT_WALK, UNIT_ATTACK, UNIT_HIT, UNIT_DYING, UNIT_DEAD, UNIT_OPERATE } UnitState;

typedef struct DrawContext {
    Figure *figure;
    int     direction;
    int     frame;
} DrawContext;

typedef struct Unit {
    char        base[12];
    char        id[40];
    char        name[48];
    const char *idle_mode;
    float       x; // sub-tiles
    float       y;
    float       target_x; // walking towards
    float       target_y;
    int         turn;
    UnitState   state;
    const char *mode; // animation currently shown
    int         frame;
    uint64_t    frame_time;
    int         hp;
    int         max_hp;
    int         damage_min;
    int         damage_max;
    int         level;         // monsters: from monstats; the player: the character's
    int         defense;       // armor class
    int         attack_rating; // to-hit
    int         experience;    // given for killing it
    float       speed;
    bool        npc;
    bool        dropped;  // added in the Testing Grounds (Clear removes these)
    bool        provoked; // hit by the player: fights back even far away
    uint64_t    next_attack;
    MapPath     path;        // the way around walls to where it's going
    float       path_goal_x; // where the path was planned to
    float       path_goal_y;
    uint64_t    next_repath;
    float       home_x; // where the map placed it: wandering stays around here
    float       home_y;
    bool        wandering;   // on a stroll (not fighting)
    uint64_t    next_wander; // when to set off again
    int         can_walk;    // -1 = not checked yet, 0 = no walk animation, 1 = yes
    int         can_attack;  // -1 = not checked yet, 0 = no attack animation (never attacks), 1 = yes
    bool        vanished;    // died without death or corpse graphics: no longer drawn
    bool        is_object;   // a chest, door, shrine, torch, ...
    ObjectInfo  object;
    char        object_mode[4]; // the animation it rests in: "nu" (closed), "on" (opened, lit)
    bool        used;           // opened / activated: can't be clicked again
    int         rest_frame;     // >= 0: show this frame of the "op" animation (doors that rest on their last frame)
    DrawContext draw;
} Unit;

typedef enum RoamPanel { PANEL_NONE, PANEL_MONSTERS, PANEL_PLACES } RoamPanel;

#define TOWN_COUNT 5
static const int town_level_ids[TOWN_COUNT] = {1, 40, 75, 103, 109}; // Rogue Encampment ... Harrogath

#define WAYPOINT_RANGE    8.0f // Single Player: how close to a waypoint the Waypoint button works
#define MESSAGE_MS        4000 // level up and other messages
#define CAPTION_MS        3500 // "Entering The Blood Moor"; fades out over the last second
#define HARDCORE_DEATH_MS 5000 // time to read the bad news
#define CHAR_PANEL        "/data/global/ui/PANEL/invchar6.dc6"
#define CHAR_PANEL_W      320
#define CHAR_PANEL_H      432

typedef struct SceneRoam {
    bool         free_roam; // Free Roam, or the game (both travel between the game's areas)
    bool         game;      // Single Player: the character from Character_Current(), real combat and levels
    Map         *map;
    bool         borrowed; // the editor's map (play-test): not ours to free
    MapRenderer *renderer;
    MapUnits    *units_cache; // figures
    MapCamera    camera;
    uint64_t     last_tick;
    unsigned     draw_flags;
    int          place;      // Free Roam: current Places index (the area you're in)
    int          area_level; // the area you're in (joined-up maps: changes as you walk), Levels.txt Id

    // Player
    Unit     player;
    int      player_class;
    bool     running;
    bool     moving;
    int      attack_target; // index into units, -1 = none
    int      use_target;    // object being walked to, to open it; -1 = none
    float    spawn_x;
    float    spawn_y;
    uint64_t respawn_at;
    uint64_t safe_until; // grace period after respawning
    int      step_frame; // walk frame the last footstep was played on, -1 = none

    Unit units[MAX_UNITS];
    int  unit_count;
    int  hovered; // unit under the mouse, -1 = none

    // UI
    Button   *buttons[6];
    Button   *back_button;
    Button   *class_button;
    Button   *run_button;
    Button   *monsters_button;
    Button   *places_button; // Free Roam
    Button   *clear_button;  // Testing Grounds
    Button   *combat_button;
    Button   *character_button; // Single Player
    Button   *town_button;      // Single Player
    Button   *preview_button;   // in the Monsters panel
    bool      preview;          // show what right click would place, under the mouse
    bool      hover_valid;
    float     hover_x; // sub-tile under the mouse
    float     hover_y;
    RoamPanel panel;
    ListBox  *list;
    char    **list_names;
    int       list_count;
    int       selected;
    Label    *title_label;
    Label    *status_label;
    Label    *help_label;
    Label    *life_label;
    Label    *target_label;
    bool      prev_left;
    bool      prev_right;

    TextBox       *seed_box; // Free Roam: world seed (in the Places panel)
    Button        *seed_use_button;
    Button        *seed_random_button;
    Label         *seed_label;
    float          key_dx; // Modern / Combined movement: where the movement keys (or stick) point, sub-tiles; 0 = none
    float          key_dy;
    bool           menu_open; // the game menu (Esc): Options, leave, back to the game; the world waits
    Button        *menu_options_button;
    Button        *menu_leave_button;
    Button        *menu_return_button;
    OptionsPanel  *options_panel;   // Options opened from that menu
    ConfirmDialog *confirm;         // "Return to the main menu?" (Back and Leave in Free Roam / Testing Grounds)
    bool           screenshot;      // take one at the end of this frame's drawing
    bool           attack_in_place; // the swing under way was from where you stand: don't walk after the target
    bool exits_armed; // walked clear of every exit since arriving (so arriving on one doesn't send you straight back)

    // Single Player
    Character *character;
    float      mana; // kept as fractions while regenerating
    float      stamina;
    bool       exhausted; // ran out of stamina: walking until it's back up a bit
    bool       in_town;
    bool       char_panel; // the character screen
    Sprite    *char_panel_sprite;
    Label     *panel_label;       // small text on the character screen
    Label     *panel_value_label; // its numbers
    Label     *message_label;     // "Level up!", ...
    Automap   *automap;           // Tab
    bool       show_automap;
    Label     *caption_label; // "Entering The Blood Moor"
    uint64_t   caption_until;
    bool       announce; // this teleport is walking through an exit: "Entering ..." (not spawning or waypoints)
    char       message[96];
    uint64_t   message_until;
    uint64_t   leave_at; // hardcore death: back to the character list at this time
} SceneRoam;

static void Roam__TogglePanel(SceneRoam *roam, RoamPanel panel);

DEFINE_SCENE_CALLBACKS(TestingGrounds);
DEFINE_SCENE_CALLBACKS(FreeRoam);
DEFINE_SCENE_CALLBACKS(Game);

// Fighting is on: always in the game, by the debug switch elsewhere
static bool Roam__Combat(const SceneRoam *roam) { return roam->game || combat_enabled; }

// Town folk (Akara, the rogue scouts in the encampment, ...) never fight and can't be attacked, whatever the combat
// switch says: it's for monsters
static bool Roam__IsPeaceful(const SceneRoam *roam, const struct Unit *unit) {
    (void)roam;
    return unit->npc;
}

// ---------------------------------------------------------------------------------------------------------------------
// Units

static float Roam__Distance(const float ax, const float ay, const float bx, const float by) {
    return sqrtf(((ax - bx) * (ax - bx)) + ((ay - by) * (ay - by)));
}

static void Roam__TurnTowards(int *turn, const float from_x, const float from_y, const float to_x, const float to_y) {
    float ax;
    float ay;
    float bx;
    float by;
    MapView_SubtileToWorld(from_x, from_y, &ax, &ay);
    MapView_SubtileToWorld(to_x, to_y, &bx, &by);
    if (fabsf(bx - ax) + fabsf(by - ay) > 0.01f) {
        *turn = MapUnits_TurnTowards(bx - ax, by - ay);
    }
}

static bool Roam__IsPlayer(const SceneRoam *roam, const Unit *unit) { return unit == &roam->player; }

static bool Roam__IsAlive(const Unit *unit) { return unit->state != UNIT_DYING && unit->state != UNIT_DEAD; }

// Monsters and NPCs can fight; objects can't
static bool Roam__IsCombatant(const Unit *unit) { return unit->max_hp > 0; }

// Running (and not out of stamina)
// Running: the Run / walk switch, flipped while the run key is held
static bool Roam__IsRunning(const SceneRoam *roam) {
    return (roam->running != Controls_Held(ACTION_RUN_HOLD)) && !roam->exhausted;
}

// The settings for the class being played (or all classes')
static int Roam__Profile(const SceneRoam *roam) {
    return roam->game ? (int)roam->character->class_id + 1 : roam->player_class + 1;
}

static const GameplaySettings *Roam__Gameplay(const SceneRoam *roam) {
    return Settings_GetGameplay(Roam__Profile(roam));
}

// The player's town animations (relaxed) when nobody's fighting: in the game that's in town
static bool Roam__PlayerAtEase(const SceneRoam *roam) { return roam->game ? roam->in_town : !combat_enabled; }

static const char *Roam__Mode(const SceneRoam *roam, const Unit *unit) {
    const bool player = Roam__IsPlayer(roam, unit);
    switch (unit->state) {
    case UNIT_WALK:
        if (player) {
            return Roam__IsRunning(roam) ? "rn" : (Roam__PlayerAtEase(roam) ? "tw" : "wl");
        }
        return "wl";
    case UNIT_ATTACK:
        return "a1";
    case UNIT_HIT:
        return "gh";
    case UNIT_DYING:
        return "dt";
    case UNIT_DEAD:
        return "dd";
    case UNIT_OPERATE:
        return "op";
    case UNIT_IDLE:
        break;
    }
    if (unit->is_object) {
        return unit->object_mode;
    }
    if (player) {
        return Roam__PlayerAtEase(roam) ? "tn" : "nu";
    }
    return unit->idle_mode;
}

// The figure for the unit's current animation, falling back to standing still when it has no such animation
static Figure *Roam__Figure(SceneRoam *roam, const Unit *unit) {
    Figure *figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, Roam__Mode(roam, unit));
    if (figure == NULL && unit->state == UNIT_DEAD) {
        figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "dt"); // last frame of dying
    }
    if (figure == NULL) {
        figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id,
                                    Roam__IsPlayer(roam, unit) ? "tn" : unit->idle_mode);
    }
    return figure;
}

// Frames of the animation shown: [first, first + count). Objects' files often hold every state's frames (a chest's
// closed and open pictures): objects.txt says which ones each animation uses.
static int Roam__FrameRange(const Unit *unit, const Figure *figure, int *first) {
    int count = Figure_GetFramesPerDirection(figure);
    *first    = 0;
    if (unit->is_object) {
        const int mode = unit->state == UNIT_OPERATE ? 1 : Objects_ModeIndex(unit->object_mode);
        Objects_FrameRange(&unit->object, mode, count, first, &count);
    }
    return count;
}

static void Roam__SetState(Unit *unit, const UnitState state, const uint64_t now) {
    if (unit->state == state) {
        return;
    }
    unit->state      = state;
    unit->frame      = 0;
    unit->frame_time = now;
}

// Advances the animation. Returns true when a one-shot animation (attack, hit, dying) just finished.
static bool Roam__Animate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    Figure *figure = Roam__Figure(roam, unit);
    if (figure == NULL) {
        return unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING ||
               unit->state == UNIT_OPERATE;
    }
    int       first  = 0; // (where the frames start doesn't matter for timing)
    const int frames = Roam__FrameRange(unit, figure, &first);
    (void)first;
    const int  ms       = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
    const bool one_shot = unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING ||
                          unit->state == UNIT_OPERATE;
    if (unit->state == UNIT_DEAD) {
        unit->frame = frames > 0 ? frames - 1 : 0;
        return false;
    }
    if (now - unit->frame_time > 1000) {
        unit->frame_time = now - (uint64_t)ms; // after a pause, don't race through frames
    }
    while (frames > 0 && now - unit->frame_time >= (uint64_t)ms) {
        unit->frame_time += (uint64_t)ms;
        if (++unit->frame >= frames) {
            if (one_shot) {
                unit->frame = frames - 1;
                return true;
            }
            unit->frame = 0;
        }
    }
    return false;
}

static int Roam__Roll(const int low, const int high) { return high > low ? low + (rand() % (high - low + 1)) : low; }

static bool Roam__InitUnit(Unit *unit, const char *base, const char *id, const float x, const float y) {
    CatalogCharacterInfo info;
    if (!CatalogCharacters_Find(MapSession_GetBestiary(), base, id, &info)) {
        return false; // no graphics
    }
    memset(unit, 0, sizeof(Unit));
    snprintf(unit->base, sizeof(unit->base), "%s", base);
    snprintf(unit->id, sizeof(unit->id), "%s", id);
    snprintf(unit->name, sizeof(unit->name), "%s", info.name);
    unit->idle_mode  = info.idle_mode;
    unit->x          = x;
    unit->y          = y;
    unit->target_x   = x;
    unit->target_y   = y;
    unit->turn       = rand() % 16;
    unit->home_x     = x;
    unit->home_y     = y;
    unit->can_walk   = -1;
    unit->can_attack = -1;
    unit->rest_frame = -1;
    unit->state      = UNIT_IDLE;

    MonsterStats stats;
    if (strcmp(base, "monsters") == 0 && MonsterStats_Get(id, &stats)) {
        unit->max_hp        = Roam__Roll(stats.hp_min, stats.hp_max > stats.hp_min ? stats.hp_max : stats.hp_min);
        unit->max_hp        = unit->max_hp > 0 ? unit->max_hp : 20;
        unit->damage_min    = stats.damage_min > 0 ? stats.damage_min : 1;
        unit->damage_max    = stats.damage_max > unit->damage_min ? stats.damage_max : unit->damage_min + 2;
        unit->speed         = (float)(stats.velocity > 0 ? stats.velocity : 3);
        unit->npc           = stats.npc;
        unit->level         = stats.level > 0 ? stats.level : 1;
        unit->defense       = stats.defense;
        unit->attack_rating = stats.attack_rating > 0 ? stats.attack_rating : 10;
        unit->experience    = stats.experience;
    } else if (strcmp(base, "chars") == 0) {
        unit->max_hp     = 50;
        unit->damage_min = 2;
        unit->damage_max = 6;
        unit->speed      = WALK_SPEED;
    }
    unit->speed = unit->speed < 2.0f ? 2.0f : (unit->speed > 9.0f ? 9.0f : unit->speed);
    unit->hp    = unit->max_hp;
    if (strcmp(base, "objects") == 0 && Objects_Get(Objects_FindToken(id), &unit->object)) {
        unit->is_object = true;
        unit->turn      = 0;
        snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.idle_mode);
    }
    return true;
}

static Unit *Roam__AddUnit(SceneRoam *roam, const char *base, const char *id, const float x, const float y) {
    if (roam->unit_count >= MAX_UNITS) {
        return NULL;
    }
    Unit *unit = &roam->units[roam->unit_count];
    if (!Roam__InitUnit(unit, base, id, x, y)) {
        return NULL;
    }
    roam->unit_count++;
    return unit;
}

// One step towards (tx, ty); slides along walls. Returns false when blocked or arrived.
static bool Roam__Step(const Map *map, Unit *unit, const float tx, const float ty, const float speed,
                       const float seconds) {
    const float dx       = tx - unit->x;
    const float dy       = ty - unit->y;
    const float distance = sqrtf((dx * dx) + (dy * dy));
    if (distance < ARRIVE_DISTANCE) {
        return false;
    }
    const float step   = speed * seconds;
    const float amount = step >= distance ? 1.0f : step / distance;
    const float next_x = unit->x + (dx * amount);
    const float next_y = unit->y + (dy * amount);

    if (Map_IsWalkable(map, (int)floorf(next_x), (int)floorf(next_y))) {
        unit->x = next_x;
        unit->y = next_y;
    } else if (Map_IsWalkable(map, (int)floorf(next_x), (int)floorf(unit->y))) {
        unit->x = next_x;
    } else if (Map_IsWalkable(map, (int)floorf(unit->x), (int)floorf(next_y))) {
        unit->y = next_y;
    } else {
        return false;
    }
    return true;
}

// Plans a path to (tx, ty) when there is none yet, the goal moved, or it's time to look again
static void Roam__PlanPath(const Map *map, Unit *unit, const float tx, const float ty, const uint64_t now,
                           const uint64_t interval) {
    const bool moved_goal = Roam__Distance(tx, ty, unit->path_goal_x, unit->path_goal_y) > REPATH_DISTANCE;
    if (unit->path.next < unit->path.count && !moved_goal && now < unit->next_repath) {
        return;
    }
    if (!moved_goal && unit->path.next >= unit->path.count && now < unit->next_repath) {
        return; // tried recently and got nowhere
    }
    unit->path_goal_x = tx;
    unit->path_goal_y = ty;
    unit->next_repath = now + interval;
    Map_FindPath(map, unit->x, unit->y, tx, ty, &unit->path);
}

// Walks along the planned path. Returns false when it has arrived or is stuck.
static bool Roam__FollowPath(const Map *map, Unit *unit, const float speed, const float seconds) {
    while (unit->path.next < unit->path.count) {
        const float px = unit->path.x[unit->path.next];
        const float py = unit->path.y[unit->path.next];
        if (Roam__Distance(unit->x, unit->y, px, py) < ARRIVE_DISTANCE) {
            unit->path.next++;
            continue;
        }
        Roam__TurnTowards(&unit->turn, unit->x, unit->y, px, py);
        if (Roam__Step(map, unit, px, py, speed, seconds)) {
            return true;
        }
        unit->path.count = 0; // blocked
        return false;
    }
    return false;
}

static void Roam__ClearPath(Unit *unit) {
    unit->path.count  = 0;
    unit->path.next   = 0;
    unit->next_repath = 0;
}

// Whether the unit has graphics for an animation. A unit only does what it can be shown doing: no attack animation,
// no attacking (Gheed, Akara, ...); no get-hit animation, no flinching; no death animation, it drops straight dead.
static bool Roam__HasMode(SceneRoam *roam, const Unit *unit, const char *mode) {
    if (Roam__IsPlayer(roam, unit)) {
        return true; // players have every animation
    }
    // The Bestiary's list: the animations monstats2 says it has, that also have graphics (some, like Gheed's
    // attack, have files but aren't used by the game)
    return CatalogCharacters_HasMode(MapSession_GetBestiary(), unit->base, unit->id, mode);
}

static bool Roam__CanAttack(SceneRoam *roam, Unit *unit) {
    if (unit->can_attack < 0) {
        unit->can_attack = Roam__HasMode(roam, unit, "a1") ? 1 : 0;
    }
    return unit->can_attack == 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Sounds

// Swings, getting hit, dying: the player's class sounds (bare-handed swing), a monster's from MonSounds.txt
static void Roam__UnitSound(const SceneRoam *roam, const Unit *unit, const MonsterSound which) {
    if (Roam__IsPlayer(roam, unit)) {
        if (which == MONSTER_SOUND_ATTACK) {
            Sounds_Play("weapon_punch_1", SOUND_KIND_SFX);
            return;
        }
        char name[64];
        snprintf(name, sizeof(name), "%s_%s_1", player_classes[roam->player_class][2],
                 which == MONSTER_SOUND_HIT ? "hit" : "death");
        for (char *c = name; *c != '\0'; c++) {
            *c = (char)tolower((unsigned char)*c);
        }
        Sounds_Play(name, SOUND_KIND_SFX);
        return;
    }
    if (strcmp(unit->base, "monsters") == 0) {
        const char *name = Sounds_MonsterSound(unit->id, which);
        if (name != NULL) {
            Sounds_Play(name, SOUND_KIND_SFX);
        }
    }
}

// The footstep for the floor under the player: "light_walk_dirt_1", "light_run_snow_1", ...
static void Roam__Footstep(const SceneRoam *roam) {
    const uint16_t material = Map_GetFloorMaterial(roam->map, (int)floorf(roam->player.x), (int)floorf(roam->player.y));
    const char    *surface  = "dirt";
    if (material & (DT1_MATERIAL_WATER | DT1_MATERIAL_LAVA)) {
        return; // no footsteps for these
    }
    if (material & DT1_MATERIAL_SNOW) {
        surface = "snow";
    } else if (material & DT1_MATERIAL_SAND) {
        surface = "sand";
    } else if (material & DT1_MATERIAL_WOOD) {
        surface = "wood";
    } else if (material & DT1_MATERIAL_OUTSIDE_STONE) {
        surface = "ostone";
    } else if (material & DT1_MATERIAL_INSIDE_STONE) {
        surface = "istone";
    }
    char name[48];
    snprintf(name, sizeof(name), "light_%s_%s_1", Roam__IsRunning(roam) ? "run" : "walk", surface);
    Sounds_Play(name, SOUND_KIND_SFX);
}

static void Roam__Damage(SceneRoam *roam, Unit *unit, const int amount, const uint64_t now) {
    if (!Roam__IsAlive(unit)) {
        return;
    }
    unit->hp -= amount;
    if (unit->hp <= 0) {
        unit->hp         = 0;
        const bool dying = Roam__HasMode(roam, unit, "dt");
        unit->vanished   = !dying && !Roam__HasMode(roam, unit, "dd");
        Roam__SetState(unit, dying ? UNIT_DYING : UNIT_DEAD, now);
        Roam__UnitSound(roam, unit, MONSTER_SOUND_DEATH);
    } else {
        Roam__UnitSound(roam, unit, MONSTER_SOUND_HIT);
        if ((unit->state == UNIT_IDLE || unit->state == UNIT_WALK) && Roam__HasMode(roam, unit, "gh")) {
            Roam__SetState(unit, UNIT_HIT, now);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects

static bool Roam__CanUse(const Unit *unit) { return unit->is_object && unit->object.selectable && !unit->used; }

// The object's footprint in sub-tiles (what it blocks), at least the sub-tile it stands on
static void Roam__Footprint(const Unit *unit, int *x0, int *y0, int *width, int *height) {
    *width  = unit->object.size_x > 0 ? unit->object.size_x : 1;
    *height = unit->object.size_y > 0 ? unit->object.size_y : 1;
    *x0     = (int)floorf(unit->x) - (*width / 2);
    *y0     = (int)floorf(unit->y) - (*height / 2);
}

// How far a point is from the edge of an object's footprint
static float Roam__DistanceToObject(const Unit *unit, const float x, const float y) {
    int x0;
    int y0;
    int width;
    int height;
    Roam__Footprint(unit, &x0, &y0, &width, &height);
    const float dx = fmaxf(fmaxf((float)x0 - x, 0.0f), x - (float)(x0 + width));
    const float dy = fmaxf(fmaxf((float)y0 - y, 0.0f), y - (float)(y0 + height));
    return sqrtf((dx * dx) + (dy * dy));
}

// Objects' footprints block walking while their animation says so (chests, closed doors, ...)
static void Roam__UpdateBlocking(SceneRoam *roam) {
    Map_ClearBlocked(roam->map);
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!unit->is_object) {
            continue;
        }
        const int mode = unit->state == UNIT_OPERATE ? 1 : Objects_ModeIndex(unit->object_mode);
        if (mode < 0 || !unit->object.collision[mode] || unit->object.size_x <= 0 || unit->object.size_y <= 0) {
            continue;
        }
        int x0;
        int y0;
        int width;
        int height;
        Roam__Footprint(unit, &x0, &y0, &width, &height);
        Map_SetBlocked(roam->map, x0, y0, width, height, true);
    }
}

static void Roam__ObjectSound(const Unit *unit) {
    const char *name = NULL;
    if (unit->object.is_door) {
        name = "object_door_wood_open";
    } else {
        switch (unit->object.operate_fn) {
        case 1:
            name = "object_casket";
            break;
        case 2:
            name = "object_shrine_holy";
            break;
        case 4:
            name = unit->object.size_x * unit->object.size_y >= 6 ? "object_chest_large" : "object_chest_small";
            break;
        case 23:
            name = "object_waypoint_open";
            break;
        default:
            break;
        }
    }
    if (name != NULL) {
        Sounds_Play(name, SOUND_KIND_SFX);
    }
}

// Done opening: rest in the "opened" animation (a chest's open lid, an active waypoint, an open door)
static void Roam__FinishOperate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.has_mode[2] ? "on" : "nu");
    // Some doors have nothing in their "on" files but end their opening on the open door: rest on that frame
    Figure *op = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op");
    if (unit->object.is_door && op != NULL && Figure_GetFramesPerDirection(op) > 1) {
        snprintf(unit->object_mode, sizeof(unit->object_mode), "op");
        unit->rest_frame = Figure_GetFramesPerDirection(op) - 1;
    }
    Roam__SetState(unit, UNIT_IDLE, now);
    Roam__UpdateBlocking(roam);
}

static void Roam__Operate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    if (roam->game && unit->object.operate_fn == 23) {
        // A waypoint: lights up the first time, and opens the list of places every time
        if (strcmp(unit->object_mode, "on") != 0 && unit->state != UNIT_OPERATE) {
            Roam__ObjectSound(unit);
            if (unit->object.has_mode[1] && MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
                Roam__SetState(unit, UNIT_OPERATE, now);
            } else {
                Roam__FinishOperate(roam, unit, now);
            }
        }
        if (roam->panel != PANEL_PLACES) {
            Roam__TogglePanel(roam, PANEL_PLACES);
        }
        return;
    }
    unit->used = true;
    Roam__ObjectSound(unit);
    if (unit->object.has_mode[1] && unit->object.frame_count[1] > 0 &&
        MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
        Roam__SetState(unit, UNIT_OPERATE, now);
        Roam__UpdateBlocking(roam);
    } else {
        Roam__FinishOperate(roam, unit, now);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Player

static int Roam__PlayerLife(const int player_class) {
    int        life  = 50;
    DataTable *table = DataTable_Load(EXCEL_CHARSTATS);
    if (table != NULL) {
        const int c_class = DataTable_GetColumnIndex(table, "class");
        const int c_vit   = DataTable_GetColumnIndex(table, "vit");
        const int c_add   = DataTable_GetColumnIndex(table, "hpadd");
        for (int row = 0; row < DataTable_GetRowCount(table); row++) {
            if (strcmp(DataTable_GetString(table, row, c_class), player_classes[player_class][2]) == 0) {
                life = DataTable_GetInt(table, row, c_vit) + DataTable_GetInt(table, row, c_add);
                break;
            }
        }
        DataTable_Destroy(&table);
    }
    return life > 0 ? life : 50;
}

// The character's numbers onto the player (after levelling up or spending points); life stays as it is
static void Roam__ApplyCharacter(SceneRoam *roam) {
    if (!roam->game) {
        return;
    }
    Character *character  = roam->character;
    Unit      *player     = &roam->player;
    player->max_hp        = Character_MaxLife(character);
    player->hp            = player->hp > player->max_hp ? player->max_hp : player->hp;
    player->level         = character->level;
    player->defense       = Character_Defense(character);
    player->attack_rating = Character_AttackRating(character);
    Character_Damage(character, &player->damage_min, &player->damage_max);
    const float max_mana    = (float)Character_MaxMana(character);
    const float max_stamina = (float)Character_MaxStamina(character);
    roam->mana              = roam->mana > max_mana ? max_mana : roam->mana;
    roam->stamina           = roam->stamina > max_stamina ? max_stamina : roam->stamina;
}

// What the character has now goes back into it (before saving)
static void Roam__StoreCharacter(SceneRoam *roam) {
    if (!roam->game) {
        return;
    }
    roam->character->life    = roam->player.hp;
    roam->character->mana    = (int)roam->mana;
    roam->character->stamina = (int)roam->stamina;
    Character_Save(roam->character);
}

static void Roam__ShowMessage(SceneRoam *roam, const char *text) {
    snprintf(roam->message, sizeof(roam->message), "%s", text);
    Label_SetText(roam->message_label, text);
    roam->message_until = SDL_GetTicks64() + MESSAGE_MS;
}

static void Roam__ResetPlayer(SceneRoam *roam) {
    Unit *player = &roam->player;
    memset(player, 0, sizeof(Unit));
    snprintf(player->base, sizeof(player->base), "chars");
    snprintf(player->id, sizeof(player->id), "%s", player_classes[roam->player_class][1]);
    snprintf(player->name, sizeof(player->name), "%s", player_classes[roam->player_class][0]);
    player->idle_mode   = "tn";
    player->x           = roam->spawn_x;
    player->y           = roam->spawn_y;
    player->max_hp      = Roam__PlayerLife(roam->player_class);
    player->hp          = player->max_hp;
    player->damage_min  = PLAYER_DAMAGE_MIN;
    player->damage_max  = PLAYER_DAMAGE_MAX;
    player->state       = UNIT_IDLE;
    player->frame_time  = SDL_GetTicks64();
    player->level       = 1;
    roam->moving        = false;
    roam->attack_target = -1;
    roam->use_target    = -1;
    if (roam->game) {
        player->hp = roam->character->life > 0 ? roam->character->life : Character_MaxLife(roam->character);
        snprintf(player->name, sizeof(player->name), "%s", roam->character->name);
        Roam__ApplyCharacter(roam);
    }
}

// The player opens the object being walked to once close enough. Returns true when that's done (or there's nothing
// to open any more).
static bool Roam__TryUse(SceneRoam *roam, const uint64_t now) {
    Unit *player = &roam->player;
    Unit *object = &roam->units[roam->use_target];
    if (!Roam__CanUse(object)) {
        roam->use_target = -1;
        return true;
    }
    if (Roam__DistanceToObject(object, player->x, player->y) > USE_RANGE) {
        return false;
    }
    Roam__TurnTowards(&player->turn, player->x, player->y, object->x, object->y);
    roam->moving     = false;
    roam->use_target = -1;
    Roam__ClearPath(player);
    Roam__Operate(roam, object, now);
    return true;
}

// Start on the walkable sub-tile closest to the area's entrance (generated areas), or else the middle of the map
static void Roam__FindSpawn(SceneRoam *roam) {
    const int cx  = roam->map->has_start ? (int)roam->map->start_x : (roam->map->width * MAP_SUBTILES) / 2;
    const int cy  = roam->map->has_start ? (int)roam->map->start_y : (roam->map->height * MAP_SUBTILES) / 2;
    roam->spawn_x = (float)cx + 0.5f;
    roam->spawn_y = (float)cy + 0.5f;
    for (int radius = 0; radius < MAP_MAX_SIZE * MAP_SUBTILES; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if ((abs(dx) == radius || abs(dy) == radius) && Map_IsWalkable(roam->map, cx + dx, cy + dy)) {
                    roam->spawn_x = (float)(cx + dx) + 0.5f;
                    roam->spawn_y = (float)(cy + dy) + 0.5f;
                    return;
                }
            }
        }
    }
}

// Units for the map's NPCs, monsters and objects; then the player at the spawn point
static void Roam__Populate(SceneRoam *roam) {
    roam->unit_count = 0;
    roam->hovered    = -1;
    for (int e = 0; e < roam->map->entity_count; e++) {
        const MapEntity *entity = &roam->map->entities[e];
        Unit            *unit   = Roam__AddUnit(roam, entity->base, entity->id, entity->x, entity->y);
        if (unit != NULL) {
            unit->turn = entity->direction;
            if (unit->is_object && entity->object_row >= 0) {
                Objects_Get(entity->object_row, &unit->object); // the exact object the map placed
                snprintf(unit->object_mode, sizeof(unit->object_mode), "%s",
                         entity->mode[0] != '\0' ? entity->mode : unit->object.idle_mode);
            }
        }
    }
    Roam__UpdateBlocking(roam);
    Roam__FindSpawn(roam);
    Roam__ResetPlayer(roam);
}

// ---------------------------------------------------------------------------------------------------------------------
// Default map

Map *TestingGrounds_CreateDefaultMap(void) {
    const int        type_index = LevelTypes_Find(DEFAULT_LEVEL_TYPE);
    const LevelType *type       = LevelTypes_Get(type_index >= 0 ? type_index : 0);
    Map             *map        = NULL;
    if (type != NULL) {
        map = Map_CreateBlank(DEFAULT_MAP_SIZE, DEFAULT_MAP_SIZE, type->act, (const char *const *)type->files,
                              type->file_count);
    } else {
        map = Map_CreateBlank(DEFAULT_MAP_SIZE, DEFAULT_MAP_SIZE, 0, NULL, 0);
    }
    snprintf(map->name, sizeof(map->name), "Testing Grounds");

    // Grass everywhere, a dirt square in the middle
    const MapTileChoice *floors;
    const int            floor_count = Map_GetChoices(map, MAP_LAYER_FLOOR, &floors);
    const MapTileChoice *grass       = floor_count > 0 ? &floors[0] : NULL;
    const MapTileChoice *dirt        = floor_count > 1 ? &floors[1] : grass;
    for (int i = 0; i < floor_count; i++) {
        if (floors[i].style == 5 && floors[i].sequence == 0) {
            grass = &floors[i];
        }
        if (floors[i].style == 0 && floors[i].sequence == 1) {
            dirt = &floors[i];
        }
    }
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            const bool middle = tx >= 12 && tx < 20 && ty >= 12 && ty < 20;
            Map_SetCell(map, MAP_LAYER_FLOOR, 0, tx, ty, middle ? dirt : grass);
        }
    }

    // A few training targets around the middle
    static const struct {
        const char *id;
        const char *name;
        float       x;
        float       y;
    } dummies[] = {
        {"zombie1",   "Zombie",    70.5f, 70.5f},
        {"fallen1",   "Fallen",    90.5f, 70.5f},
        {"quillrat1", "Quill Rat", 70.5f, 90.5f},
        {"skeleton1", "Skeleton",  90.5f, 90.5f},
    };
    for (size_t d = 0; d < sizeof(dummies) / sizeof(dummies[0]); d++) {
        Map_AddEntity(map, "monsters", dummies[d].id, dummies[d].name, dummies[d].x, dummies[d].y);
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
// UI

static void Roam__UpdateButtons(SceneRoam *roam) {
    Button_SetText(roam->run_button, roam->running ? "Run" : "Walk");
    Button_SetText(roam->preview_button, roam->preview ? "Preview On" : "Preview Off");
    Button_SetToggled(roam->preview_button, roam->preview);
    if (roam->game) {
        Button_SetText(roam->places_button, roam->panel == PANEL_PLACES ? "Close" : "Waypoint");
        Button_SetToggled(roam->character_button, roam->char_panel);
        Button_SetEnabled(roam->town_button, !roam->in_town);
        return;
    }
    Button_SetText(roam->class_button, player_classes[roam->player_class][0]);
    Button_SetText(roam->monsters_button, roam->panel == PANEL_MONSTERS ? "Hide List" : "Monsters");
    if (roam->places_button != NULL) {
        Button_SetText(roam->places_button, roam->panel == PANEL_PLACES ? "Hide List" : "Places");
    }
    Button_SetText(roam->combat_button, combat_enabled ? "Combat On" : "Combat Off");
    Button_SetToggled(roam->combat_button, combat_enabled);
}

static void Roam__FreeList(SceneRoam *roam) {
    for (int i = 0; i < roam->list_count; i++) {
        free(roam->list_names[i]);
    }
    free(roam->list_names);
    roam->list_names = NULL;
    roam->list_count = 0;
}

static void Roam__RefreshList(SceneRoam *roam) {
    Roam__FreeList(roam);
    int list_y = CATEGORIES_Y;
    if (roam->panel == PANEL_PLACES) {
        Label_SetText(roam->title_label, roam->game ? "Waypoint" : "Places");
        roam->list_count = Places_GetCount();
        roam->list_names = calloc(roam->list_count > 0 ? (size_t)roam->list_count : 1, sizeof(char *));
        FAIL_IF_NULL(roam->list_names);
        for (int i = 0; i < roam->list_count; i++) {
            char name[80];
            snprintf(name, sizeof(name), "Act %d - %s", Places_Get(i)->act + 1, Places_Get(i)->name);
            roam->list_names[i] = strdup(name);
            FAIL_IF_NULL(roam->list_names[i]);
        }
    } else {
        Label_SetText(roam->title_label, "Monsters");
        const CatalogCharacters *bestiary = MapSession_GetBestiary();
        roam->list_count                  = CatalogCharacters_GetCount(bestiary);
        roam->list_names                  = calloc(roam->list_count > 0 ? (size_t)roam->list_count : 1, sizeof(char *));
        FAIL_IF_NULL(roam->list_names);
        for (int i = 0; i < roam->list_count; i++) {
            roam->list_names[i] = strdup(CatalogCharacters_GetName(bestiary, i));
            FAIL_IF_NULL(roam->list_names[i]);
        }
        list_y += CatalogCharacters_GetCategoriesHeight(bestiary, PANEL_INNER_W) + 4; // under the category bar
    }
    ListBox_SetItems(roam->list, (const char *const *)roam->list_names, roam->list_count);
    ListBox_SetPosition(roam->list, PANEL_INNER_X, list_y);
    if (roam->panel == PANEL_PLACES) {
        ListBox_SetSelectedIndex(roam->list, roam->place);
    }
    roam->selected = ListBox_GetSelectedIndex(roam->list);
}

static void Roam__TogglePanel(SceneRoam *roam, const RoamPanel panel) {
    roam->panel = roam->panel == panel ? PANEL_NONE : panel;
    if (roam->panel != PANEL_NONE) {
        Roam__RefreshList(roam);
    }
    Roam__UpdateButtons(roam);
}

// Free Roam: go to another place
static void Roam__EnteredArea(SceneRoam *roam, int level_id, bool caption);
static int  Roam__FindPlace(int level_id);
static void Roam__ArrivalSpot(const Map *map, float x, float y, float *out_x, float *out_y);

static void Roam__Teleport(SceneRoam *roam, const int place_index) {
    const Place *place = Places_Get(place_index);
    if (place == NULL) {
        return;
    }
    // Already on the map (an act's outdoors are one map): just go there
    const MapArea *area = roam->map != NULL && !roam->borrowed && roam->map->seed == World_GetSeed()
                              ? Map_FindArea(roam->map, place->level_id)
                              : NULL;
    if (area != NULL) {
        Roam__ArrivalSpot(roam->map, area->start_x, area->start_y, &roam->spawn_x, &roam->spawn_y);
        roam->player.x        = roam->spawn_x;
        roam->player.y        = roam->spawn_y;
        roam->player.target_x = roam->spawn_x;
        roam->player.target_y = roam->spawn_y;
        roam->moving          = false;
        roam->attack_target   = -1;
        Roam__ClearPath(&roam->player);
        roam->exits_armed = false;
        Roam__EnteredArea(roam, place->level_id, roam->announce);
        return;
    }
    if (roam->game && roam->map != NULL && Roam__IsAlive(&roam->player)) {
        Roam__StoreCharacter(roam); // life as you leave
    }
    char text[96];
    snprintf(text, sizeof(text), place->generated ? "Building %s..." : "Loading %s...", place->name);
    LoadingScreen_Begin(text);
    Map *map = Places_LoadMap(place);
    if (map == NULL) {
        LoadingScreen_End();
        return;
    }
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
    }
    roam->map         = map;
    roam->borrowed    = false;
    roam->place       = place_index;
    roam->area_level  = place->level_id;
    roam->exits_armed = false;
    MapRenderer_ClearCache(roam->renderer);
    roam->in_town = false;
    for (int t = 0; t < TOWN_COUNT; t++) {
        roam->in_town = roam->in_town || town_level_ids[t] == place->level_id;
    }
    Roam__Populate(roam);
    // The automap of an act's outdoors is kept under its lowest level Id (the town), whichever area you came in by
    int automap_key = place->level_id;
    for (int a = 0; a < roam->map->area_count; a++) {
        automap_key = roam->map->areas[a].level_id < automap_key ? roam->map->areas[a].level_id : automap_key;
    }
    Automap_SetArea(roam->automap, roam->map, automap_key, World_GetSeed());
    if (roam->free_roam && roam->announce) {
        // The game's caption as you come in through an exit: "Entering The Blood Moor" (not where you start)
        char caption[96];
        World_GetEnteringText(place->level_id, caption, sizeof(caption));
        if (caption[0] != '\0') {
            Label_SetText(roam->caption_label, caption);
            roam->caption_until = SDL_GetTicks64() + CAPTION_MS;
        }
    }
    if (roam->game) {
        roam->character->act = place->act;
        roam->exhausted      = false;
        roam->panel          = PANEL_NONE;
        if (roam->in_town) {
            // In town you're looked after: life and mana back to full
            roam->player.hp = roam->player.max_hp;
            roam->mana      = (float)Character_MaxMana(roam->character);
        }
        Roam__StoreCharacter(roam);
    }
    LoadingScreen_End();
}

static int Roam__FindPlace(const int level_id) {
    for (int i = 0; i < Places_GetCount(); i++) {
        if (Places_Get(i)->level_id == level_id) {
            return i;
        }
    }
    return -1;
}

// Single Player: back to the act's town (dying, the Town button)
static void Roam__GoToTown(SceneRoam *roam, const int act) {
    const int place = Roam__FindPlace(town_level_ids[act >= 0 && act < TOWN_COUNT ? act : 0]);
    if (place >= 0) {
        Roam__Teleport(roam, place);
    }
}

// Free Roam: a new world seed (0 = random); the area you're in is built again from it
static void Roam__NewWorld(SceneRoam *roam, const uint32_t seed) {
    fixed_seed = seed;
    World_SetSeed(seed);
    Roam__Teleport(roam, roam->place);
    char text[64];
    snprintf(text, sizeof(text), "World seed %u", World_GetSeed());
    Roam__ShowMessage(roam, text);
}

// A walkable sub-tile near (x, y) that isn't on any exit
static void Roam__ArrivalSpot(const Map *map, const float x, const float y, float *out_x, float *out_y) {
    const int cx = (int)x;
    const int cy = (int)y;
    *out_x       = x;
    *out_y       = y;
    for (int radius = 0; radius < 60; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if ((abs(dx) != radius && abs(dy) != radius) || !Map_IsWalkable(map, cx + dx, cy + dy) ||
                    Map_ExitAt(map, (float)(cx + dx) + 0.5f, (float)(cy + dy) + 0.5f) != NULL) {
                    continue;
                }
                *out_x = (float)(cx + dx) + 0.5f;
                *out_y = (float)(cy + dy) + 0.5f;
                return;
            }
        }
    }
}

// Walked into another area of a joined-up map (or arrived in one): the game's caption, and town or not
static void Roam__EnteredArea(SceneRoam *roam, const int level_id, const bool caption_wanted) {
    if (level_id == roam->area_level) {
        return;
    }
    roam->area_level = level_id;
    const int place  = Roam__FindPlace(level_id);
    roam->place      = place >= 0 ? place : roam->place;
    char caption[96];
    World_GetEnteringText(level_id, caption, sizeof(caption));
    if (caption_wanted && caption[0] != '\0') {
        Label_SetText(roam->caption_label, caption);
        roam->caption_until = SDL_GetTicks64() + CAPTION_MS;
    }
    const bool was_in_town = roam->in_town;
    roam->in_town          = false;
    for (int t = 0; t < TOWN_COUNT; t++) {
        roam->in_town = roam->in_town || town_level_ids[t] == level_id;
    }
    if (roam->game && roam->in_town && !was_in_town) {
        // In town you're looked after: life and mana back to full
        roam->player.hp = roam->player.max_hp;
        roam->mana      = (float)Character_MaxMana(roam->character);
        Roam__StoreCharacter(roam);
    }
    Roam__UpdateButtons(roam);
}

// The name of where you are: the area you're standing in on a joined-up map
static const char *Roam__AreaName(const SceneRoam *roam) {
    const MapArea *area = Map_AreaAt(roam->map, roam->player.x, roam->player.y);
    return area != NULL ? area->name : roam->map->name;
}

// Through an exit into the next area, coming in at its way back
static void Roam__Travel(SceneRoam *roam, const int target_level) {
    const int from_level = roam->area_level;
    const int place      = Roam__FindPlace(target_level);
    if (place < 0) {
        Roam__ShowMessage(roam, "That way isn't open yet.");
        roam->exits_armed = false;
        return;
    }
    roam->announce = true;
    Roam__Teleport(roam, place);
    roam->announce      = false;
    const MapExit *back = Map_ExitTo(roam->map, from_level);
    if (back != NULL) {
        Roam__ArrivalSpot(roam->map, back->arrive_x, back->arrive_y, &roam->spawn_x, &roam->spawn_y);
        roam->player.x        = roam->spawn_x;
        roam->player.y        = roam->spawn_y;
        roam->player.target_x = roam->spawn_x;
        roam->player.target_y = roam->spawn_y;
    }
    roam->exits_armed = false;
}

// Single Player: whether a waypoint is near enough to use the Waypoint button
static bool Roam__NearWaypoint(const SceneRoam *roam) {
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 23 &&
            Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) <= WAYPOINT_RANGE) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

static void *Roam__Create(const bool free_roam, const bool game) {
    Cursor_SetVisible(true);

    SceneRoam *roam = calloc(1, sizeof(SceneRoam));
    FAIL_IF_NULL(roam);
    roam->free_roam     = free_roam || game;
    roam->game          = game;
    roam->renderer      = MapRenderer_Create();
    roam->units_cache   = MapUnits_Create();
    roam->camera.zoom   = 1.0f;
    roam->last_tick     = SDL_GetTicks64();
    roam->draw_flags    = MAP_DRAW_DEFAULT;
    roam->attack_target = -1;
    roam->use_target    = -1;
    roam->step_frame    = -1;
    roam->hovered       = -1;
    srand((unsigned)SDL_GetTicks64());

    roam->title_label  = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    roam->status_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    roam->help_label   = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    roam->life_label   = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    roam->target_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(roam->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(roam->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_SetAlignment(roam->target_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_END);
    Label_SetColor(roam->help_label, 0xA0, 0xA0, 0xA0, 0xFF);
    Label_SetText(roam->help_label, game ? "Left: walk / attack / open   R run   C character   V waypoint (near one)   "
                                           "Home town   Tab map   L light   Esc menu"
                                    : free_roam
                                        ? "Left walk/attack  Right drop  Shift+Right remove  R run  C combat  "
                                          "L light  B collision  Tab map  N new world  Esc menu"
                                        : "Left: walk/attack  Right: drop  Shift+Right: remove  R run  C combat  "
                                          "P preview  L light  B blocked  Esc menu");

    const char *names[6] = {
        "Back", "Class", "Walk", free_roam ? "Places" : "Monsters", free_roam ? "Monsters" : "Clear", "Combat"};
    if (game) {
        names[0] = "Exit Game";
        names[1] = "Character";
        names[3] = "Waypoint";
        names[4] = "Town";
    }
    for (int i = 0; i < 6; i++) {
        roam->buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, names[i]);
        Button_SetPosition(roam->buttons[i], 8 + (i * BUTTON_STEP), BOTTOM_Y);
    }
    roam->back_button   = roam->buttons[0];
    roam->class_button  = roam->buttons[1];
    roam->run_button    = roam->buttons[2];
    roam->combat_button = roam->buttons[5];
    if (game) {
        roam->class_button     = NULL;
        roam->combat_button    = NULL;
        roam->character_button = roam->buttons[1];
        roam->places_button    = roam->buttons[3];
        roam->town_button      = roam->buttons[4];
        Button_SetVisible(roam->buttons[5], false);

        roam->character    = Character_Current();
        roam->player_class = (int)roam->character->class_id;
        roam->running      = true;
        roam->mana = (float)(roam->character->mana > 0 ? roam->character->mana : Character_MaxMana(roam->character));
        roam->stamina =
            (float)(roam->character->stamina > 0 ? roam->character->stamina : Character_MaxStamina(roam->character));
        roam->char_panel_sprite = Sprite_Create(CHAR_PANEL, PALETTE_UNITS);
        roam->panel_label       = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        roam->panel_value_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    } else if (free_roam) {
        roam->places_button   = roam->buttons[3];
        roam->monsters_button = roam->buttons[4];
    } else {
        roam->monsters_button = roam->buttons[3];
        roam->clear_button    = roam->buttons[4];
    }

    roam->menu_options_button = Button_Create(BUTTON_TYPE_WIDE, "Options");
    roam->menu_leave_button   = Button_Create(BUTTON_TYPE_WIDE, game ? "Save and Exit Game" : "Leave");
    roam->menu_return_button  = Button_Create(BUTTON_TYPE_WIDE, game ? "Return to Game" : "Back to Free Roam");
    roam->confirm             = ConfirmDialog_Create();
    Button_SetPosition(roam->menu_options_button, 264, 210);
    Button_SetPosition(roam->menu_leave_button, 264, 260);
    Button_SetPosition(roam->menu_return_button, 264, 310);
    roam->automap       = Automap_Create();
    roam->caption_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetAlignment(roam->caption_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    roam->message_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(roam->message_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(roam->message_label, 0xFF, 0xE8, 0x60, 0xFF);

    roam->preview        = true;
    roam->preview_button = Button_Create(BUTTON_TYPE_MEDIUM, "Preview On");
    Button_SetPosition(roam->preview_button, PANEL_INNER_X + 38, PREVIEW_BUTTON_Y);

    roam->list = ListBox_Create(FONTS_FONT8, PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W, LIST_ROWS, LIST_ROW_H);
    ListBox_SetBackground(roam->list, 0, 0, 0, 160);

    if (game || free_roam) {
        World_SetSeed(game ? 0 : fixed_seed); // a new world for every game; Free Roam keeps a seed you typed in
    }
    if (free_roam && !game) {
        roam->seed_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        roam->seed_box   = TextBox_Create(FONTS_FONT8, PANEL_INNER_X, SEED_BOX_Y, PANEL_INNER_W, 20, "type a seed");
        roam->seed_use_button    = Button_Create(BUTTON_TYPE_CANCEL, "Use");
        roam->seed_random_button = Button_Create(BUTTON_TYPE_CANCEL, "Random");
        Button_SetPosition(roam->seed_use_button, PANEL_INNER_X, SEED_BUTTONS_Y);
        Button_SetPosition(roam->seed_random_button, PANEL_INNER_X + PANEL_INNER_W - 96, SEED_BUTTONS_Y);
    }
    if (game) {
        Roam__GoToTown(roam, roam->character->act); // where the character was last
    } else if (free_roam) {
        // Start in the Rogue Encampment
        int start = 0;
        for (int i = 0; i < Places_GetCount(); i++) {
            if (Places_Get(i)->level_id == START_LEVEL_ID) {
                start = i;
            }
        }
        Roam__Teleport(roam, start);
    }
    if (roam->map == NULL) {
        if (!free_roam && MapSession_IsPlaytest() && MapSession_PeekMap() != NULL) {
            roam->map      = (Map *)MapSession_PeekMap();
            roam->borrowed = true;
        } else {
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s.abm", Map_GetSaveFolder(), TESTING_GROUNDS_MAP);
            roam->map = free_roam ? NULL : Map_LoadFile(path);
            if (roam->map == NULL) {
                roam->map = TestingGrounds_CreateDefaultMap();
            }
        }
        Roam__Populate(roam);
    }
    Roam__UpdateButtons(roam);
    return roam;
}

void *TestingGrounds_Create(void) { return Roam__Create(false, false); }

void *FreeRoam_Create(void) { return Roam__Create(true, false); }

void *Game_Create(void) { return Roam__Create(false, true); }

static void Roam__Free(void **scene_ref) {
    SceneRoam *roam = *(SceneRoam **)scene_ref;
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
    } else if (roam->map != NULL) {
        Map_ClearBlocked(roam->map); // the editor's map: leave it as it was
    }
    MapRenderer_Destroy(&roam->renderer);
    MapUnits_Destroy(&roam->units_cache);
    Roam__FreeList(roam);
    ListBox_Destroy(&roam->list);
    for (int i = 0; i < 6; i++) {
        Button_Destroy(&roam->buttons[i]);
    }
    Button_Destroy(&roam->preview_button);
    Label_Destroy(&roam->title_label);
    Label_Destroy(&roam->status_label);
    Label_Destroy(&roam->help_label);
    Label_Destroy(&roam->life_label);
    Label_Destroy(&roam->target_label);
    Sprite_Destroy(&roam->char_panel_sprite);
    TextBox_Destroy(&roam->seed_box);
    Button_Destroy(&roam->seed_use_button);
    Button_Destroy(&roam->seed_random_button);
    Label_Destroy(&roam->seed_label);
    Label_Destroy(&roam->caption_label);
    Automap_Destroy(&roam->automap);
    Button_Destroy(&roam->menu_options_button);
    Button_Destroy(&roam->menu_leave_button);
    Button_Destroy(&roam->menu_return_button);
    OptionsPanel_Destroy(&roam->options_panel);
    ConfirmDialog_Destroy(&roam->confirm);
    Label_Destroy(&roam->panel_label);
    Label_Destroy(&roam->panel_value_label);
    Label_Destroy(&roam->message_label);
    free(roam);
    *scene_ref = NULL;
}

void TestingGrounds_Free(void **scene_ref) { Roam__Free(scene_ref); }

void FreeRoam_Free(void **scene_ref) { Roam__Free(scene_ref); }

void Game_Free(void **scene_ref) { Roam__Free(scene_ref); }

// ---------------------------------------------------------------------------------------------------------------------
// Update

// Single Player: experience for a kill, and what comes with a new level
static void Roam__GainExperience(SceneRoam *roam, const Unit *victim) {
    if (!roam->game || victim->experience <= 0) {
        return;
    }
    const int levels = Character_AddExperience(roam->character, (uint32_t)victim->experience);
    if (levels > 0) {
        roam->player.hp = Character_MaxLife(roam->character); // a new level fills you up
        roam->mana      = (float)Character_MaxMana(roam->character);
        roam->stamina   = (float)Character_MaxStamina(roam->character);
        Roam__ApplyCharacter(roam);
        Sounds_Play("cursor_level_up", SOUND_KIND_UI);
        char text[96];
        snprintf(text, sizeof(text), "Level Up! You are now level %d  (C: spend your stat points)",
                 roam->character->level);
        Roam__ShowMessage(roam, text);
        Roam__StoreCharacter(roam);
    }
}

// The player's blow lands: in the game it can miss (attack rating against defense, levels), and kills give
// experience
static void Roam__PlayerStrikes(SceneRoam *roam, Unit *target, const uint64_t now) {
    const Unit *player = &roam->player;
    if (roam->game && Roam__Roll(1, 100) >
                          Character_HitChance(player->attack_rating, player->level, target->defense, target->level)) {
        return; // missed
    }
    const bool was_alive = Roam__IsAlive(target);
    Roam__Damage(roam, target, Roam__Roll(player->damage_min, player->damage_max), now);
    if (was_alive && !Roam__IsAlive(target)) {
        Roam__GainExperience(roam, target);
    }
}

// Attacking from where you stand (Stand still, Modern movement, the attack key): what's under the mouse if it's in
// reach, else the nearest monster in reach, else a swing at the air towards the mouse
static void Roam__AttackInPlace(SceneRoam *roam, const int hovered, const float x, const float y, const uint64_t now) {
    Unit *player = &roam->player;
    if (player->state == UNIT_ATTACK || player->state == UNIT_HIT || !Roam__IsAlive(player)) {
        return;
    }
    int target = -1;
    if (Roam__Combat(roam)) {
        float nearest = ATTACK_RANGE;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            if (!Roam__IsCombatant(unit) || !Roam__IsAlive(unit) || Roam__IsPeaceful(roam, unit)) {
                continue;
            }
            const float distance = Roam__Distance(player->x, player->y, unit->x, unit->y);
            if (i == hovered && distance <= ATTACK_RANGE) {
                target = i;
                break;
            }
            if (distance <= nearest) {
                nearest = distance;
                target  = i;
            }
        }
    }
    roam->moving          = false;
    roam->use_target      = -1;
    roam->attack_target   = target;
    roam->attack_in_place = true;
    Roam__ClearPath(player);
    Roam__TurnTowards(&player->turn, player->x, player->y, target >= 0 ? roam->units[target].x : x,
                      target >= 0 ? roam->units[target].y : y);
    Roam__SetState(player, UNIT_ATTACK, now);
    Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
}

static void Roam__UpdatePlayer(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit *player = &roam->player;

    if (player->state == UNIT_DEAD) {
        if (roam->game && roam->character->dead) {
            return; // hardcore: no coming back
        }
        if (now >= roam->respawn_at) {
            if (roam->game) {
                // Like the game: you wake up in town with full life
                roam->character->life = Character_MaxLife(roam->character);
                Roam__ResetPlayer(roam);
                Roam__GoToTown(roam, roam->character->act);
            } else {
                Roam__ResetPlayer(roam); // back at the start with full life
            }
            roam->safe_until = now + RESPAWN_GRACE;
            for (int i = 0; i < roam->unit_count; i++) {
                roam->units[i].provoked = false;
            }
        }
        return;
    }

    // Single Player: mana comes back over time; running uses stamina, resting brings it back
    if (roam->game && Roam__IsAlive(player)) {
        const float max_mana    = (float)Character_MaxMana(roam->character);
        const float max_stamina = (float)Character_MaxStamina(roam->character);
        const int   regen       = Character_ManaRegenSeconds(roam->character);
        roam->mana              = fminf(max_mana, roam->mana + (max_mana * seconds / (float)(regen > 0 ? regen : 120)));
        if (Roam__IsRunning(roam) && (roam->moving || roam->key_dx != 0.0f || roam->key_dy != 0.0f)) {
            // charstats RunDrain is per frame (25 a second), in 256ths
            roam->stamina -= (float)Character_RunDrain(roam->character) * 25.0f / 256.0f * seconds;
            if (roam->stamina <= 0.0f) {
                roam->stamina   = 0.0f;
                roam->exhausted = true;
            }
        } else {
            roam->stamina = fminf(max_stamina, roam->stamina + (max_stamina * seconds / 25.0f));
        }
        if (roam->exhausted && roam->stamina >= max_stamina * 0.1f) {
            roam->exhausted = false;
        }
    }
    if (Roam__Animate(roam, player, now)) {
        if (player->state == UNIT_ATTACK) {
            // The blow lands at the end of the swing
            if (roam->attack_target >= 0) {
                Unit *target = &roam->units[roam->attack_target];
                if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE + 0.5f) {
                    target->provoked = true;
                    Roam__PlayerStrikes(roam, target, now);
                }
            }
            if (roam->attack_in_place) {
                roam->attack_target   = -1; // one swing, no chasing
                roam->attack_in_place = false;
            }
            Roam__SetState(player, UNIT_IDLE, now);
        } else if (player->state == UNIT_DYING) {
            Roam__SetState(player, UNIT_DEAD, now);
            roam->respawn_at    = now + RESPAWN_DELAY;
            roam->attack_target = -1;
            if (roam->game && roam->character->hardcore) {
                roam->character->dead = true;
                roam->leave_at        = now + HARDCORE_DEATH_MS;
                Roam__StoreCharacter(roam);
                Roam__ShowMessage(roam, "You have died. Hardcore characters don't come back.");
            }
            return;
        } else {
            Roam__SetState(player, UNIT_IDLE, now);
        }
    }
    if (player->state == UNIT_DYING || player->state == UNIT_ATTACK || player->state == UNIT_HIT) {
        return;
    }

    // Modern / Combined: walking with the movement keys (or the stick), straight, sliding along walls
    const bool key_walking = roam->key_dx != 0.0f || roam->key_dy != 0.0f;
    if (key_walking) {
        roam->use_target    = -1;
        roam->attack_target = -1;
        roam->moving        = false;
        Roam__ClearPath(player);
        const float step = (Roam__IsRunning(roam) ? RUN_SPEED : WALK_SPEED) * seconds;
        const float nx   = player->x + (roam->key_dx * step);
        const float ny   = player->y + (roam->key_dy * step);
        if (Map_IsWalkable(roam->map, (int)nx, (int)ny)) {
            player->x = nx;
            player->y = ny;
        } else if (Map_IsWalkable(roam->map, (int)nx, (int)player->y)) {
            player->x = nx;
        } else if (Map_IsWalkable(roam->map, (int)player->x, (int)ny)) {
            player->y = ny;
        }
        Roam__TurnTowards(&player->turn, player->x, player->y, player->x + roam->key_dx, player->y + roam->key_dy);
        // Heading somewhere a few steps ahead (entrances take you in when you walk into them)
        player->target_x = player->x + (roam->key_dx * 3.0f);
        player->target_y = player->y + (roam->key_dy * 3.0f);
    }

    // Opening something: walk up to it, then open it
    if (!key_walking && roam->use_target >= 0 && !Roam__TryUse(roam, now)) {
        player->target_x = roam->units[roam->use_target].x;
        player->target_y = roam->units[roam->use_target].y;
        roam->moving     = true;
    }

    // Attacking: walk up to the target, then swing
    if (!key_walking && roam->attack_target >= 0) {
        Unit *target = &roam->units[roam->attack_target];
        if (!Roam__Combat(roam) || !Roam__IsAlive(target)) {
            roam->attack_target = -1;
        } else {
            if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE) {
                Roam__TurnTowards(&player->turn, player->x, player->y, target->x, target->y);
                roam->moving = false;
                Roam__ClearPath(player);
                Roam__SetState(player, UNIT_ATTACK, now);
                Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
                return;
            }
            player->target_x = target->x;
            player->target_y = target->y;
            roam->moving     = true;
        }
    }

    if (!key_walking && roam->moving) {
        // Around walls, not through them
        Roam__PlanPath(roam->map, player, player->target_x, player->target_y, now, REPATH_PLAYER_MS);
        roam->moving = Roam__FollowPath(roam->map, player, Roam__IsRunning(roam) ? RUN_SPEED : WALK_SPEED, seconds);
        if (!roam->moving) {
            Roam__ClearPath(player);
            if (roam->use_target >= 0 && !Roam__TryUse(roam, now)) {
                roam->use_target = -1; // got as close as it gets, and it's still out of reach
            }
        }
    }
    Roam__SetState(player, (roam->moving || key_walking) ? UNIT_WALK : UNIT_IDLE, now);

    // A footstep as each foot comes down (the start and the middle of the walk cycle)
    Figure *figure = player->state == UNIT_WALK ? Roam__Figure(roam, player) : NULL;
    if (figure != NULL) {
        const int frames = Figure_GetFramesPerDirection(figure);
        const int frame  = frames > 0 ? player->frame % frames : 0;
        if (frame != roam->step_frame && (frame == 0 || frame == frames / 2)) {
            Roam__Footstep(roam);
        }
        roam->step_frame = frame;
    } else {
        roam->step_frame = -1;
    }
}

// Strolling around home while nobody's fighting: wait a few seconds, walk to a random reachable spot nearby, repeat
static void Roam__Wander(SceneRoam *roam, Unit *unit, const float seconds, const uint64_t now) {
    if (unit->can_walk < 0) {
        // Guards keep their post; everyone else with a walk animation strolls
        const bool guard  = strstr(unit->id, "guard") != NULL;
        unit->can_walk    = !guard && Roam__HasMode(roam, unit, "wl") ? 1 : 0;
        unit->next_wander = now + (uint64_t)Roam__Roll(500, WANDER_PAUSE_MAX_MS); // don't all set off at once
    }
    if (unit->can_walk == 0) {
        Roam__SetState(unit, UNIT_IDLE, now);
        return;
    }

    if (unit->wandering) {
        const float speed = unit->speed < WANDER_SPEED_MAX ? unit->speed : WANDER_SPEED_MAX;
        if (Roam__FollowPath(roam->map, unit, speed, seconds)) {
            Roam__SetState(unit, UNIT_WALK, now);
            return;
        }
        unit->wandering   = false; // arrived (or bumped into something): rest a while
        unit->next_wander = now + (uint64_t)Roam__Roll(WANDER_PAUSE_MIN_MS, WANDER_PAUSE_MAX_MS);
        Roam__ClearPath(unit);
    }
    Roam__SetState(unit, UNIT_IDLE, now);

    if (now < unit->next_wander) {
        return;
    }
    // Somewhere walkable near home (a few tries), then plan the way there
    const float radius = unit->npc ? WANDER_NPC_RADIUS : WANDER_MONSTER_RADIUS;
    for (int attempt = 0; attempt < 6; attempt++) {
        const float angle    = (float)(rand() % 628) / 100.0f;
        const float distance = radius * (float)(30 + (rand() % 71)) / 100.0f;
        const float tx       = floorf(unit->home_x + (cosf(angle) * distance)) + 0.5f;
        const float ty       = floorf(unit->home_y + (sinf(angle) * distance)) + 0.5f;
        if (!Map_IsWalkable(roam->map, (int)floorf(tx), (int)floorf(ty)) ||
            Roam__Distance(tx, ty, unit->x, unit->y) < 1.0f) {
            continue;
        }
        Roam__ClearPath(unit);
        if (Map_FindPath(roam->map, unit->x, unit->y, tx, ty, &unit->path)) {
            unit->path_goal_x = tx;
            unit->path_goal_y = ty;
            unit->wandering   = true;
            return;
        }
    }
    unit->next_wander = now + WANDER_PAUSE_MIN_MS; // nowhere to go right now
}

static void Roam__UpdateUnits(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit      *player       = &roam->player;
    const bool player_alive = Roam__IsAlive(player) && now >= roam->safe_until; // can be fought

    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (Roam__Animate(roam, unit, now)) {
            if (unit->state == UNIT_ATTACK && player_alive && Roam__Combat(roam) &&
                Roam__Distance(unit->x, unit->y, player->x, player->y) <= ATTACK_RANGE + 0.5f &&
                (!roam->game || Roam__Roll(1, 100) <= Character_HitChance(unit->attack_rating, unit->level,
                                                                          player->defense, player->level))) {
                Roam__Damage(roam, player, Roam__Roll(unit->damage_min, unit->damage_max), now);
            }
            if (unit->state == UNIT_OPERATE) {
                Roam__FinishOperate(roam, unit, now);
            } else {
                Roam__SetState(unit, unit->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
            }
        }
        if (!Roam__IsAlive(unit) || !Roam__IsCombatant(unit) || unit->state == UNIT_ATTACK || unit->state == UNIT_HIT) {
            continue;
        }

        if (!Roam__Combat(roam) || !player_alive || Roam__IsPeaceful(roam, unit)) {
            unit->provoked = false;
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }

        // Only units that can attack (have an attack animation) fight back; the rest carry on strolling
        if (!Roam__CanAttack(roam, unit)) {
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }

        // Combat: everyone nearby that can fight (NPCs included) comes for the player
        const float distance = Roam__Distance(unit->x, unit->y, player->x, player->y);
        if (distance > AGGRO_RANGE && !unit->provoked) {
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }
        unit->wandering = false;
        if (distance <= ATTACK_RANGE) {
            Roam__TurnTowards(&unit->turn, unit->x, unit->y, player->x, player->y);
            Roam__ClearPath(unit);
            if (now >= unit->next_attack) {
                unit->next_attack = now + ATTACK_COOLDOWN;
                Roam__SetState(unit, UNIT_ATTACK, now);
                Roam__UnitSound(roam, unit, MONSTER_SOUND_ATTACK);
            } else {
                Roam__SetState(unit, UNIT_IDLE, now);
            }
        } else {
            // Chase: straight at the player when nothing's in the way, otherwise around the walls. Re-planning is
            // spread over time (each monster at its own moment) so a crowd doesn't all search in the same frame.
            bool moved;
            if (Map_HasClearLine(roam->map, unit->x, unit->y, player->x, player->y)) {
                Roam__ClearPath(unit);
                Roam__TurnTowards(&unit->turn, unit->x, unit->y, player->x, player->y);
                moved = Roam__Step(roam->map, unit, player->x, player->y, unit->speed, seconds);
            } else {
                Roam__PlanPath(roam->map, unit, player->x, player->y, now,
                               REPATH_MONSTER_MS + (uint64_t)((i % 8) * 37));
                moved = Roam__FollowPath(roam->map, unit, unit->speed, seconds);
            }
            Roam__SetState(unit, moved ? UNIT_WALK : UNIT_IDLE, now);
        }
    }
}

// The living unit nearest the mouse (within reach), -1 if none
static int Roam__UnitAt(const SceneRoam *roam, const float sub_x, const float sub_y) {
    int   nearest  = -1;
    float distance = HOVER_RANGE;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || (unit->is_object && !Roam__CanUse(unit))) {
            continue; // torches, opened chests, ... can't be clicked
        }
        // Figures stand on their feet but most of the body is above: aim a little higher up the screen
        const float d = Roam__Distance(unit->x - 0.8f, unit->y - 0.8f, sub_x, sub_y);
        if (d < distance) {
            distance = d;
            nearest  = i;
        }
    }
    return nearest;
}

// ---------------------------------------------------------------------------------------------------------------------
// Single Player: character screen (the game's own panel art) and keys

static const int stat_rows[STAT_COUNT] = {92, 154, 240, 302}; // strength, dexterity, vitality, energy (panel y)

static bool Roam__StatPlusAt(const int stat, const int x, const int y) {
    return x >= 112 && x <= 134 && y >= stat_rows[stat] - 10 && y <= stat_rows[stat] + 10;
}

static void Roam__PanelText(SceneRoam *roam, Label *label, const char *text, const int x, const int y, const uint8_t r,
                            const uint8_t g, const uint8_t b) {
    Label_SetText(label, text);
    Label_SetColor(label, r, g, b, 0xFF);
    Label_SetAlignment(label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_Draw(label, x, y);
    (void)roam;
}

static void Roam__DrawCharacterPanel(SceneRoam *roam) {
    const Character *character = roam->character;
    Sprite_DrawMulti(roam->char_panel_sprite, 0, 0, 0, 2, 2);
    Label *small = roam->panel_label;
    Label *value = roam->panel_value_label;
    char   text[64];

    Roam__PanelText(roam, value, character->name, 96, 17, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, value, Character_ClassName(character->class_id), 252, 17, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, small, "Level", 32, 39, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->level);
    Roam__PanelText(roam, value, text, 32, 57, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Experience", 122, 39, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%lu", (unsigned long)character->experience);
    Roam__PanelText(roam, value, text, 122, 57, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Next Level", 252, 39, 0xC7, 0xB3, 0x77);
    if (character->level < CHARACTER_MAX_LEVEL) {
        snprintf(text, sizeof(text), "%lu", (unsigned long)Character_ExperienceFor(character->level + 1));
    } else {
        snprintf(text, sizeof(text), "-");
    }
    Roam__PanelText(roam, value, text, 252, 57, 0xFF, 0xFF, 0xFF);

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        Roam__PanelText(roam, small, Character_StatName((CharacterStat)stat), 44, stat_rows[stat], 0xC7, 0xB3, 0x77);
        snprintf(text, sizeof(text), "%d", character->stats[stat]);
        Roam__PanelText(roam, value, text, 97, stat_rows[stat], 0xFF, 0xFF, 0xFF);
        if (character->stat_points > 0) {
            // Where the game shows its "+" button
            const SDL_Rect box = {116, stat_rows[stat] - 8, 16, 16};
            SDL_SetRenderDrawColor(sdl_renderer, 60, 50, 20, 255);
            SDL_RenderFillRect(sdl_renderer, &box);
            SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
            SDL_RenderDrawRect(sdl_renderer, &box);
            SDL_RenderDrawLine(sdl_renderer, box.x + 4, box.y + 8, box.x + 11, box.y + 8);
            SDL_RenderDrawLine(sdl_renderer, box.x + 8, box.y + 4, box.x + 8, box.y + 11);
        }
    }

    int damage_min;
    int damage_max;
    Character_Damage(character, &damage_min, &damage_max);
    Roam__PanelText(roam, small, "Damage", 210, 92, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d-%d", damage_min, damage_max);
    Roam__PanelText(roam, value, text, 286, 92, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, Character_WeaponName(character), 210, 115, 0xA0, 0xA0, 0xA0);
    Roam__PanelText(roam, small, "Attack Rating", 215, 154, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", Character_AttackRating(character));
    Roam__PanelText(roam, value, text, 291, 154, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Defense", 215, 177, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", Character_Defense(character));
    Roam__PanelText(roam, value, text, 291, 177, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Skill Points", 215, 201, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->skill_points);
    Roam__PanelText(roam, value, text, 291, 201, 0xFF, 0xFF, 0xFF);

    const struct {
        const char *name;
        int         y;
        int         current;
        int         maximum;
    } pools[3] = {
        {"Stamina", 240, (int)roam->stamina, Character_MaxStamina(character)},
        {"Life",    263, roam->player.hp,    Character_MaxLife(character)   },
        {"Mana",    302, (int)roam->mana,    Character_MaxMana(character)   },
    };
    for (int p = 0; p < 3; p++) {
        Roam__PanelText(roam, small, pools[p].name, 195, pools[p].y, 0xC7, 0xB3, 0x77);
        snprintf(text, sizeof(text), "%d", pools[p].current);
        Roam__PanelText(roam, value, text, 250, pools[p].y, 0xFF, 0xFF, 0xFF);
        snprintf(text, sizeof(text), "%d", pools[p].maximum);
        Roam__PanelText(roam, value, text, 291, pools[p].y, 0xFF, 0xFF, 0xFF);
    }

    static const char *resists[4] = {"Fire Resist", "Cold Resist", "Lightning Res.", "Poison Resist"};
    for (int r = 0; r < 4; r++) {
        Roam__PanelText(roam, small, resists[r], 222, 341 + (r * 24), 0xC7, 0xB3, 0x77);
        Roam__PanelText(roam, value, "0%", 291, 341 + (r * 24), 0xFF, 0xFF, 0xFF);
    }

    Roam__PanelText(roam, small, "Stat Points", 66, 396, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, small, "Remaining", 66, 410, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->stat_points);
    Roam__PanelText(roam, value, text, 145, 404, character->stat_points > 0 ? 0x40 : 0xFF, 0xFF,
                    character->stat_points > 0 ? 0x40 : 0xFF);
}

// Single Player keys, buttons and panels. Returns true when the scene is being left.
static bool Roam__UpdateGame(SceneRoam *roam, const uint64_t now, const int mouse_x, const int mouse_y,
                             const bool clicked) {
    if (roam->leave_at != 0 && now >= roam->leave_at) {
        Scene_Set(SCENE_REF(CharacterSelect)); // hardcore death
        return true;
    }

    if (Controls_Pressed(ACTION_CHARACTER) || Button_Update(roam->character_button)) {
        roam->char_panel = !roam->char_panel;
        Roam__UpdateButtons(roam);
    }
    if (roam->char_panel && clicked) {
        for (int stat = 0; stat < STAT_COUNT; stat++) {
            if (!Roam__StatPlusAt(stat, mouse_x, mouse_y)) {
                continue;
            }
            // Vitality and energy raise life, mana and stamina by what they add, now
            roam->character->life    = roam->player.hp;
            roam->character->mana    = (int)roam->mana;
            roam->character->stamina = (int)roam->stamina;
            if (Character_SpendStatPoint(roam->character, (CharacterStat)stat)) {
                roam->player.hp = roam->character->life;
                roam->mana      = (float)roam->character->mana;
                roam->stamina   = (float)roam->character->stamina;
                Roam__ApplyCharacter(roam);
                Sounds_Play("cursor_point_drop", SOUND_KIND_UI);
            }
        }
    }

    const bool can_waypoint = roam->in_town || Roam__NearWaypoint(roam);
    if (Controls_Pressed(ACTION_WAYPOINT) || Button_Update(roam->places_button)) {
        if (roam->panel == PANEL_PLACES || can_waypoint) {
            Roam__TogglePanel(roam, PANEL_PLACES);
        } else {
            Roam__ShowMessage(roam, "Stand near a waypoint (or go back to town) to travel.");
        }
    }
    if ((Controls_Pressed(ACTION_TOWN) || Button_Update(roam->town_button)) && !roam->in_town &&
        Roam__IsAlive(&roam->player)) {
        Roam__GoToTown(roam, roam->character->act);
        Roam__UpdateButtons(roam);
    }

    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        if (roam->panel != PANEL_NONE || roam->char_panel) {
            roam->panel      = PANEL_NONE;
            roam->char_panel = false;
            Roam__UpdateButtons(roam);
        } else {
            roam->menu_open = true; // the game menu, like the game's
        }
    }
    if (Button_Update(roam->back_button)) {
        if (!roam->character->dead) {
            Roam__StoreCharacter(roam);
        }
        Scene_Set(SCENE_REF(CharacterSelect));
        return true;
    }
    return false;
}

// Leaving from the game menu: Single Player saves and goes back to the character list; Free Roam to where it came from
static void Roam__Leave(SceneRoam *roam) {
    if (roam->game) {
        if (!roam->character->dead) {
            Roam__StoreCharacter(roam);
        }
        Scene_Set(SCENE_REF(CharacterSelect));
    } else {
        Scene_Set(roam->borrowed ? SCENE_REF(MapEditor) : SCENE_REF(MainMenu));
    }
}

// Free Roam and Testing Grounds ask before leaving (Back, or Leave in the menu)
static void Roam__AskLeave(SceneRoam *roam) {
    if (roam->borrowed) {
        ConfirmDialog_Open(roam->confirm, "Back to the Map Editor", "Stop play-testing and go back to the editor?",
                           "Yes", "No", NULL);
    } else {
        ConfirmDialog_Open(roam->confirm, "Main Menu", "Are you sure you wish to return to the main menu?", "Yes", "No",
                           NULL);
    }
}

// The game menu and Options over the world, which waits meanwhile. Returns true while either is open.
static bool Roam__UpdateMenu(SceneRoam *roam) {
    if (ConfirmDialog_IsOpen(roam->confirm)) {
        if (ConfirmDialog_Update(roam->confirm) == CONFIRM_FIRST) {
            roam->menu_open = false;
            Roam__Leave(roam);
        }
        return true;
    }
    if (roam->options_panel != NULL) {
        if (!OptionsPanel_Update(roam->options_panel)) {
            OptionsPanel_Destroy(&roam->options_panel);
        }
        return true;
    }
    if (!roam->menu_open) {
        return false;
    }
    InputManager_ClearPresses();
    if (Button_Update(roam->menu_options_button)) {
        roam->options_panel = OptionsPanel_Create();
    } else if (Button_Update(roam->menu_leave_button)) {
        if (roam->game) {
            roam->menu_open = false;
            Roam__Leave(roam);
        } else {
            Roam__AskLeave(roam);
        }
    } else if (Button_Update(roam->menu_return_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        roam->menu_open = false;
    }
    return true;
}

static void Roam__Update(SceneRoam *roam) {
    if (Roam__UpdateMenu(roam)) {
        roam->last_tick = SDL_GetTicks64(); // the world waits
        return;
    }
    const uint64_t now     = SDL_GetTicks64();
    const float    seconds = fminf((float)(now - roam->last_tick) / 1000.0f, 0.1f);
    roam->last_tick        = now;
    const SDL_Rect view    = {0, 0, VIEW_W, VIEW_H};

    // Controls: the class's own (or all classes'), the actions of this mode; nothing while typing a seed
    Controls_SetProfile(Roam__Profile(roam));
    Controls_SetContext(roam->game ? CONTROL_CONTEXT_GAME : CONTROL_CONTEXT_DEBUG);
    Controls_AllowAnywhere(ACTION_COLLISION, Settings_GetDebug()->collision_in_game); // Options › Gameplay › Debug
    if (TextBox_IsFocused(roam->seed_box)) {
        InputManager_ClearPresses();
    }
    Controls_Update();
    const GameplaySettings *gameplay = Roam__Gameplay(roam);

    int  mouse_x;
    int  mouse_y;
    bool left  = Controls_Held(ACTION_MOVE_ATTACK);
    bool right = Controls_Held(ACTION_RIGHT_SKILL);
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    const bool over_panel = (roam->panel != PANEL_NONE && mouse_x >= PANEL_X) ||
                            (roam->game && roam->char_panel && mouse_x < CHAR_PANEL_W && mouse_y < CHAR_PANEL_H);
    const bool over_view = mouse_y < VIEW_H && !over_panel;

    if (roam->game && Roam__UpdateGame(roam, now, mouse_x, mouse_y, left && !roam->prev_left)) {
        return;
    }

    if (over_view && (Controls_Pressed(ACTION_ZOOM_IN) || Controls_Pressed(ACTION_ZOOM_OUT))) {
        roam->camera.zoom = Controls_Pressed(ACTION_ZOOM_IN) ? fminf(roam->camera.zoom * 2.0f, 2.0f)
                                                             : fmaxf(roam->camera.zoom / 2.0f, 0.25f);
    }
    InputManager_ConsumeMouseWheel(); // the wheel is bound through the controls
    if (Controls_Pressed(ACTION_RUN_TOGGLE)) {
        roam->running = !roam->running;
        Roam__UpdateButtons(roam);
    }
    if (Controls_Pressed(ACTION_COMBAT)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (Controls_Pressed(ACTION_COLLISION)) {
        roam->draw_flags ^= MAP_DRAW_BLOCKED;
    }
    if (Controls_Pressed(ACTION_LIGHT)) {
        light_level = (light_level + 1) % LIGHT_LEVEL_COUNT;
    }
    if (Controls_Pressed(ACTION_PREVIEW)) {
        roam->preview = !roam->preview;
        Roam__UpdateButtons(roam);
    }
    if (roam->free_roam && Controls_Pressed(ACTION_AUTOMAP)) {
        roam->show_automap = !roam->show_automap; // the automap, like the game's
    }
    if (Controls_Pressed(ACTION_SCREENSHOT)) {
        roam->screenshot = true;
    }
    if (Controls_Pressed(ACTION_CLEAR_SCREEN)) {
        roam->panel        = PANEL_NONE;
        roam->char_panel   = false;
        roam->show_automap = false;
        Roam__UpdateButtons(roam);
    }
    if (!roam->game && roam->free_roam && Controls_Pressed(ACTION_WAYPOINT)) {
        Roam__TogglePanel(roam, PANEL_PLACES);
    }
    if (roam->free_roam && Controls_Pressed(ACTION_NEW_WORLD)) {
        Roam__NewWorld(roam, 0); // the same area in a new random world
        return;
    }

    // Modern / Combined: the movement keys (and the left stick) walk
    roam->key_dx = 0.0f;
    roam->key_dy = 0.0f;
    if (gameplay->movement != MOVEMENT_CLASSIC) {
        // On screen, up is towards lower x and y in sub-tiles, right towards higher x and lower y
        float sx = (Controls_Held(ACTION_MOVE_RIGHT) ? 1.0f : 0.0f) - (Controls_Held(ACTION_MOVE_LEFT) ? 1.0f : 0.0f);
        float sy = (Controls_Held(ACTION_MOVE_DOWN) ? 1.0f : 0.0f) - (Controls_Held(ACTION_MOVE_UP) ? 1.0f : 0.0f);
        const float stick_x = InputManager_GetPadAxis(SDL_CONTROLLER_AXIS_LEFTX);
        const float stick_y = InputManager_GetPadAxis(SDL_CONTROLLER_AXIS_LEFTY);
        if (fabsf(stick_x) > 0.25f || fabsf(stick_y) > 0.25f) {
            sx = stick_x;
            sy = stick_y;
        }
        // Screen directions to sub-tiles (the screen is squashed 2:1, so a step right is half a step in y)
        const float dx     = sx + (sy * 2.0f);
        const float dy     = (sy * 2.0f) - sx;
        const float length = sqrtf((dx * dx) + (dy * dy));
        if (length > 0.01f) {
            roam->key_dx = dx / length;
            roam->key_dy = dy / length;
        }
    }

    float world_x;
    float world_y;
    float sub_x;
    float sub_y;
    MapView_ScreenToWorld(&roam->camera, &view, mouse_x, mouse_y, &world_x, &world_y);
    MapView_WorldToSubtile(world_x, world_y, &sub_x, &sub_y);
    roam->hovered     = over_view ? Roam__UnitAt(roam, sub_x, sub_y) : -1;
    roam->hover_valid = over_view;
    roam->hover_x     = floorf(sub_x) + 0.5f;
    roam->hover_y     = floorf(sub_y) + 0.5f;

    // Modern / Combined: the attack key hits what's under the mouse, or the nearest monster in reach
    if (gameplay->movement != MOVEMENT_CLASSIC && Controls_Pressed(ACTION_ATTACK) && Roam__IsAlive(&roam->player)) {
        Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
    }
    // Modern: the mouse attacks and opens things within reach, but doesn't walk
    if (gameplay->movement == MOVEMENT_MODERN && over_view && left && !roam->prev_left &&
        Roam__IsAlive(&roam->player)) {
        if (roam->hovered >= 0 && Roam__CanUse(&roam->units[roam->hovered])) {
            roam->use_target = roam->hovered;
            if (!Roam__TryUse(roam, now)) {
                roam->use_target = -1; // out of reach
            }
        } else {
            Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
        }
    }
    // Stand still (Shift): clicks attack where you stand instead of walking
    const bool stand_still = Controls_Held(ACTION_STAND_STILL);
    if (gameplay->movement != MOVEMENT_MODERN && stand_still && over_view && left && !roam->prev_left &&
        Roam__IsAlive(&roam->player)) {
        Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
    }

    // Left: attack what's under the mouse (combat on), otherwise walk there (hold to keep following the mouse)
    if (gameplay->movement != MOVEMENT_MODERN && !stand_still && over_view && left && Roam__IsAlive(&roam->player)) {
        if (!roam->prev_left && roam->hovered >= 0 && Roam__CanUse(&roam->units[roam->hovered])) {
            roam->use_target    = roam->hovered; // walk over and open it
            roam->attack_target = -1;
            Roam__ClearPath(&roam->player);
        } else if (!roam->prev_left && Roam__Combat(roam) && roam->hovered >= 0 &&
                   Roam__IsCombatant(&roam->units[roam->hovered]) &&
                   !Roam__IsPeaceful(roam, &roam->units[roam->hovered])) {
            roam->attack_target   = roam->hovered;
            roam->attack_in_place = false;
            roam->use_target      = -1;
        } else if (roam->use_target >= 0 && roam->prev_left) {
            // holding the button after clicking an object: keep going to it
        } else if (roam->attack_target < 0 || roam->prev_left == false) {
            roam->use_target      = -1;
            roam->attack_target   = -1;
            roam->player.target_x = sub_x;
            roam->player.target_y = sub_y;
            roam->moving          = true;
            if (!roam->prev_left) {
                Roam__ClearPath(&roam->player); // a new click: plan from scratch
            }
        }
    }
    // Right: drop the selected monster; Shift+Right removes the nearest one
    if (over_view && right && !roam->prev_right && !roam->game) {
        if ((InputManager_GetMods() & KMOD_SHIFT) != 0) {
            if (roam->hovered >= 0) {
                roam->units[roam->hovered] = roam->units[--roam->unit_count];
                roam->attack_target        = -1;
                roam->use_target           = -1;
                roam->hovered              = -1;
                Roam__UpdateBlocking(roam);
            }
        } else if (roam->panel != PANEL_PLACES) {
            CatalogCharacterInfo info;
            if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), roam->selected, &info)) {
                Unit *unit = Roam__AddUnit(roam, info.spec->base, info.id, roam->hover_x, roam->hover_y);
                if (unit != NULL) {
                    unit->dropped = true;
                    Roam__UpdateBlocking(roam);
                }
            }
        }
    }
    roam->prev_left  = left;
    roam->prev_right = right;

    Roam__UpdatePlayer(roam, seconds, now);
    Roam__UpdateUnits(roam, seconds, now);

    // Which area of a joined-up map you're in
    if (roam->free_roam && roam->map->area_count > 0) {
        const MapArea *area = Map_AreaAt(roam->map, roam->player.x, roam->player.y);
        if (area != NULL) {
            Roam__EnteredArea(roam, area->level_id, true);
        }
    }

    // Walking off the edge into the next area, or onto an entrance
    if (roam->free_roam && Roam__IsAlive(&roam->player)) {
        const MapExit *exit = Map_ExitAt(roam->map, roam->player.x, roam->player.y);
        // Entrances only take you in when you were heading for them, not when you walk across one
        const bool meant = exit != NULL && (!exit->entrance || (roam->player.target_x >= (float)exit->x0 - 3.0f &&
                                                                roam->player.target_x <= (float)exit->x1 + 3.0f &&
                                                                roam->player.target_y >= (float)exit->y0 - 3.0f &&
                                                                roam->player.target_y <= (float)exit->y1 + 3.0f));
        if (exit == NULL) {
            roam->exits_armed = true;
        } else if (roam->exits_armed && meant) {
            roam->moving = false;
            Roam__ClearPath(&roam->player);
            Roam__Travel(roam, exit->target_level);
            return;
        }
    }

    // Camera follows the player (keeping the body, not the feet, in the middle)
    MapView_SubtileToWorld(roam->player.x, roam->player.y, &roam->camera.x, &roam->camera.y);
    roam->camera.y -= 40.0f;

    // Side panel
    if (roam->panel == PANEL_MONSTERS &&
        CatalogCharacters_UpdateCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W)) {
        Roam__RefreshList(roam);
    }
    if (roam->panel == PANEL_MONSTERS && Button_Update(roam->preview_button)) {
        roam->preview = !roam->preview;
        Roam__UpdateButtons(roam);
    }
    if (roam->panel == PANEL_PLACES && roam->seed_box != NULL) {
        TextBox_Update(roam->seed_box);
        const bool enter = TextBox_IsFocused(roam->seed_box) && InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN);
        if (Button_Update(roam->seed_use_button) || enter) {
            const unsigned long typed = strtoul(TextBox_GetText(roam->seed_box), NULL, 10);
            TextBox_Blur(roam->seed_box);
            if (typed > 0 && typed <= 0xFFFFFFFFul) {
                Roam__NewWorld(roam, (uint32_t)typed);
                return;
            }
            Roam__ShowMessage(roam, "A seed is a whole number, 1 or more.");
        }
        if (Button_Update(roam->seed_random_button)) {
            TextBox_Blur(roam->seed_box);
            TextBox_Clear(roam->seed_box);
            Roam__NewWorld(roam, 0);
            return;
        }
    }
    if (roam->panel != PANEL_NONE && ListBox_Update(roam->list)) {
        roam->selected = ListBox_GetSelectedIndex(roam->list);
        if (roam->panel == PANEL_PLACES && roam->selected != roam->place) {
            Roam__Teleport(roam, roam->selected);
        }
    }

    if (roam->class_button != NULL && Button_Update(roam->class_button)) {
        roam->player_class = (roam->player_class + 1) % PLAYER_CLASS_COUNT;
        const float x      = roam->player.x;
        const float y      = roam->player.y;
        Roam__ResetPlayer(roam); // new character, same spot
        roam->player.x = x;
        roam->player.y = y;
        Roam__UpdateButtons(roam);
    }
    if (Button_Update(roam->run_button)) {
        roam->running = !roam->running;
        Roam__UpdateButtons(roam);
    }
    if (roam->monsters_button != NULL && Button_Update(roam->monsters_button)) {
        Roam__TogglePanel(roam, PANEL_MONSTERS);
    }
    if (!roam->game && roam->places_button != NULL && Button_Update(roam->places_button)) {
        Roam__TogglePanel(roam, PANEL_PLACES);
    }
    if (roam->clear_button != NULL && Button_Update(roam->clear_button)) {
        int kept = 0;
        for (int i = 0; i < roam->unit_count; i++) {
            if (!roam->units[i].dropped) {
                roam->units[kept++] = roam->units[i];
            }
        }
        roam->unit_count    = kept;
        roam->attack_target = -1;
        roam->use_target    = -1;
        Roam__UpdateBlocking(roam);
    }
    if (roam->combat_button != NULL && Button_Update(roam->combat_button)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (!roam->game && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        roam->menu_open = true;
    }
    if (!roam->game && Button_Update(roam->back_button)) {
        // Play-testing goes back to the editor (which takes its map back), otherwise to the main menu: asked first
        Roam__AskLeave(roam);
    }
}

void TestingGrounds_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

void Game_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

void FreeRoam_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

// ---------------------------------------------------------------------------------------------------------------------
// Render

static void Roam__DrawFigure(void *context, const int screen_x, const int screen_y, const float zoom) {
    const DrawContext *draw = context;
    Figure_Draw(draw->figure, draw->direction, draw->frame, screen_x, screen_y, zoom);
}

static bool Roam__PrepareDraw(SceneRoam *roam, Unit *unit, MapDrawable *drawable) {
    if (unit->state == UNIT_DEAD && unit->vanished) {
        return false; // no corpse graphics: it's gone
    }
    Figure *figure = Roam__Figure(roam, unit);
    if (figure == NULL) {
        return false;
    }
    int       first      = 0;
    const int frames     = Roam__FrameRange(unit, figure, &first);
    unit->draw.figure    = figure;
    unit->draw.direction = MapUnits_Direction(figure, unit->turn);
    unit->draw.frame     = frames > 0 ? first + (unit->frame % frames) : 0;
    if (unit->state == UNIT_DEAD) {
        unit->draw.frame = frames > 0 ? first + frames - 1 : 0;
    }
    if (unit->is_object && unit->rest_frame >= 0 && unit->state == UNIT_IDLE) {
        unit->draw.frame = unit->rest_frame;
    }
    drawable->x       = unit->x;
    drawable->y       = unit->y;
    drawable->draw    = Roam__DrawFigure;
    drawable->context = &unit->draw;
    return true;
}

// Name and life bar over a unit
static void Roam__DrawUnitInfo(SceneRoam *roam, const SDL_Rect *view, const Unit *unit, const bool show_bar) {
    float wx;
    float wy;
    MapView_SubtileToWorld(unit->x, unit->y, &wx, &wy);
    int sx;
    int sy;
    MapView_WorldToScreen(&roam->camera, view, wx, wy, &sx, &sy);
    int top = sy - (int)(90.0f * roam->camera.zoom);
    if (unit->draw.figure != NULL) {
        int xmin;
        int ymin;
        int xmax;
        int ymax;
        Figure_GetBounds(unit->draw.figure, &xmin, &ymin, &xmax, &ymax);
        top = sy + (int)((float)ymin * roam->camera.zoom) - 4;
    }

    Label_SetText(roam->target_label, unit->name);
    Label_SetColor(roam->target_label, unit->npc ? 0xC0 : 0xFF, unit->npc ? 0xC0 : 0xE8, unit->npc ? 0xFF : 0xA0, 0xFF);
    Label_Draw(roam->target_label, sx, top - (show_bar ? 8 : 0));
    if (show_bar && unit->max_hp > 0) {
        const SDL_Rect back = {sx - 25, top - 6, 50, 5};
        const SDL_Rect fill = {sx - 25, top - 6, 50 * unit->hp / unit->max_hp, 5};
        SDL_SetRenderDrawColor(sdl_renderer, 40, 0, 0, 220);
        SDL_RenderFillRect(sdl_renderer, &back);
        SDL_SetRenderDrawColor(sdl_renderer, 200, 20, 20, 255);
        SDL_RenderFillRect(sdl_renderer, &fill);
    }
}

static void Roam__DrawPreview(SceneRoam *roam, const SDL_Rect *view) {
    CatalogCharacterInfo info;
    if (!CatalogCharacters_GetInfo(MapSession_GetBestiary(), roam->selected, &info)) {
        return;
    }
    float wx;
    float wy;
    MapView_SubtileToWorld(roam->hover_x, roam->hover_y, &wx, &wy);
    int sx;
    int sy;
    MapView_WorldToScreen(&roam->camera, view, wx, wy, &sx, &sy);

    // The sub-tile it lands on: green if it can stand there, red if not
    const bool walkable   = Map_IsWalkable(roam->map, (int)floorf(roam->hover_x), (int)floorf(roam->hover_y));
    const int  hw         = (int)(16.0f * roam->camera.zoom);
    const int  hh         = (int)(8.0f * roam->camera.zoom);
    SDL_Point  diamond[5] = {
        {sx,      sy - hh},
        {sx + hw, sy     },
        {sx,      sy + hh},
        {sx - hw, sy     },
        {sx,      sy - hh}
    };
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, walkable ? 80 : 255, walkable ? 255 : 60, 80, 220);
    SDL_RenderDrawLines(sdl_renderer, diamond, 5);

    Figure *figure = MapUnits_GetFigure(roam->units_cache, info.spec->base, info.id, info.idle_mode);
    if (figure != NULL) {
        int turn = 0;
        Roam__TurnTowards(&turn, roam->hover_x, roam->hover_y, roam->player.x, roam->player.y);
        const int frames = Figure_GetFramesPerDirection(figure);
        const int ms     = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
        const int frame  = frames > 0 ? (int)((SDL_GetTicks64() / (uint64_t)ms) % (uint64_t)frames) : 0;
        Figure_SetAlpha(figure, 140);
        Figure_Draw(figure, MapUnits_Direction(figure, turn), frame, sx, sy, roam->camera.zoom);
        Figure_SetAlpha(figure, 255);
    }

    Label_SetText(roam->target_label, info.name);
    Label_SetColor(roam->target_label, 0xA0, 0xFF, 0xA0, 0xFF);
    Label_Draw(roam->target_label, sx, sy + hh + 18);
}

// Single Player: mana and stamina next to life, the experience bar, messages
static void Roam__DrawGameHud(SceneRoam *roam) {
    const Character *character   = roam->character;
    const int        max_mana    = Character_MaxMana(character);
    const int        max_stamina = Character_MaxStamina(character);
    char             text[64];

    const SDL_Rect mana      = {VIEW_W - 128, VIEW_H - 34, 120, 10};
    const SDL_Rect mana_fill = {VIEW_W - 128, VIEW_H - 34,
                                max_mana > 0 ? (int)(120.0f * roam->mana / (float)max_mana) : 0, 10};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 40, 220);
    SDL_RenderFillRect(sdl_renderer, &mana);
    SDL_SetRenderDrawColor(sdl_renderer, 30, 60, 220, 255);
    SDL_RenderFillRect(sdl_renderer, &mana_fill);
    snprintf(text, sizeof(text), "Mana %d / %d", (int)roam->mana, max_mana);
    Label_SetText(roam->life_label, text);
    int width = 0;
    Label_GetSize(roam->life_label, &width, NULL);
    Label_Draw(roam->life_label, VIEW_W - 134 - width, VIEW_H - 37);

    // Stamina: a thin bar under life, yellow (red when you've run yourself out)
    const SDL_Rect stamina      = {8, VIEW_H - 22, 120, 4};
    const SDL_Rect stamina_fill = {8, VIEW_H - 22,
                                   max_stamina > 0 ? (int)(120.0f * roam->stamina / (float)max_stamina) : 0, 4};
    SDL_SetRenderDrawColor(sdl_renderer, 30, 30, 0, 220);
    SDL_RenderFillRect(sdl_renderer, &stamina);
    if (roam->exhausted) {
        SDL_SetRenderDrawColor(sdl_renderer, 200, 60, 20, 255);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 210, 190, 40, 255);
    }
    SDL_RenderFillRect(sdl_renderer, &stamina_fill);

    // Experience through this level, across the bottom of the view
    const uint32_t low     = Character_ExperienceFor(character->level);
    const uint32_t high    = Character_ExperienceFor(character->level + 1);
    const float    done    = (character->level >= CHARACTER_MAX_LEVEL || high <= low)
                                 ? 1.0f
                                 : (float)(character->experience - low) / (float)(high - low);
    const SDL_Rect xp      = {0, VIEW_H - 16, VIEW_W, 2};
    const SDL_Rect xp_fill = {0, VIEW_H - 16, (int)((float)VIEW_W * fminf(fmaxf(done, 0.0f), 1.0f)), 2};
    SDL_SetRenderDrawColor(sdl_renderer, 40, 34, 10, 255);
    SDL_RenderFillRect(sdl_renderer, &xp);
    SDL_SetRenderDrawColor(sdl_renderer, 230, 200, 80, 255);
    SDL_RenderFillRect(sdl_renderer, &xp_fill);

    if (character->stat_points > 0 && !roam->char_panel && (SDL_GetTicks64() / 600) % 2 == 0) {
        Label_SetText(roam->life_label, "New stats! (C)");
        Label_SetColor(roam->life_label, 0xFF, 0xE8, 0x60, 0xFF);
        Label_Draw(roam->life_label, 340, VIEW_H - 37);
        Label_SetColor(roam->life_label, 0xFF, 0xFF, 0xFF, 0xFF);
    }
}

// Collision view (B): where each unit stands (the sub-tile clicks and fights go by); purple = monsters and the
// player, light blue = town folk
static void Roam__DrawUnitFootprints(SceneRoam *roam, const SDL_Rect *view) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int i = -1; i < roam->unit_count; i++) {
        const Unit *unit = i < 0 ? &roam->player : &roam->units[i];
        if (unit->is_object || !Roam__IsAlive(unit)) {
            continue;
        }
        const float x       = floorf(unit->x);
        const float y       = floorf(unit->y);
        float       c[4][2] = {
            {x,        y       },
            {x + 1.0f, y       },
            {x + 1.0f, y + 1.0f},
            {x,        y + 1.0f}
        };
        SDL_Point points[5];
        for (int k = 0; k < 4; k++) {
            float wx;
            float wy;
            MapView_SubtileToWorld(c[k][0], c[k][1], &wx, &wy);
            MapView_WorldToScreen(&roam->camera, view, wx, wy, &points[k].x, &points[k].y);
        }
        points[4] = points[0];
        if (unit->npc) {
            SDL_SetRenderDrawColor(sdl_renderer, 140, 220, 255, 255);
        } else {
            SDL_SetRenderDrawColor(sdl_renderer, 230, 80, 255, 255);
        }
        SDL_RenderDrawLines(sdl_renderer, points, 5);
    }
}

// What the collision view's colours mean
static void Roam__DrawCollisionLegend(SceneRoam *roam) {
    static const struct {
        uint8_t     r, g, b;
        const char *text;
    } entries[] = {
        {255, 40,  40,  "Walls, cliffs, water (the map)"},
        {60,  140, 255, "Objects (chests, doors, ...)"  },
        {60,  230, 90,  "Way to the next area"          },
        {255, 220, 40,  "Entrance (click to go in)"     },
        {230, 80,  255, "Monsters and you"              },
        {140, 220, 255, "Town folk"                     },
    };
    const int      count = (int)(sizeof(entries) / sizeof(entries[0]));
    const SDL_Rect box   = {4, 18, 210, (count * 13) + 6};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &box);
    for (int i = 0; i < count; i++) {
        const SDL_Rect swatch = {box.x + 5, box.y + 5 + (i * 13), 9, 9};
        SDL_SetRenderDrawColor(sdl_renderer, entries[i].r, entries[i].g, entries[i].b, 255);
        SDL_RenderFillRect(sdl_renderer, &swatch);
        Label_SetText(roam->status_label, entries[i].text);
        Label_Draw(roam->status_label, box.x + 19, swatch.y - 1);
    }
}

static void Roam__Render(SceneRoam *roam) {
    const SDL_Rect view = {0, 0, VIEW_W, VIEW_H};

    SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 14, 255);
    SDL_RenderClear(sdl_renderer);

    MapDrawable drawables[MAX_UNITS + 1];
    int         count = 0;
    for (int i = 0; i < roam->unit_count; i++) {
        if (Roam__PrepareDraw(roam, &roam->units[i], &drawables[count])) {
            count++;
        }
    }
    if (Roam__PrepareDraw(roam, &roam->player, &drawables[count])) {
        count++;
    }
    MapRenderer_SetViewer(roam->renderer, roam->player.x, roam->player.y); // roofs fade over the player
    MapRenderer_SetSeeThrough(roam->renderer, Roam__Gameplay(roam)->see_through);
    MapRenderer_Draw(roam->renderer, roam->map, &roam->camera, &view, drawables, count, roam->draw_flags);
    if (roam->draw_flags & MAP_DRAW_BLOCKED) {
        Roam__DrawUnitFootprints(roam, &view);
    }
    if (roam->free_roam) {
        Automap_Reveal(roam->automap, roam->player.x, roam->player.y);
        if (roam->show_automap) {
            Automap_Draw(roam->automap, &view, roam->player.x, roam->player.y);
        }
    }
    if (light_levels[light_level].darkness > 0) {
        float light_x;
        float light_y;
        MapView_SubtileToWorld(roam->player.x, roam->player.y, &light_x, &light_y);
        MapRenderer_DrawLight(roam->renderer, &roam->camera, &view, light_x, light_y - 30.0f,
                              light_levels[light_level].radius, light_levels[light_level].darkness);
    }

    // Walk target marker
    if (roam->moving && roam->attack_target < 0) {
        float wx;
        float wy;
        MapView_SubtileToWorld(roam->player.target_x, roam->player.target_y, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(&roam->camera, &view, wx, wy, &sx, &sy);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 120, 200);
        SDL_RenderDrawLine(sdl_renderer, sx - 6, sy, sx + 6, sy);
        SDL_RenderDrawLine(sdl_renderer, sx, sy - 3, sx, sy + 3);
    }

    // Placement preview: the selected monster, see-through, where right click would drop it
    if (roam->panel == PANEL_MONSTERS && roam->preview && roam->hover_valid) {
        Roam__DrawPreview(roam, &view);
    }

    // Who's under the mouse / being attacked
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    if (roam->attack_target >= 0 && roam->attack_target != roam->hovered) {
        Roam__DrawUnitInfo(roam, &view, &roam->units[roam->attack_target], true);
    }
    if (roam->hovered >= 0) {
        Roam__DrawUnitInfo(roam, &view, &roam->units[roam->hovered],
                           Roam__Combat(roam) && !Roam__IsPeaceful(roam, &roam->units[roam->hovered]));
    }

    // Status bar
    const SDL_Rect bar = {0, 0, VIEW_W, 14};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &bar);
    int npcs  = 0;
    int alive = 0;
    for (int i = 0; i < roam->unit_count; i++) {
        npcs  += roam->units[i].npc ? 1 : 0;
        alive += Roam__IsAlive(&roam->units[i]) ? 1 : 0;
    }
    char status[200];
    if (roam->game) {
        snprintf(status, sizeof(status), "%s   %s, level %d %s%s   light: %s", Roam__AreaName(roam),
                 roam->character->name, roam->character->level, Character_ClassName(roam->character->class_id),
                 roam->character->hardcore ? " (hardcore)" : "", light_levels[light_level].name);
    } else {
        char seed[32] = "";
        if (roam->free_roam) {
            snprintf(seed, sizeof(seed), "   seed %u", World_GetSeed());
        }
        snprintf(status, sizeof(status), "%s%s   %s   %d units (%d NPCs, %d alive)   combat %s   light: %s%s",
                 Roam__AreaName(roam), roam->borrowed ? " (play-test)" : "", player_classes[roam->player_class][0],
                 roam->unit_count, npcs, alive, combat_enabled ? "ON" : "off", light_levels[light_level].name, seed);
    }
    Label_SetText(roam->status_label, status);
    Label_Draw(roam->status_label, 4, 2);
    if (roam->draw_flags & MAP_DRAW_BLOCKED) {
        Roam__DrawCollisionLegend(roam);
    }
    const SDL_Rect help_bar = {0, VIEW_H - 14, VIEW_W, 14};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &help_bar);
    Label_Draw(roam->help_label, 4, VIEW_H - 12);

    // Life
    const Unit    *player = &roam->player;
    const SDL_Rect life   = {8, VIEW_H - 34, 120, 10};
    const SDL_Rect fill   = {8, VIEW_H - 34, player->max_hp > 0 ? 120 * player->hp / player->max_hp : 0, 10};
    SDL_SetRenderDrawColor(sdl_renderer, 40, 0, 0, 220);
    SDL_RenderFillRect(sdl_renderer, &life);
    SDL_SetRenderDrawColor(sdl_renderer, 200, 20, 20, 255);
    SDL_RenderFillRect(sdl_renderer, &fill);
    char life_text[64];
    if (player->state == UNIT_DEAD && roam->game && roam->character->dead) {
        snprintf(life_text, sizeof(life_text), "You died");
    } else if (player->state == UNIT_DEAD) {
        const uint64_t now = SDL_GetTicks64();
        snprintf(life_text, sizeof(life_text),
                 roam->game ? "You died - back in town in %d..." : "You died - back in %d...",
                 (int)((roam->respawn_at > now ? roam->respawn_at - now + 999 : 0) / 1000));
    } else {
        snprintf(life_text, sizeof(life_text), "Life %d / %d", player->hp, player->max_hp);
    }
    Label_SetText(roam->life_label, life_text);
    Label_Draw(roam->life_label, 134, VIEW_H - 37);

    if (roam->game) {
        Roam__DrawGameHud(roam);
    }

    if (roam->panel != PANEL_NONE) {
        const SDL_Rect panel = {PANEL_X, 14, 800 - PANEL_X, VIEW_H - 28};
        SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 235);
        SDL_RenderFillRect(sdl_renderer, &panel);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
        SDL_RenderDrawRect(sdl_renderer, &panel);
        Label_Draw(roam->title_label, PANEL_X + ((800 - PANEL_X) / 2), 30);
        if (roam->panel == PANEL_MONSTERS) {
            CatalogCharacters_DrawCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W);
        }
        ListBox_Draw(roam->list);
        if (roam->panel == PANEL_MONSTERS) {
            Button_Draw(roam->preview_button);
        }
        if (roam->panel == PANEL_PLACES && roam->seed_box != NULL) {
            char text[64];
            snprintf(text, sizeof(text), "World seed: %u%s", World_GetSeed(),
                     fixed_seed != 0 ? " (fixed)" : " (random)");
            Label_SetText(roam->seed_label, text);
            Label_Draw(roam->seed_label, PANEL_INNER_X, SEED_LABEL_Y);
            TextBox_Draw(roam->seed_box);
            Button_Draw(roam->seed_use_button);
            Button_Draw(roam->seed_random_button);
        }
    }

    if (roam->game && roam->char_panel) {
        Roam__DrawCharacterPanel(roam);
    }
    const uint64_t now = SDL_GetTicks64();
    if (now < roam->caption_until) {
        const uint64_t left  = roam->caption_until - now;
        const uint8_t  alpha = left >= 1000 ? 255 : (uint8_t)((left * 255) / 1000);
        Label_SetColor(roam->caption_label, 0xFF, 0x58, 0x48, alpha); // the game's red
        Label_Draw(roam->caption_label, VIEW_W / 2, 80);
    }
    if (now < roam->message_until) {
        Label_Draw(roam->message_label, VIEW_W / 2, 125);
    }

    const SDL_Rect bottom = {0, VIEW_H, 800, 600 - VIEW_H};
    SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
    SDL_RenderFillRect(sdl_renderer, &bottom);
    for (int i = 0; i < 6; i++) {
        Button_Draw(roam->buttons[i]);
    }
    if (roam->options_panel != NULL) {
        // The game stays dimly behind it, without the bars and labels showing through
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 248);
        SDL_RenderFillRect(sdl_renderer, NULL);
        OptionsPanel_Draw(roam->options_panel);
    } else if (roam->menu_open && !ConfirmDialog_IsOpen(roam->confirm)) {
        const SDL_Rect screen = {0, 0, 800, 600};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(sdl_renderer, &screen);
        Button_Draw(roam->menu_options_button);
        Button_Draw(roam->menu_leave_button);
        Button_Draw(roam->menu_return_button);
    }
    ConfirmDialog_Draw(roam->confirm);
    if (roam->screenshot) {
        roam->screenshot = false;
        char path[4300];
        if (Screenshot_Save(path, sizeof(path))) {
            const char *name = strrchr(path, '/');
            char        text[160];
            snprintf(text, sizeof(text), "Screenshot saved: %.120s", name != NULL ? name + 1 : path);
            Roam__ShowMessage(roam, text);
        }
    }
}

void TestingGrounds_Render(void *scene_ref) { Roam__Render(scene_ref); }

void FreeRoam_Render(void *scene_ref) { Roam__Render(scene_ref); }

void Game_Render(void *scene_ref) { Roam__Render(scene_ref); }
