#include "SceneTestingGrounds.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../managers/InputManager.h"
#include "../map/LevelTypes.h"
#include "../map/Map.h"
#include "../map/MapRenderer.h"
#include "../map/MapSession.h"
#include "../map/MapUnits.h"
#include "CatalogCharacters.h"
#include "SceneMainMenu.h"
#include "SceneMapEditor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W          800
#define VIEW_H          558
#define BOTTOM_Y        562
#define PANEL_X         580
#define PANEL_INNER_X   588
#define PANEL_INNER_W   204
#define CATEGORIES_Y    48
#define LIST_ROWS       20
#define LIST_ROW_H      18
#define MAX_DRAWABLES   1024
#define MAX_SPAWNS      256
#define WALK_SPEED      5.0f // sub-tiles per second
#define RUN_SPEED       9.0f
#define ARRIVE_DISTANCE 0.15f

#define DEFAULT_MAP_SIZE    32
#define DEFAULT_LEVEL_TYPE  "Act 1 - Town"
#define TESTING_GROUNDS_MAP "testing_grounds"

static const char *player_classes[][2] = {
    {"Amazon",      "am"},
    {"Sorceress",   "so"},
    {"Necromancer", "ne"},
    {"Paladin",     "pa"},
    {"Barbarian",   "ba"},
    {"Druid",       "dz"},
    {"Assassin",    "ai"},
};
#define PLAYER_CLASS_COUNT (int)(sizeof(player_classes) / sizeof(player_classes[0]))

typedef struct Spawn {
    char  base[12];
    char  id[40];
    float x;
    float y;
} Spawn;

typedef struct DrawContext {
    Figure *figure;
    int     direction;
    int     frame;
} DrawContext;

typedef struct SceneTestingGrounds {
    Map         *map;
    bool         borrowed; // the editor's map (play-test): not ours to free
    MapRenderer *renderer;
    MapUnits    *units;
    MapCamera    camera;
    uint64_t     last_tick;
    unsigned     draw_flags;

    // Player
    int      player_class;
    float    player_x; // sub-tiles
    float    player_y;
    float    target_x;
    float    target_y;
    bool     moving;
    bool     running;
    int      player_turn;
    int      player_frame;
    uint64_t player_frame_time;

    // Spawned monsters (not saved into the map)
    Spawn spawns[MAX_SPAWNS];
    int   spawn_count;

    // UI
    Button  *back_button;
    Button  *class_button;
    Button  *run_button;
    Button  *spawn_button;
    Button  *clear_button;
    bool     panel_open;
    ListBox *list;
    char   **list_names;
    int      list_count;
    int      selected;
    Label   *title_label;
    Label   *status_label;
    Label   *help_label;
    bool     prev_left;
    bool     prev_right;

    DrawContext player_draw;
    DrawContext spawn_draws[MAX_SPAWNS];
} SceneTestingGrounds;

DEFINE_SCENE_CALLBACKS(TestingGrounds);

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
// Helpers

static const char *Grounds__PlayerMode(const SceneTestingGrounds *grounds) {
    if (!grounds->moving) {
        return "tn";
    }
    return grounds->running ? "rn" : "tw";
}

static void Grounds__UpdateButtons(SceneTestingGrounds *grounds) {
    Button_SetText(grounds->class_button, player_classes[grounds->player_class][0]);
    Button_SetText(grounds->run_button, grounds->running ? "Run" : "Walk");
    Button_SetText(grounds->spawn_button, grounds->panel_open ? "Hide List" : "Monsters");
}

static void Grounds__RefreshList(SceneTestingGrounds *grounds) {
    for (int i = 0; i < grounds->list_count; i++) {
        free(grounds->list_names[i]);
    }
    free(grounds->list_names);

    const CatalogCharacters *bestiary = MapSession_GetBestiary();
    grounds->list_count               = CatalogCharacters_GetCount(bestiary);
    grounds->list_names = calloc(grounds->list_count > 0 ? (size_t)grounds->list_count : 1, sizeof(char *));
    FAIL_IF_NULL(grounds->list_names);
    for (int i = 0; i < grounds->list_count; i++) {
        grounds->list_names[i] = strdup(CatalogCharacters_GetName(bestiary, i));
        FAIL_IF_NULL(grounds->list_names[i]);
    }
    ListBox_SetItems(grounds->list, (const char *const *)grounds->list_names, grounds->list_count);
    grounds->selected = 0;
    // Under the category bar, which may take two rows
    ListBox_SetPosition(grounds->list, PANEL_INNER_X,
                        CATEGORIES_Y + CatalogCharacters_GetCategoriesHeight(bestiary, PANEL_INNER_W) + 4);
}

