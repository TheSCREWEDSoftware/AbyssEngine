#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/LoadingScreen.h"
#include "../managers/InputManager.h"
#include "../map/LevelTypes.h"
#include "../map/Map.h"
#include "../map/MapRenderer.h"
#include "../map/MapSession.h"
#include "../map/MapUnits.h"
#include "../map/MonsterStats.h"
#include "../map/Pathfinding.h"
#include "../map/Places.h"
#include "CatalogCharacters.h"
#include "SceneFreeRoam.h"
#include "SceneMainMenu.h"
#include "SceneMapEditor.h"
#include "SceneTestingGrounds.h"
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
#define MAX_UNITS        512
#define WALK_SPEED       5.0f // sub-tiles per second
#define RUN_SPEED        9.0f
#define ARRIVE_DISTANCE  0.15f

// Combat
#define AGGRO_RANGE       10.0f // sub-tiles
#define ATTACK_RANGE      1.6f
#define ATTACK_COOLDOWN   900 // ms between a monster's attacks
#define RESPAWN_DELAY     3000
#define RESPAWN_GRACE     3000 // after coming back, monsters leave you alone for a moment
#define PLAYER_DAMAGE_MIN 6
#define PLAYER_DAMAGE_MAX 14
#define REPATH_PLAYER_MS  250 // while the mouse is held / the target moves
#define REPATH_MONSTER_MS 500
#define REPATH_DISTANCE   1.0f // re-plan when the goal moved this far
#define HOVER_RANGE       2.5f

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

static bool combat_enabled = false; // kept between visits

typedef enum UnitState { UNIT_IDLE, UNIT_WALK, UNIT_ATTACK, UNIT_HIT, UNIT_DYING, UNIT_DEAD } UnitState;

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
    float       speed;
    bool        npc;
    bool        dropped;  // added in the Testing Grounds (Clear removes these)
    bool        provoked; // hit by the player: fights back even far away
    uint64_t    next_attack;
    MapPath     path;        // the way around walls to where it's going
    float       path_goal_x; // where the path was planned to
    float       path_goal_y;
    uint64_t    next_repath;
    DrawContext draw;
} Unit;

typedef enum RoamPanel { PANEL_NONE, PANEL_MONSTERS, PANEL_PLACES } RoamPanel;

typedef struct SceneRoam {
    bool         free_roam;
    Map         *map;
    bool         borrowed; // the editor's map (play-test): not ours to free
    MapRenderer *renderer;
    MapUnits    *units_cache; // figures
    MapCamera    camera;
    uint64_t     last_tick;
    unsigned     draw_flags;
    int          place; // Free Roam: current Places index

    // Player
    Unit     player;
    int      player_class;
    bool     running;
    bool     moving;
    int      attack_target; // index into units, -1 = none
    float    spawn_x;
    float    spawn_y;
    uint64_t respawn_at;
    uint64_t safe_until; // grace period after respawning

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
    Button   *preview_button; // in the Monsters panel
    bool      preview;        // show what right click would place, under the mouse
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
} SceneRoam;

DEFINE_SCENE_CALLBACKS(TestingGrounds);
DEFINE_SCENE_CALLBACKS(FreeRoam);

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

static const char *Roam__Mode(const SceneRoam *roam, const Unit *unit) {
    const bool player = Roam__IsPlayer(roam, unit);
    switch (unit->state) {
    case UNIT_WALK:
        if (player) {
            return roam->running ? "rn" : (combat_enabled ? "wl" : "tw");
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
    case UNIT_IDLE:
        break;
    }
    if (player) {
        return combat_enabled ? "nu" : "tn";
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
        return unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING;
    }
    const int  frames   = Figure_GetFramesPerDirection(figure);
    const int  ms       = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
    const bool one_shot = unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING;
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
    unit->idle_mode = info.idle_mode;
    unit->x         = x;
    unit->y         = y;
    unit->target_x  = x;
    unit->target_y  = y;
    unit->turn      = rand() % 16;
    unit->state     = UNIT_IDLE;

    MonsterStats stats;
    if (strcmp(base, "monsters") == 0 && MonsterStats_Get(id, &stats)) {
        unit->max_hp     = Roam__Roll(stats.hp_min, stats.hp_max > stats.hp_min ? stats.hp_max : stats.hp_min);
        unit->max_hp     = unit->max_hp > 0 ? unit->max_hp : 20;
        unit->damage_min = stats.damage_min > 0 ? stats.damage_min : 1;
        unit->damage_max = stats.damage_max > unit->damage_min ? stats.damage_max : unit->damage_min + 2;
        unit->speed      = (float)(stats.velocity > 0 ? stats.velocity : 3);
        unit->npc        = stats.npc;
    } else if (strcmp(base, "chars") == 0) {
        unit->max_hp     = 50;
        unit->damage_min = 2;
        unit->damage_max = 6;
        unit->speed      = WALK_SPEED;
    }
    unit->speed = unit->speed < 2.0f ? 2.0f : (unit->speed > 9.0f ? 9.0f : unit->speed);
    unit->hp    = unit->max_hp;
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

static void Roam__Damage(Unit *unit, const int amount, const uint64_t now) {
    if (!Roam__IsAlive(unit)) {
        return;
    }
    unit->hp -= amount;
    if (unit->hp <= 0) {
        unit->hp = 0;
        Roam__SetState(unit, UNIT_DYING, now);
    } else if (unit->state == UNIT_IDLE || unit->state == UNIT_WALK) {
        Roam__SetState(unit, UNIT_HIT, now);
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
    roam->moving        = false;
    roam->attack_target = -1;
}

// Start on the walkable sub-tile closest to the middle of the map
static void Roam__FindSpawn(SceneRoam *roam) {
    const int cx  = (roam->map->width * MAP_SUBTILES) / 2;
    const int cy  = (roam->map->height * MAP_SUBTILES) / 2;
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
        }
    }
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
    Button_SetText(roam->class_button, player_classes[roam->player_class][0]);
    Button_SetText(roam->run_button, roam->running ? "Run" : "Walk");
    Button_SetText(roam->monsters_button, roam->panel == PANEL_MONSTERS ? "Hide List" : "Monsters");
    if (roam->places_button != NULL) {
        Button_SetText(roam->places_button, roam->panel == PANEL_PLACES ? "Hide List" : "Places");
    }
    Button_SetText(roam->combat_button, combat_enabled ? "Combat On" : "Combat Off");
    Button_SetText(roam->preview_button, roam->preview ? "Preview On" : "Preview Off");
    Button_SetToggled(roam->preview_button, roam->preview);
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
        Label_SetText(roam->title_label, "Places");
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
static void Roam__Teleport(SceneRoam *roam, const int place_index) {
    const Place *place = Places_Get(place_index);
    if (place == NULL) {
        return;
    }
    char text[96];
    snprintf(text, sizeof(text), "Loading %s...", place->name);
    LoadingScreen_Begin(text);
    Map *map = Map_LoadDS1(place->files[0]);
    if (map == NULL) {
        LoadingScreen_End();
        return;
    }
    snprintf(map->name, sizeof(map->name), "%s", place->name);
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
    }
    roam->map      = map;
    roam->borrowed = false;
    roam->place    = place_index;
    MapRenderer_ClearCache(roam->renderer);
    Roam__Populate(roam);
    LoadingScreen_End();
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

static void *Roam__Create(const bool free_roam) {
    Cursor_SetVisible(true);

    SceneRoam *roam = calloc(1, sizeof(SceneRoam));
    FAIL_IF_NULL(roam);
    roam->free_roam     = free_roam;
    roam->renderer      = MapRenderer_Create();
    roam->units_cache   = MapUnits_Create();
    roam->camera.zoom   = 1.0f;
    roam->last_tick     = SDL_GetTicks64();
    roam->draw_flags    = MAP_DRAW_DEFAULT;
    roam->attack_target = -1;
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
    Label_SetText(roam->help_label, "Left: walk/attack  Right: drop  Shift+Right: remove  R run  C combat  P preview  "
                                    "B blocked  Wheel zoom  Esc back");

    const char *names[6] = {
        "Back", "Class", "Walk", free_roam ? "Places" : "Monsters", free_roam ? "Monsters" : "Clear", "Combat"};
    for (int i = 0; i < 6; i++) {
        roam->buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, names[i]);
        Button_SetPosition(roam->buttons[i], 8 + (i * BUTTON_STEP), BOTTOM_Y);
    }
    roam->back_button   = roam->buttons[0];
    roam->class_button  = roam->buttons[1];
    roam->run_button    = roam->buttons[2];
    roam->combat_button = roam->buttons[5];
    if (free_roam) {
        roam->places_button   = roam->buttons[3];
        roam->monsters_button = roam->buttons[4];
    } else {
        roam->monsters_button = roam->buttons[3];
        roam->clear_button    = roam->buttons[4];
    }

    roam->preview        = true;
    roam->preview_button = Button_Create(BUTTON_TYPE_MEDIUM, "Preview On");
    Button_SetPosition(roam->preview_button, PANEL_INNER_X + 38, PREVIEW_BUTTON_Y);

    roam->list = ListBox_Create(FONTS_FONT8, PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W, LIST_ROWS, LIST_ROW_H);
    ListBox_SetBackground(roam->list, 0, 0, 0, 160);

    if (free_roam) {
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

void *TestingGrounds_Create(void) { return Roam__Create(false); }

void *FreeRoam_Create(void) { return Roam__Create(true); }

static void Roam__Free(void **scene_ref) {
    SceneRoam *roam = *(SceneRoam **)scene_ref;
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
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
    free(roam);
    *scene_ref = NULL;
}

void TestingGrounds_Free(void **scene_ref) { Roam__Free(scene_ref); }

void FreeRoam_Free(void **scene_ref) { Roam__Free(scene_ref); }

// ---------------------------------------------------------------------------------------------------------------------
// Update

static void Roam__UpdatePlayer(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit *player = &roam->player;

    if (player->state == UNIT_DEAD) {
        if (now >= roam->respawn_at) {
            Roam__ResetPlayer(roam); // back at the start with full life
            roam->safe_until = now + RESPAWN_GRACE;
            for (int i = 0; i < roam->unit_count; i++) {
                roam->units[i].provoked = false;
            }
        }
        return;
    }
    if (Roam__Animate(roam, player, now)) {
        if (player->state == UNIT_ATTACK) {
            // The blow lands at the end of the swing
            if (roam->attack_target >= 0) {
                Unit *target = &roam->units[roam->attack_target];
                if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE + 0.5f) {
                    target->provoked = true;
                    Roam__Damage(target, Roam__Roll(player->damage_min, player->damage_max), now);
                }
            }
            Roam__SetState(player, UNIT_IDLE, now);
        } else if (player->state == UNIT_DYING) {
            Roam__SetState(player, UNIT_DEAD, now);
            roam->respawn_at    = now + RESPAWN_DELAY;
            roam->attack_target = -1;
            return;
        } else {
            Roam__SetState(player, UNIT_IDLE, now);
        }
    }
    if (player->state == UNIT_DYING || player->state == UNIT_ATTACK || player->state == UNIT_HIT) {
        return;
    }

    // Attacking: walk up to the target, then swing
    if (roam->attack_target >= 0) {
        Unit *target = &roam->units[roam->attack_target];
        if (!combat_enabled || !Roam__IsAlive(target)) {
            roam->attack_target = -1;
        } else {
            if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE) {
                Roam__TurnTowards(&player->turn, player->x, player->y, target->x, target->y);
                roam->moving = false;
                Roam__ClearPath(player);
                Roam__SetState(player, UNIT_ATTACK, now);
                return;
            }
            player->target_x = target->x;
            player->target_y = target->y;
            roam->moving     = true;
        }
    }

    if (roam->moving) {
        // Around walls, not through them
        Roam__PlanPath(roam->map, player, player->target_x, player->target_y, now, REPATH_PLAYER_MS);
        roam->moving = Roam__FollowPath(roam->map, player, roam->running ? RUN_SPEED : WALK_SPEED, seconds);
        if (!roam->moving) {
            Roam__ClearPath(player);
        }
    }
    Roam__SetState(player, roam->moving ? UNIT_WALK : UNIT_IDLE, now);
}

static void Roam__UpdateUnits(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit      *player       = &roam->player;
    const bool player_alive = Roam__IsAlive(player) && now >= roam->safe_until; // can be fought

    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        if (Roam__Animate(roam, unit, now)) {
            if (unit->state == UNIT_ATTACK && player_alive && combat_enabled &&
                Roam__Distance(unit->x, unit->y, player->x, player->y) <= ATTACK_RANGE + 0.5f) {
                Roam__Damage(player, Roam__Roll(unit->damage_min, unit->damage_max), now);
            }
            Roam__SetState(unit, unit->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
        }
        if (!Roam__IsAlive(unit) || !Roam__IsCombatant(unit) || unit->state == UNIT_ATTACK || unit->state == UNIT_HIT) {
            continue;
        }

        if (!combat_enabled || !player_alive) {
            unit->provoked = false;
            Roam__SetState(unit, UNIT_IDLE, now);
            continue;
        }

        // Combat: everyone nearby (NPCs included) comes for the player
        const float distance = Roam__Distance(unit->x, unit->y, player->x, player->y);
        if (distance > AGGRO_RANGE && !unit->provoked) {
            Roam__SetState(unit, UNIT_IDLE, now);
            continue;
        }
        if (distance <= ATTACK_RANGE) {
            Roam__TurnTowards(&unit->turn, unit->x, unit->y, player->x, player->y);
            Roam__ClearPath(unit);
            if (now >= unit->next_attack) {
                unit->next_attack = now + ATTACK_COOLDOWN;
                Roam__SetState(unit, UNIT_ATTACK, now);
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
        if (!Roam__IsAlive(unit)) {
            continue;
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

static void Roam__Update(SceneRoam *roam) {
    const uint64_t now     = SDL_GetTicks64();
    const float    seconds = fminf((float)(now - roam->last_tick) / 1000.0f, 0.1f);
    roam->last_tick        = now;
    const SDL_Rect view    = {0, 0, VIEW_W, VIEW_H};

    int  mouse_x;
    int  mouse_y;
    bool left;
    bool right;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, &right);
    const bool over_panel = roam->panel != PANEL_NONE && mouse_x >= PANEL_X;
    const bool over_view  = mouse_y < VIEW_H && !over_panel;

    if (over_view) {
        const int wheel = InputManager_ConsumeMouseWheel();
        if (wheel != 0) {
            roam->camera.zoom =
                wheel > 0 ? fminf(roam->camera.zoom * 2.0f, 2.0f) : fmaxf(roam->camera.zoom / 2.0f, 0.25f);
        }
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_R)) {
        roam->running = !roam->running;
        Roam__UpdateButtons(roam);
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_C)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_B)) {
        roam->draw_flags ^= MAP_DRAW_BLOCKED;
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_P)) {
        roam->preview = !roam->preview;
        Roam__UpdateButtons(roam);
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

    // Left: attack what's under the mouse (combat on), otherwise walk there (hold to keep following the mouse)
    if (over_view && left && Roam__IsAlive(&roam->player)) {
        if (!roam->prev_left && combat_enabled && roam->hovered >= 0 &&
            Roam__IsCombatant(&roam->units[roam->hovered])) {
            roam->attack_target = roam->hovered;
        } else if (roam->attack_target < 0 || roam->prev_left == false) {
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
    if (over_view && right && !roam->prev_right) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            if (roam->hovered >= 0) {
                roam->units[roam->hovered] = roam->units[--roam->unit_count];
                roam->attack_target        = -1;
                roam->hovered              = -1;
            }
        } else if (roam->panel != PANEL_PLACES) {
            CatalogCharacterInfo info;
            if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), roam->selected, &info)) {
                Unit *unit = Roam__AddUnit(roam, info.spec->base, info.id, roam->hover_x, roam->hover_y);
                if (unit != NULL) {
                    unit->dropped = true;
                }
            }
        }
    }
    roam->prev_left  = left;
    roam->prev_right = right;

    Roam__UpdatePlayer(roam, seconds, now);
    Roam__UpdateUnits(roam, seconds, now);

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
    if (roam->panel != PANEL_NONE && ListBox_Update(roam->list)) {
        roam->selected = ListBox_GetSelectedIndex(roam->list);
        if (roam->panel == PANEL_PLACES && roam->selected != roam->place) {
            Roam__Teleport(roam, roam->selected);
        }
    }

    if (Button_Update(roam->class_button)) {
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
    if (Button_Update(roam->monsters_button)) {
        Roam__TogglePanel(roam, PANEL_MONSTERS);
    }
    if (roam->places_button != NULL && Button_Update(roam->places_button)) {
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
    }
    if (Button_Update(roam->combat_button)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (Button_Update(roam->back_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        // Play-testing goes back to the editor (which takes its map back), otherwise to the main menu
        Scene_Set(roam->borrowed ? SCENE_REF(MapEditor) : SCENE_REF(MainMenu));
    }
}

void TestingGrounds_Update(void *scene_ref, const uint64_t delta) {
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
    Figure *figure = Roam__Figure(roam, unit);
    if (figure == NULL) {
        return false;
    }
    const int frames     = Figure_GetFramesPerDirection(figure);
    unit->draw.figure    = figure;
    unit->draw.direction = MapUnits_Direction(figure, unit->turn);
    unit->draw.frame     = frames > 0 ? unit->frame % frames : 0;
    if (unit->state == UNIT_DEAD) {
        unit->draw.frame = frames > 0 ? frames - 1 : 0;
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
    MapRenderer_Draw(roam->renderer, roam->map, &roam->camera, &view, drawables, count, roam->draw_flags);

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
        Roam__DrawUnitInfo(roam, &view, &roam->units[roam->hovered], combat_enabled);
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
    snprintf(status, sizeof(status), "%s%s   %s   %d units (%d NPCs, %d alive)   combat %s", roam->map->name,
             roam->borrowed ? " (play-test)" : "", player_classes[roam->player_class][0], roam->unit_count, npcs, alive,
             combat_enabled ? "ON" : "off");
    Label_SetText(roam->status_label, status);
    Label_Draw(roam->status_label, 4, 2);
    const SDL_Rect help_bar = {0, VIEW_H - 14, VIEW_W, 14};
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
    if (player->state == UNIT_DEAD) {
        snprintf(life_text, sizeof(life_text), "You died - back in %d...",
                 (int)((roam->respawn_at - SDL_GetTicks64() + 999) / 1000));
    } else {
        snprintf(life_text, sizeof(life_text), "Life %d / %d", player->hp, player->max_hp);
    }
    Label_SetText(roam->life_label, life_text);
    Label_Draw(roam->life_label, 134, VIEW_H - 37);

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
    }

    const SDL_Rect bottom = {0, VIEW_H, 800, 600 - VIEW_H};
    SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
    SDL_RenderFillRect(sdl_renderer, &bottom);
    for (int i = 0; i < 6; i++) {
        Button_Draw(roam->buttons[i]);
    }
}

void TestingGrounds_Render(void *scene_ref) { Roam__Render(scene_ref); }

void FreeRoam_Render(void *scene_ref) { Roam__Render(scene_ref); }