// Start on the walkable sub-tile closest to the middle of the map
static void Grounds__PlacePlayer(SceneTestingGrounds *grounds) {
    const int cx = (grounds->map->width * MAP_SUBTILES) / 2;
    const int cy = (grounds->map->height * MAP_SUBTILES) / 2;
    for (int radius = 0; radius < MAP_MAX_SIZE * MAP_SUBTILES; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if ((abs(dx) == radius || abs(dy) == radius) && Map_IsWalkable(grounds->map, cx + dx, cy + dy)) {
                    grounds->player_x = (float)(cx + dx) + 0.5f;
                    grounds->player_y = (float)(cy + dy) + 0.5f;
                    return;
                }
            }
        }
    }
    grounds->player_x = (float)cx + 0.5f;
    grounds->player_y = (float)cy + 0.5f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

void *TestingGrounds_Create(void) {
    Cursor_SetVisible(true);

    SceneTestingGrounds *grounds = calloc(1, sizeof(SceneTestingGrounds));
    FAIL_IF_NULL(grounds);
    grounds->renderer    = MapRenderer_Create();
    grounds->units       = MapUnits_Create();
    grounds->camera.zoom = 1.0f;
    grounds->last_tick   = SDL_GetTicks64();
    grounds->draw_flags  = MAP_DRAW_DEFAULT;

    if (MapSession_IsPlaytest() && MapSession_PeekMap() != NULL) {
        grounds->map      = (Map *)MapSession_PeekMap();
        grounds->borrowed = true;
    } else {
        char path[4200];
        snprintf(path, sizeof(path), "%s/%s.abm", Map_GetSaveFolder(), TESTING_GROUNDS_MAP);
        grounds->map = Map_LoadFile(path);
        if (grounds->map == NULL) {
            grounds->map = TestingGrounds_CreateDefaultMap();
        }
    }
    Grounds__PlacePlayer(grounds);
    grounds->target_x = grounds->player_x;
    grounds->target_y = grounds->player_y;

    grounds->title_label = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetText(grounds->title_label, "Monsters");
    Label_SetAlignment(grounds->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(grounds->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    grounds->status_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    grounds->help_label   = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetText(grounds->help_label,
                  "Left click: walk   Right click: drop the selected monster   Shift+Right: remove   "
                  "R: run   B: blocked tiles   Wheel: zoom");
    Label_SetColor(grounds->help_label, 0xA0, 0xA0, 0xA0, 0xFF);

    static const char *names[5] = {"Back", "Class", "Walk", "Monsters", "Clear"};
    Button            *buttons[5];
    for (int i = 0; i < 5; i++) {
        buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, names[i]);
        Button_SetPosition(buttons[i], 8 + (i * 140), BOTTOM_Y);
    }
    grounds->back_button  = buttons[0];
    grounds->class_button = buttons[1];
    grounds->run_button   = buttons[2];
    grounds->spawn_button = buttons[3];
    grounds->clear_button = buttons[4];

    grounds->list = ListBox_Create(FONTS_FONT8, PANEL_INNER_X, 64, PANEL_INNER_W, LIST_ROWS, LIST_ROW_H);
    ListBox_SetBackground(grounds->list, 0, 0, 0, 160);
    Grounds__RefreshList(grounds);
    Grounds__UpdateButtons(grounds);
    return grounds;
}

void TestingGrounds_Free(void **scene_ref) {
    SceneTestingGrounds *grounds = *(SceneTestingGrounds **)scene_ref;
    if (!grounds->borrowed) {
        Map_Destroy(&grounds->map);
    }
    MapRenderer_Destroy(&grounds->renderer);
    MapUnits_Destroy(&grounds->units);
    for (int i = 0; i < grounds->list_count; i++) {
        free(grounds->list_names[i]);
    }
    free(grounds->list_names);
    ListBox_Destroy(&grounds->list);
    Button_Destroy(&grounds->back_button);
    Button_Destroy(&grounds->class_button);
    Button_Destroy(&grounds->run_button);
    Button_Destroy(&grounds->spawn_button);
    Button_Destroy(&grounds->clear_button);
    Label_Destroy(&grounds->title_label);
    Label_Destroy(&grounds->status_label);
    Label_Destroy(&grounds->help_label);
    free(grounds);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Update

static Figure *Grounds__PlayerFigure(SceneTestingGrounds *grounds) {
    return MapUnits_GetFigure(grounds->units, "chars", player_classes[grounds->player_class][1],
                              Grounds__PlayerMode(grounds));
}

static void Grounds__MovePlayer(SceneTestingGrounds *grounds, const float seconds) {
    if (!grounds->moving) {
        return;
    }
    const float dx       = grounds->target_x - grounds->player_x;
    const float dy       = grounds->target_y - grounds->player_y;
    const float distance = sqrtf((dx * dx) + (dy * dy));
    if (distance < ARRIVE_DISTANCE) {
        grounds->moving = false;
        return;
    }

    const float step   = (grounds->running ? RUN_SPEED : WALK_SPEED) * seconds;
    const float amount = step >= distance ? 1.0f : step / distance;
    const float next_x = grounds->player_x + (dx * amount);
    const float next_y = grounds->player_y + (dy * amount);

    // Slide along walls: try the full step, then each axis on its own
    if (Map_IsWalkable(grounds->map, (int)floorf(next_x), (int)floorf(next_y))) {
        grounds->player_x = next_x;
        grounds->player_y = next_y;
    } else if (Map_IsWalkable(grounds->map, (int)floorf(next_x), (int)floorf(grounds->player_y))) {
        grounds->player_x = next_x;
    } else if (Map_IsWalkable(grounds->map, (int)floorf(grounds->player_x), (int)floorf(next_y))) {
        grounds->player_y = next_y;
    } else {
        grounds->moving = false; // blocked
    }
}

static void Grounds__TurnTowards(int *turn, const float from_x, const float from_y, const float to_x,
                                 const float to_y) {
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

void TestingGrounds_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneTestingGrounds *grounds = scene_ref;
    const uint64_t       now     = SDL_GetTicks64();
    const float          seconds = (float)(now - grounds->last_tick) / 1000.0f;
    grounds->last_tick           = now;
    const SDL_Rect view          = {0, 0, VIEW_W, VIEW_H};

    int  mouse_x;
    int  mouse_y;
    bool left;
    bool right;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, &right);
    const bool over_panel = grounds->panel_open && mouse_x >= PANEL_X;
    const bool over_view  = mouse_y < VIEW_H && !over_panel;

    if (over_view) {
        const int wheel = InputManager_ConsumeMouseWheel();
        if (wheel != 0) {
            grounds->camera.zoom =
                wheel > 0 ? fminf(grounds->camera.zoom * 2.0f, 2.0f) : fmaxf(grounds->camera.zoom / 2.0f, 0.25f);
        }
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_R)) {
        grounds->running = !grounds->running;
        Grounds__UpdateButtons(grounds);
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_B)) {
        grounds->draw_flags ^= MAP_DRAW_BLOCKED;
    }

    float world_x;
    float world_y;
    float sub_x;
    float sub_y;
    MapView_ScreenToWorld(&grounds->camera, &view, mouse_x, mouse_y, &world_x, &world_y);
    MapView_WorldToSubtile(world_x, world_y, &sub_x, &sub_y);

    // Left: walk there (hold to keep following the mouse)
    if (over_view && left) {
        grounds->target_x = sub_x;
        grounds->target_y = sub_y;
        grounds->moving   = true;
    }
    // Right: drop the selected monster; Shift+Right removes the nearest one
    if (over_view && right && !grounds->prev_right) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            int   nearest  = -1;
            float distance = 9.0f;
            for (int i = 0; i < grounds->spawn_count; i++) {
                const float dx = grounds->spawns[i].x - sub_x;
                const float dy = grounds->spawns[i].y - sub_y;
                if ((dx * dx) + (dy * dy) < distance) {
                    distance = (dx * dx) + (dy * dy);
                    nearest  = i;
                }
            }
            if (nearest >= 0) {
                grounds->spawns[nearest] = grounds->spawns[--grounds->spawn_count];
            }
        } else if (grounds->spawn_count < MAX_SPAWNS) {
            CatalogCharacterInfo info;
            if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), grounds->selected, &info)) {
                Spawn *spawn = &grounds->spawns[grounds->spawn_count++];
                snprintf(spawn->base, sizeof(spawn->base), "%s", info.spec->base);
                snprintf(spawn->id, sizeof(spawn->id), "%s", info.id);
                spawn->x = floorf(sub_x) + 0.5f;
                spawn->y = floorf(sub_y) + 0.5f;
            }
        }
    }
    grounds->prev_left  = left;
    grounds->prev_right = right;

    // Player movement and animation
    const char *previous_mode = Grounds__PlayerMode(grounds);
    if (grounds->moving) {
        Grounds__TurnTowards(&grounds->player_turn, grounds->player_x, grounds->player_y, grounds->target_x,
                             grounds->target_y);
    }
    Grounds__MovePlayer(grounds, seconds);
    if (strcmp(previous_mode, Grounds__PlayerMode(grounds)) != 0) {
        grounds->player_frame      = 0;
        grounds->player_frame_time = now;
    }
    Figure *figure = Grounds__PlayerFigure(grounds);
    if (figure != NULL) {
        const int frames = Figure_GetFramesPerDirection(figure);
        const int ms     = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
        while (frames > 0 && now - grounds->player_frame_time >= (uint64_t)ms) {
            grounds->player_frame_time += (uint64_t)ms;
            grounds->player_frame       = (grounds->player_frame + 1) % frames;
        }
    }

    // Camera follows the player
    MapView_SubtileToWorld(grounds->player_x, grounds->player_y, &grounds->camera.x, &grounds->camera.y);
    grounds->camera.y -= 40.0f; // keep the figure's body, not its feet, in the middle

    // Panel
    if (grounds->panel_open) {
        if (CatalogCharacters_UpdateCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W)) {
            Grounds__RefreshList(grounds);
        }
        if (ListBox_Update(grounds->list)) {
            grounds->selected = ListBox_GetSelectedIndex(grounds->list);
        }
    }

    if (Button_Update(grounds->class_button)) {
        grounds->player_class = (grounds->player_class + 1) % PLAYER_CLASS_COUNT;
        Grounds__UpdateButtons(grounds);
    }
    if (Button_Update(grounds->run_button)) {
        grounds->running = !grounds->running;
        Grounds__UpdateButtons(grounds);
    }
    if (Button_Update(grounds->spawn_button)) {
        grounds->panel_open = !grounds->panel_open;
        Grounds__UpdateButtons(grounds);
    }
    if (Button_Update(grounds->clear_button)) {
        grounds->spawn_count = 0;
    }
    if (Button_Update(grounds->back_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        // Play-testing goes back to the editor (which takes its map back), otherwise to the main menu
        Scene_Set(grounds->borrowed ? SCENE_REF(MapEditor) : SCENE_REF(MainMenu));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Render

static void Grounds__DrawFigure(void *context, const int screen_x, const int screen_y, const float zoom) {
    const DrawContext *draw = context;
    Figure_Draw(draw->figure, draw->direction, draw->frame, screen_x, screen_y, zoom);
}

void TestingGrounds_Render(void *scene_ref) {
    SceneTestingGrounds *grounds = scene_ref;
    const SDL_Rect       view    = {0, 0, VIEW_W, VIEW_H};
    const uint64_t       now     = SDL_GetTicks64();

    SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 14, 255);
    SDL_RenderClear(sdl_renderer);

    MapDrawable drawables[MAX_DRAWABLES];
    int count = MapUnits_BuildDrawables(grounds->units, grounds->map, now, drawables, MAX_DRAWABLES - MAX_SPAWNS - 1);

    // Spawned monsters face the player
    for (int i = 0; i < grounds->spawn_count && count < MAX_DRAWABLES - 1; i++) {
        const Spawn *spawn  = &grounds->spawns[i];
        Figure      *figure = MapUnits_GetFigure(grounds->units, spawn->base, spawn->id,
                                                 MapUnits_IdleMode(grounds->units, spawn->base, spawn->id));
        if (figure == NULL) {
            continue;
        }
        int turn = 0;
        Grounds__TurnTowards(&turn, spawn->x, spawn->y, grounds->player_x, grounds->player_y);
        const int    frames = Figure_GetFramesPerDirection(figure);
        const int    ms     = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
        DrawContext *draw   = &grounds->spawn_draws[i];
        draw->figure        = figure;
        draw->direction     = MapUnits_Direction(figure, turn);
        draw->frame         = frames > 0 ? (int)(((now + ((uint64_t)i * 977u)) / (uint64_t)ms) % (uint64_t)frames) : 0;
        drawables[count++]  = (MapDrawable){spawn->x, spawn->y, Grounds__DrawFigure, draw};
    }

    Figure *player = Grounds__PlayerFigure(grounds);
    if (player != NULL) {
        grounds->player_draw.figure    = player;
        grounds->player_draw.direction = MapUnits_Direction(player, grounds->player_turn);
        grounds->player_draw.frame =
            grounds->player_frame %
            (Figure_GetFramesPerDirection(player) > 0 ? Figure_GetFramesPerDirection(player) : 1);
        drawables[count++] =
            (MapDrawable){grounds->player_x, grounds->player_y, Grounds__DrawFigure, &grounds->player_draw};
    }

    MapRenderer_Draw(grounds->renderer, grounds->map, &grounds->camera, &view, drawables, count, grounds->draw_flags);

    // Walk target marker
    if (grounds->moving) {
        float wx;
        float wy;
        MapView_SubtileToWorld(grounds->target_x, grounds->target_y, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(&grounds->camera, &view, wx, wy, &sx, &sy);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 120, 200);
        SDL_RenderDrawLine(sdl_renderer, sx - 6, sy, sx + 6, sy);
        SDL_RenderDrawLine(sdl_renderer, sx, sy - 3, sx, sy + 3);
    }

    // Status
    const SDL_Rect bar = {0, 0, VIEW_W, 14};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &bar);
    char status[200];
    snprintf(status, sizeof(status), "%s%s   %s at %.1f, %.1f   %d placed + %d dropped monsters", grounds->map->name,
             grounds->borrowed ? " (play-test)" : "", player_classes[grounds->player_class][0],
             (double)grounds->player_x, (double)grounds->player_y, grounds->map->entity_count, grounds->spawn_count);
    Label_SetText(grounds->status_label, status);
    Label_Draw(grounds->status_label, 4, 2);
    const SDL_Rect help_bar = {0, VIEW_H - 14, VIEW_W, 14};
    SDL_RenderFillRect(sdl_renderer, &help_bar);
    Label_Draw(grounds->help_label, 4, VIEW_H - 12);

    if (grounds->panel_open) {
        const SDL_Rect panel = {PANEL_X, 14, 800 - PANEL_X, VIEW_H - 28};
        SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 235);
        SDL_RenderFillRect(sdl_renderer, &panel);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
        SDL_RenderDrawRect(sdl_renderer, &panel);
        Label_Draw(grounds->title_label, PANEL_X + ((800 - PANEL_X) / 2), 30);
        CatalogCharacters_DrawCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W);
        ListBox_Draw(grounds->list);
    }

    const SDL_Rect bottom = {0, VIEW_H, 800, 600 - VIEW_H};
    SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
    SDL_RenderFillRect(sdl_renderer, &bottom);
    Button_Draw(grounds->back_button);
    Button_Draw(grounds->class_button);
    Button_Draw(grounds->run_button);
    Button_Draw(grounds->spawn_button);
    Button_Draw(grounds->clear_button);
}
