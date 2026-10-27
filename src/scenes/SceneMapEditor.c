#include "SceneMapEditor.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/ConfirmDialog.h"
#include "../drawing/Cursor.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/LoadingScreen.h"
#include "../drawing/TabBar.h"
#include "../drawing/TextBox.h"
#include "../game/Settings.h"
#include "../managers/InputManager.h"
#include "../map/LevelTypes.h"
#include "../map/Map.h"
#include "../map/MapHistory.h"
#include "../map/MapRenderer.h"
#include "../map/MapSession.h"
#include "../map/MapUnits.h"
#include "CatalogCharacters.h"
#include "SceneMainMenu.h"
#include "SceneTestingGrounds.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Layout (800x600): map view on the left, tool panel on the right, file buttons under the map.
#define VIEW_W        580
#define VIEW_H        556
#define PANEL_X       580
#define PANEL_INNER_X 588
#define PANEL_INNER_W 204
#define TOOLS_Y       34
#define PREVIEW_H     92
// Below the tool tabs, which can take one or two rows (set when the editor is created)
#define PREVIEW_Y     (editor->preview_y)
#define CATEGORIES_Y  (editor->preview_y + PREVIEW_H + 6) // Units: Bestiary categories under the preview
#define LIST_Y        (editor->preview_y + PREVIEW_H + 8)
#define LIST_ROWS     15
#define LIST_ROW_H    18
#define BOTTOM_Y      562
#define MAX_DRAWABLES 1024
#define PAN_SPEED     0.6f // world pixels per millisecond at zoom 1

static const float zoom_levels[] = {0.25f, 0.5f, 1.0f, 2.0f};
#define ZOOM_COUNT   (int)(sizeof(zoom_levels) / sizeof(zoom_levels[0]))
#define ZOOM_DEFAULT 2

static const int map_sizes[] = {16, 24, 32, 48, 64, 96, 128};
#define MAP_SIZE_COUNT   (int)(sizeof(map_sizes) / sizeof(map_sizes[0]))
#define MAP_SIZE_DEFAULT 1

typedef enum EditorTool { TOOL_FLOOR, TOOL_WALL, TOOL_SHADOW, TOOL_UNITS, TOOL_COUNT } EditorTool;
static const char *tool_names[TOOL_COUNT] = {"Floor", "Wall", "Shadow", "Units"};

typedef enum EditorDialog { DIALOG_NONE, DIALOG_OPEN, DIALOG_NEW, DIALOG_SAVE } EditorDialog;
typedef enum OpenSource { OPEN_MY_MAPS, OPEN_GAME_MAPS, OPEN_SOURCE_COUNT } OpenSource;
static const char *open_source_names[OPEN_SOURCE_COUNT] = {"My Maps", "Game Areas (DS1)"};

#define DIALOG_X 140
#define DIALOG_Y 80
#define DIALOG_W 520
#define DIALOG_H 440

// What the editor asks about with its confirm dialog
typedef enum EditorQuestion { QUESTION_NONE, QUESTION_LEAVE, QUESTION_RESTORE, QUESTION_RESET_TESTING } EditorQuestion;

typedef struct SceneMapEditor {
    Map           *map;
    char           file_path[4200]; // where the map was loaded from / saved to ("" = new)
    bool           dirty;
    MapHistory    *history;       // undo / redo (Ctrl+Z / Ctrl+Y)
    bool           stroke;        // a change is open: the mouse is held painting or erasing
    uint64_t       last_autosave; // Options › Gameplay › Debug: auto-save every so many minutes
    ConfirmDialog *confirm;
    EditorQuestion question;
    MapRenderer   *renderer;
    MapUnits      *units;
    MapCamera      camera;
    int            zoom_index;
    unsigned       draw_flags;

    // Tool panel
    EditorTool tool;
    TabBar    *tool_bar;
    int        preview_y;
    ListBox   *list;
    char     **list_names;
    int        list_count;
    int        selected[TOOL_COUNT];
    Label     *title_label;
    Label     *preview_label;
    Label     *info_labels[4];
    Button    *fill_button;
    Button    *back_button;

    // File buttons
    Button *new_button;
    Button *open_button;
    Button *save_button;
    Button *play_button;

    // Map view
    Label   *status_label;
    Label   *help_label;
    bool     hover_valid;
    int      hover_tx;
    int      hover_ty;
    float    hover_sx; // sub-tiles
    float    hover_sy;
    bool     prev_left;
    bool     prev_right;
    bool     panning;
    int      pan_mouse_x;
    int      pan_mouse_y;
    float    pan_camera_x;
    float    pan_camera_y;
    uint64_t last_tick;

    char     message[128];
    uint64_t message_until;
    Label   *message_label;

    // Dialogs
    EditorDialog dialog;
    Label       *dialog_title;
    Label       *dialog_labels[3];
    Button      *dialog_ok;
    Button      *dialog_extra;
    Button      *dialog_ds1; // Save: as the game's DS1 too
    Button      *dialog_cancel;
    TabBar      *open_tabs;
    TextBox     *search_box;
    ListBox     *dialog_list;
    char       **saved_names;
    int          saved_count;
    const char **game_maps; // DS1 paths from the file list
    int          game_map_count;
    int         *dialog_visible; // indices shown in dialog_list
    int          dialog_visible_count;
    Button      *new_type_button;
    Button      *new_size_button;
    int          new_type;
    int          new_size;
    TextBox     *name_box;
} SceneMapEditor;

DEFINE_SCENE_CALLBACKS(MapEditor);

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

static void Editor__Message(SceneMapEditor *editor, const char *text) {
    snprintf(editor->message, sizeof(editor->message), "%s", text);
    editor->message_until = SDL_GetTicks64() + 3000;
    Label_SetText(editor->message_label, editor->message);
}

static bool Editor__Inside(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void Editor__CenterCamera(SceneMapEditor *editor) {
    MapView_SubtileToWorld((float)(editor->map->width * MAP_SUBTILES) / 2.0f,
                           (float)(editor->map->height * MAP_SUBTILES) / 2.0f, &editor->camera.x, &editor->camera.y);
}

static MapLayerKind Editor__LayerKind(const EditorTool tool) {
    return tool == TOOL_WALL ? MAP_LAYER_WALL : (tool == TOOL_SHADOW ? MAP_LAYER_SHADOW : MAP_LAYER_FLOOR);
}

static const char *Editor__OrientationName(const int orientation) {
    switch (orientation) {
    case 0:
        return "Floor";
    case 1:
        return "Left wall";
    case 2:
        return "Right wall";
    case 3:
        return "NW corner";
    case 5:
        return "SE corner";
    case 6:
        return "NE corner";
    case 7:
        return "SW corner";
    case 8:
        return "Door L";
    case 9:
        return "Door R";
    case 10:
    case 11:
        return "Marker";
    case 12:
        return "Pillar";
    case 13:
        return "Shadow";
    case 14:
        return "Tree";
    case 15:
        return "Roof";
    default:
        return orientation >= 16 ? "Lower wall" : "Wall";
    }
}

// The list sits under the category bar (one or two rows) on the Units tab
static int Editor__ListY(const SceneMapEditor *editor) {
    if (editor->tool != TOOL_UNITS) {
        return LIST_Y;
    }
    return CATEGORIES_Y + CatalogCharacters_GetCategoriesHeight(MapSession_GetBestiary(), PANEL_INNER_W) + 4;
}

static void Editor__FreeListNames(SceneMapEditor *editor) {
    for (int i = 0; i < editor->list_count; i++) {
        free(editor->list_names[i]);
    }
    free(editor->list_names);
    editor->list_names = NULL;
    editor->list_count = 0;
}

// Fills the panel list for the current tool
static void Editor__RefreshList(SceneMapEditor *editor) {
    Editor__FreeListNames(editor);

    if (editor->tool == TOOL_UNITS) {
        const CatalogCharacters *bestiary = MapSession_GetBestiary();
        editor->list_count                = CatalogCharacters_GetCount(bestiary);
        editor->list_names = calloc(editor->list_count > 0 ? (size_t)editor->list_count : 1, sizeof(char *));
        FAIL_IF_NULL(editor->list_names);
        for (int i = 0; i < editor->list_count; i++) {
            editor->list_names[i] = strdup(CatalogCharacters_GetName(bestiary, i));
            FAIL_IF_NULL(editor->list_names[i]);
        }
    } else {
        const MapTileChoice *choices;
        editor->list_count = Map_GetChoices(editor->map, Editor__LayerKind(editor->tool), &choices);
        editor->list_names = calloc(editor->list_count > 0 ? (size_t)editor->list_count : 1, sizeof(char *));
        FAIL_IF_NULL(editor->list_names);
        for (int i = 0; i < editor->list_count; i++) {
            const char *dt1   = editor->map->dt1_paths[choices[i].dt1_index];
            const char *slash = strrchr(dt1, '\\');
            char        name[96];
            snprintf(name, sizeof(name), "%s %d:%d (%s)", Editor__OrientationName(choices[i].orientation),
                     choices[i].style, choices[i].sequence, slash != NULL ? slash + 1 : dt1);
            editor->list_names[i] = strdup(name);
            FAIL_IF_NULL(editor->list_names[i]);
        }
    }

    ListBox_SetItems(editor->list, (const char *const *)editor->list_names, editor->list_count);
    if (editor->selected[editor->tool] >= editor->list_count) {
        editor->selected[editor->tool] = 0;
    }
    ListBox_SetSelectedIndex(editor->list, editor->selected[editor->tool]);
    // The category bar takes room above the list for units
    ListBox_SetPosition(editor->list, PANEL_INNER_X, Editor__ListY(editor));
}

static const MapTileChoice *Editor__SelectedChoice(const SceneMapEditor *editor) {
    if (editor->tool == TOOL_UNITS) {
        return NULL;
    }
    const MapTileChoice *choices;
    const int            count = Map_GetChoices(editor->map, Editor__LayerKind(editor->tool), &choices);
    const int            index = editor->selected[editor->tool];
    return (index >= 0 && index < count) ? &choices[index] : NULL;
}

// The recovery file: the map as it is after every change (Options › Gameplay › Debug › Keep every change...)
static void Editor__RecoveryPath(char *path, const size_t size) {
    snprintf(path, size, "%s/editor-recovery.abm", AbyssConfiguration_GetConfigFolder());
}

static void Editor__ForgetRecovery(void) {
    char path[4200];
    Editor__RecoveryPath(path, sizeof(path));
    remove(path);
}

// What's kept while play-testing (the scene is made again when you come back)
static struct {
    MapHistory *history;
    char        file_path[4200];
    bool        dirty;
} parked;

// A change was made (a stroke, a fill, a unit): kept in the recovery file if wanted
static void Editor__EndChange(SceneMapEditor *editor) {
    MapHistory_End(editor->history);
    if (Settings_GetDebug()->editor_keep_changes && editor->dirty) {
        char path[4200];
        Editor__RecoveryPath(path, sizeof(path));
        Map_SaveFile(editor->map, path);
    }
}

static void Editor__Undo(SceneMapEditor *editor, const bool redo) {
    editor->stroke = false;
    const bool done =
        redo ? MapHistory_Redo(editor->history, editor->map) : MapHistory_Undo(editor->history, editor->map);
    if (!done) {
        Editor__Message(editor, redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    editor->dirty = true;
    char text[64];
    snprintf(text, sizeof(text), "%s: change %d of %d", redo ? "Redone" : "Undone",
             MapHistory_GetPosition(editor->history), MapHistory_GetCount(editor->history));
    Editor__Message(editor, text);
    if (Settings_GetDebug()->editor_keep_changes) {
        char path[4200];
        Editor__RecoveryPath(path, sizeof(path));
        Map_SaveFile(editor->map, path);
    }
}

static void Editor__SetMap(SceneMapEditor *editor, Map *map, const char *file_path) {
    Map_Destroy(&editor->map);
    editor->map = map;
    snprintf(editor->file_path, sizeof(editor->file_path), "%s", file_path != NULL ? file_path : "");
    editor->dirty  = false;
    editor->stroke = false;
    MapHistory_Clear(editor->history);
    MapRenderer_ClearCache(editor->renderer);
    for (int t = 0; t < TOOL_COUNT; t++) {
        if (t != TOOL_UNITS) {
            editor->selected[t] = 0;
        }
    }
    Editor__CenterCamera(editor);
    Editor__RefreshList(editor);
}

static Map *Editor__CreateBlank(const int type_index, const int size) {
    const LevelType *type = LevelTypes_Get(type_index);
    if (type == NULL) {
        return NULL;
    }
    Map *map = Map_CreateBlank(size, size, type->act, (const char *const *)type->files, type->file_count);
    snprintf(map->name, sizeof(map->name), "New %s", type->name);

    // Start covered with the tile set's first floor
    const MapTileChoice *floors;
    if (Map_GetChoices(map, MAP_LAYER_FLOOR, &floors) > 0) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                Map_SetCell(map, MAP_LAYER_FLOOR, 0, tx, ty, &floors[0]);
            }
        }
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

void *MapEditor_Create(void) {
    Cursor_SetVisible(true);

    SceneMapEditor *editor = calloc(1, sizeof(SceneMapEditor));
    FAIL_IF_NULL(editor);
    editor->renderer    = MapRenderer_Create();
    editor->units       = MapUnits_Create();
    editor->zoom_index  = ZOOM_DEFAULT;
    editor->camera.zoom = zoom_levels[ZOOM_DEFAULT];
    editor->draw_flags  = MAP_DRAW_DEFAULT | MAP_DRAW_GRID;
    editor->last_tick   = SDL_GetTicks64();
    editor->new_size    = MAP_SIZE_DEFAULT;
    editor->new_type    = LevelTypes_Find("Act 1 - Town") >= 0 ? LevelTypes_Find("Act 1 - Town") : 0;

    editor->title_label = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetText(editor->title_label, "Map Editor");
    Label_SetAlignment(editor->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(editor->title_label, 0xC7, 0xB3, 0x77, 0xFF);

    editor->tool_bar  = TabBar_Create(tool_names, TOOL_COUNT, TOOL_FLOOR);
    editor->preview_y = TOOLS_Y + TabBar_GetHeight(editor->tool_bar, PANEL_INNER_W) + 4;
    editor->list      = ListBox_Create(FONTS_FONT8, PANEL_INNER_X, LIST_Y, PANEL_INNER_W, LIST_ROWS, LIST_ROW_H);
    ListBox_SetBackground(editor->list, 0, 0, 0, 160);

    editor->preview_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(editor->preview_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int i = 0; i < 4; i++) {
        editor->info_labels[i] = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        Label_SetColor(editor->info_labels[i], 0xC0, 0xC0, 0xC0, 0xFF);
    }

    editor->fill_button = Button_Create(BUTTON_TYPE_MEDIUM, "Fill Floor");
    Button_SetPosition(editor->fill_button, PANEL_INNER_X + 38, 512);
    editor->back_button = Button_Create(BUTTON_TYPE_MEDIUM, "Back");
    Button_SetPosition(editor->back_button, PANEL_INNER_X + 38, BOTTOM_Y);

    static const char *file_names[4] = {"New", "Open", "Save", "Play"};
    Button            *file_buttons[4];
    for (int i = 0; i < 4; i++) {
        file_buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, file_names[i]);
        Button_SetPosition(file_buttons[i], 8 + (i * 140), BOTTOM_Y);
    }
    editor->new_button  = file_buttons[0];
    editor->open_button = file_buttons[1];
    editor->save_button = file_buttons[2];
    editor->play_button = file_buttons[3];

    editor->status_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    editor->help_label   = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetText(editor->help_label, "L: paint  R: erase  Middle/WASD: move  Wheel: zoom  G grid  H walls  "
                                      "B blocked  R roofs");
    Label_SetColor(editor->help_label, 0xA0, 0xA0, 0xA0, 0xFF);
    editor->message_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(editor->message_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(editor->message_label, 0xFF, 0xE8, 0xA0, 0xFF);

    // Dialog widgets (shared by Open / New / Save)
    editor->dialog_title = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetAlignment(editor->dialog_title, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(editor->dialog_title, 0xC7, 0xB3, 0x77, 0xFF);
    for (int i = 0; i < 3; i++) {
        editor->dialog_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetColor(editor->dialog_labels[i], 0xD8, 0xC0, 0x80, 0xFF);
    }
    editor->dialog_ok     = Button_Create(BUTTON_TYPE_MEDIUM, "OK");
    editor->dialog_extra  = Button_Create(BUTTON_TYPE_MEDIUM, "Extra");
    editor->dialog_cancel = Button_Create(BUTTON_TYPE_MEDIUM, "Cancel");
    editor->dialog_ds1    = Button_Create(BUTTON_TYPE_MEDIUM, "As DS1");
    Button_SetPosition(editor->dialog_ds1, DIALOG_X + 196, DIALOG_Y + DIALOG_H - 100);
    Button_SetPosition(editor->dialog_ok, DIALOG_X + 20, DIALOG_Y + DIALOG_H - 50);
    Button_SetPosition(editor->dialog_extra, DIALOG_X + 196, DIALOG_Y + DIALOG_H - 50);
    Button_SetPosition(editor->dialog_cancel, DIALOG_X + DIALOG_W - 148, DIALOG_Y + DIALOG_H - 50);
    editor->open_tabs = TabBar_Create(open_source_names, OPEN_SOURCE_COUNT, OPEN_MY_MAPS);
    editor->search_box =
        TextBox_Create(FONTS_FONT16, DIALOG_X + 20, DIALOG_Y + 66, DIALOG_W - 40, 20, "Type to search...");
    editor->dialog_list = ListBox_Create(FONTS_FONT8, DIALOG_X + 20, DIALOG_Y + 92, DIALOG_W - 40, 15, LIST_ROW_H);
    ListBox_SetBackground(editor->dialog_list, 0, 0, 0, 160);
    editor->new_type_button = Button_Create(BUTTON_TYPE_WIDE, "Area");
    editor->new_size_button = Button_Create(BUTTON_TYPE_MEDIUM, "Size");
    Button_SetPosition(editor->new_type_button, DIALOG_X + 180, DIALOG_Y + 80);
    Button_SetPosition(editor->new_size_button, DIALOG_X + 180, DIALOG_Y + 140);
    editor->name_box = TextBox_Create(FONTS_FONT16, DIALOG_X + 140, DIALOG_Y + 90, DIALOG_W - 160, 20, "Map name");

    // The game's DS1 areas, for Open
    const char *const *files;
    const int          file_count = FileManager_GetFileList(&files);
    editor->game_maps             = calloc(file_count > 0 ? (size_t)file_count : 1, sizeof(char *));
    FAIL_IF_NULL(editor->game_maps);
    for (int i = 0; i < file_count; i++) {
        const size_t len = strlen(files[i]);
        if (len > 4 && (strcmp(files[i] + len - 4, ".ds1") == 0 || strcmp(files[i] + len - 4, ".DS1") == 0)) {
            editor->game_maps[editor->game_map_count++] = files[i];
        }
    }
    editor->dialog_visible = calloc((size_t)(editor->game_map_count + 1), sizeof(int));
    FAIL_IF_NULL(editor->dialog_visible);

    editor->history       = MapHistory_Create();
    editor->confirm       = ConfirmDialog_Create();
    editor->last_autosave = SDL_GetTicks64();

    // Coming back from a play-test: carry on with the same map, its file, its undo history
    const bool returning = MapSession_PeekMap() != NULL;
    Map       *map       = returning ? MapSession_TakeMap() : NULL;
    if (map == NULL) {
        map = Editor__CreateBlank(editor->new_type, map_sizes[MAP_SIZE_DEFAULT]);
    }
    if (map == NULL) {
        map = TestingGrounds_CreateDefaultMap();
    }
    Editor__SetMap(editor, map, NULL);
    if (returning && parked.history != NULL) {
        MapHistory_Destroy(&editor->history);
        editor->history = parked.history;
        snprintf(editor->file_path, sizeof(editor->file_path), "%s", parked.file_path);
        editor->dirty = parked.dirty;
    } else {
        MapHistory_Destroy(&parked.history);
    }
    parked.history = NULL;

    // Changes the editor didn't get to save last time (it closed with them): offer them back
    char recovery[4200];
    Editor__RecoveryPath(recovery, sizeof(recovery));
    FILE *left_over = returning ? NULL : fopen(recovery, "r");
    if (left_over != NULL) {
        fclose(left_over);
        editor->question = QUESTION_RESTORE;
        ConfirmDialog_Open(editor->confirm, "Unsaved Changes",
                           "The editor closed last time with changes that weren't saved. Open them again?", "Restore",
                           "Discard", NULL);
    }

    // The Units tab's monsters and NPCs, loaded now (under the same loading screen) so the tab opens straight away
    LoadingScreen_Begin("Loading monsters and NPCs...");
    CatalogCharacterInfo info;
    if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), editor->selected[TOOL_UNITS], &info)) {
        MapUnits_GetFigure(editor->units, info.spec->base, info.id, info.idle_mode); // its preview
    }
    LoadingScreen_End();
    return editor;
}

void MapEditor_Free(void **scene_ref) {
    SceneMapEditor *editor = *(SceneMapEditor **)scene_ref;

    Editor__FreeListNames(editor);
    Map_FreeNames(editor->saved_names, editor->saved_count);
    free(editor->game_maps);
    free(editor->dialog_visible);
    // Handing over to the Testing Grounds keeps the map alive (see Play), and its undo history
    if (MapSession_PeekMap() != editor->map) {
        Map_Destroy(&editor->map);
        MapHistory_Destroy(&editor->history);
    } else {
        MapHistory_Destroy(&parked.history);
        parked.history = editor->history;
        snprintf(parked.file_path, sizeof(parked.file_path), "%s", editor->file_path);
        parked.dirty = editor->dirty;
    }
    ConfirmDialog_Destroy(&editor->confirm);
    MapRenderer_Destroy(&editor->renderer);
    MapUnits_Destroy(&editor->units);

    TabBar_Destroy(&editor->tool_bar);
    ListBox_Destroy(&editor->list);
    Label_Destroy(&editor->title_label);
    Label_Destroy(&editor->preview_label);
    for (int i = 0; i < 4; i++) {
        Label_Destroy(&editor->info_labels[i]);
    }
    Button_Destroy(&editor->fill_button);
    Button_Destroy(&editor->back_button);
    Button_Destroy(&editor->new_button);
    Button_Destroy(&editor->open_button);
    Button_Destroy(&editor->save_button);
    Button_Destroy(&editor->play_button);
    Label_Destroy(&editor->status_label);
    Label_Destroy(&editor->help_label);
    Label_Destroy(&editor->message_label);
    Label_Destroy(&editor->dialog_title);
    for (int i = 0; i < 3; i++) {
        Label_Destroy(&editor->dialog_labels[i]);
    }
    Button_Destroy(&editor->dialog_ok);
    Button_Destroy(&editor->dialog_extra);
    Button_Destroy(&editor->dialog_ds1);
    Button_Destroy(&editor->dialog_cancel);
    TabBar_Destroy(&editor->open_tabs);
    TextBox_Destroy(&editor->search_box);
    ListBox_Destroy(&editor->dialog_list);
    Button_Destroy(&editor->new_type_button);
    Button_Destroy(&editor->new_size_button);
    TextBox_Destroy(&editor->name_box);

    free(editor);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialogs

static bool Editor__MatchesSearch(const char *text, const char *query) {
    if (query[0] == '\0') {
        return true;
    }
    for (const char *start = text; *start != '\0'; start++) {
        const char *t = start;
        const char *q = query;
        while (*t != '\0' && *q != '\0' && tolower((unsigned char)*t) == tolower((unsigned char)*q)) {
            t++;
            q++;
        }
        if (*q == '\0') {
            return true;
        }
    }
    return false;
}

static void Editor__RefreshOpenList(SceneMapEditor *editor) {
    const char *query  = TextBox_GetText(editor->search_box);
    const bool  mine   = TabBar_GetSelected(editor->open_tabs) == OPEN_MY_MAPS;
    const int   source = mine ? editor->saved_count : editor->game_map_count;

    const char **names = calloc((size_t)(source + 1), sizeof(char *));
    FAIL_IF_NULL(names);
    editor->dialog_visible_count = 0;
    for (int i = 0; i < source; i++) {
        const char *name = mine ? editor->saved_names[i] : editor->game_maps[i];
        if (!mine && strncmp(name, "data\\global\\tiles\\", 18) == 0) {
            name += 18;
        }
        if (Editor__MatchesSearch(name, query)) {
            names[editor->dialog_visible_count]                  = name;
            editor->dialog_visible[editor->dialog_visible_count] = i;
            editor->dialog_visible_count++;
        }
    }
    ListBox_SetItems(editor->dialog_list, names, editor->dialog_visible_count);
    free(names);

    char count[64];
    snprintf(count, sizeof(count), mine ? "%d saved maps in your maps folder" : "%d game areas",
             editor->dialog_visible_count);
    Label_SetText(editor->dialog_labels[0], count);
}

static void Editor__UpdateNewButtons(SceneMapEditor *editor) {
    const LevelType *type = LevelTypes_Get(editor->new_type);
    Button_SetText(editor->new_type_button, type != NULL ? type->name : "No tile sets");
    char size[32];
    snprintf(size, sizeof(size), "%d x %d", map_sizes[editor->new_size], map_sizes[editor->new_size]);
    Button_SetText(editor->new_size_button, size);
}

static void Editor__OpenDialog(SceneMapEditor *editor, const EditorDialog dialog) {
    editor->dialog = dialog;
    InputManager_ResetMouseButtons();
    for (int i = 0; i < 3; i++) {
        Label_SetText(editor->dialog_labels[i], "");
    }
    switch (dialog) {
    case DIALOG_OPEN:
        Label_SetText(editor->dialog_title, "Open Map");
        Button_SetText(editor->dialog_ok, "Open");
        Button_SetText(editor->dialog_extra, "Reset TG");
        TestingGrounds_EnsureMapFile(); // the Testing Grounds map is one of yours
        Map_FreeNames(editor->saved_names, editor->saved_count);
        editor->saved_count = Map_ListSavedMaps(&editor->saved_names);
        TextBox_Clear(editor->search_box);
        TextBox_Focus(editor->search_box);
        Editor__RefreshOpenList(editor);
        break;
    case DIALOG_NEW:
        Label_SetText(editor->dialog_title, "New Map");
        Button_SetText(editor->dialog_ok, "Create");
        Button_SetText(editor->dialog_extra, "Arena");
        Label_SetText(editor->dialog_labels[0], "Tile set:");
        Label_SetText(editor->dialog_labels[1], "Size:");
        Label_SetText(editor->dialog_labels[2], "\"Arena\" starts from the default Testing Grounds map.");
        Editor__UpdateNewButtons(editor);
        break;
    case DIALOG_SAVE: {
        Label_SetText(editor->dialog_title, "Save Map");
        Button_SetText(editor->dialog_ok, "Save");
        Button_SetText(editor->dialog_extra, "As Testing");
        Label_SetText(editor->dialog_labels[0], "Name:");
        char folder[160];
        snprintf(folder, sizeof(folder), "Saved in %.140s", Map_GetSaveFolder());
        Label_SetText(editor->dialog_labels[1], folder);
        Label_SetText(editor->dialog_labels[2],
                      "\"As Testing\": the Testing Grounds map. \"As DS1\": the game's format.");
        TextBox_Clear(editor->name_box);
        TextBox_Focus(editor->name_box);
        InputManager_SetTextInput(editor->map->name); // start from the current name
        TextBox_Update(editor->name_box);
        break;
    }
    case DIALOG_NONE:
        break;
    }
}

static void Editor__CloseDialog(SceneMapEditor *editor) {
    editor->dialog = DIALOG_NONE;
    TextBox_Blur(editor->search_box);
    TextBox_Blur(editor->name_box);
    InputManager_ResetMouseButtons();
}

// File names: keep letters, digits, space, - and _
static void Editor__SafeFileName(const char *name, char *out, const size_t out_len) {
    size_t o = 0;
    for (const char *c = name; *c != '\0' && o + 1 < out_len; c++) {
        if (isalnum((unsigned char)*c) || *c == '-' || *c == '_' || *c == ' ') {
            out[o++] = *c;
        }
    }
    out[o] = '\0';
    if (o == 0) {
        snprintf(out, out_len, "untitled");
    }
}

static void Editor__Save(SceneMapEditor *editor, const char *file_name) {
    char safe[64];
    Editor__SafeFileName(file_name, safe, sizeof(safe));
    char path[4200];
    snprintf(path, sizeof(path), "%s/%s.abm", Map_GetSaveFolder(), safe);
    if (Map_SaveFile(editor->map, path)) {
        snprintf(editor->file_path, sizeof(editor->file_path), "%s", path);
        editor->dirty = false;
        Editor__ForgetRecovery();
        char message[128];
        snprintf(message, sizeof(message), "Saved %s.abm", safe);
        Editor__Message(editor, message);
    } else {
        Editor__Message(editor, "Could not save the map (see the log)");
    }
}

static void Editor__UpdateDialog(SceneMapEditor *editor) {
    const bool cancel = Button_Update(editor->dialog_cancel) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE);
    bool       ok     = Button_Update(editor->dialog_ok);

    switch (editor->dialog) {
    case DIALOG_OPEN: {
        if (TabBar_Update(editor->open_tabs, DIALOG_X + 20, DIALOG_Y + 40, DIALOG_W - 40)) {
            Editor__RefreshOpenList(editor);
        }
        if (TextBox_Update(editor->search_box)) {
            Editor__RefreshOpenList(editor);
        }
        ListBox_Update(editor->dialog_list);
        if (TabBar_GetSelected(editor->open_tabs) == OPEN_MY_MAPS && Button_Update(editor->dialog_extra)) {
            editor->question = QUESTION_RESET_TESTING;
            ConfirmDialog_Open(editor->confirm, "Reset Testing Grounds",
                               "Put the Testing Grounds map (testing_grounds) back to the original? Your changes to it "
                               "will be lost.",
                               "Reset", "Cancel", NULL);
            return;
        }
        ok            = ok || InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN);
        const int row = ListBox_GetSelectedIndex(editor->dialog_list);
        if (ok && row >= 0 && row < editor->dialog_visible_count) {
            const int index = editor->dialog_visible[row];
            Map      *map   = NULL;
            char      path[4200];
            LoadingScreen_Begin("Loading map...");
            if (TabBar_GetSelected(editor->open_tabs) == OPEN_MY_MAPS) {
                snprintf(path, sizeof(path), "%s/%s.abm", Map_GetSaveFolder(), editor->saved_names[index]);
                map = Map_LoadFile(path);
            } else {
                path[0] = '\0';
                map     = Map_LoadDS1(editor->game_maps[index]);
            }
            LoadingScreen_End();
            if (map != NULL) {
                Editor__SetMap(editor, map, path);
                Editor__CloseDialog(editor);
                Editor__Message(editor, "Map loaded");
            } else {
                Editor__Message(editor, "Could not load that map");
            }
            return;
        }
        break;
    }
    case DIALOG_NEW:
        if (Button_Update(editor->new_type_button) && LevelTypes_GetCount() > 0) {
            editor->new_type = (editor->new_type + 1) % LevelTypes_GetCount();
            Editor__UpdateNewButtons(editor);
        }
        if (Button_Update(editor->new_size_button)) {
            editor->new_size = (editor->new_size + 1) % MAP_SIZE_COUNT;
            Editor__UpdateNewButtons(editor);
        }
        if (Button_Update(editor->dialog_extra)) {
            Editor__SetMap(editor, TestingGrounds_CreateDefaultMap(), NULL);
            Editor__CloseDialog(editor);
            return;
        }
        if (ok) {
            Map *map = Editor__CreateBlank(editor->new_type, map_sizes[editor->new_size]);
            if (map != NULL) {
                Editor__SetMap(editor, map, NULL);
            }
            Editor__CloseDialog(editor);
            return;
        }
        break;
    case DIALOG_SAVE: {
        TextBox_Update(editor->name_box);
        const bool as_testing = Button_Update(editor->dialog_extra);
        if (Button_Update(editor->dialog_ds1)) {
            // The game's DS1, next to the .abm (the units without a preset number are left out)
            const char *name = TextBox_GetText(editor->name_box);
            char        safe[64];
            Editor__SafeFileName(name[0] != '\0' ? name : editor->map->name, safe, sizeof(safe));
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s.ds1", Map_GetSaveFolder(), safe);
            char message[128];
            snprintf(message, sizeof(message),
                     Map_SaveDS1(editor->map, path) ? "Saved %s.ds1" : "Could not save %s.ds1", safe);
            Editor__Message(editor, message);
            Editor__CloseDialog(editor);
            return;
        }
        ok = ok || InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN);
        if (ok || as_testing) {
            const char *name = TextBox_GetText(editor->name_box);
            if (!as_testing && name[0] != '\0') {
                snprintf(editor->map->name, sizeof(editor->map->name), "%s", name);
            }
            Editor__Save(editor, as_testing ? "testing_grounds" : editor->map->name);
            Editor__CloseDialog(editor);
            return;
        }
        break;
    }
    case DIALOG_NONE:
        break;
    }

    if (cancel) {
        Editor__CloseDialog(editor);
    }
}

static void Editor__DrawDialog(const SceneMapEditor *editor) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(sdl_renderer, NULL);
    const SDL_Rect box = {DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H};
    SDL_SetRenderDrawColor(sdl_renderer, 20, 18, 14, 245);
    SDL_RenderFillRect(sdl_renderer, &box);
    SDL_SetRenderDrawColor(sdl_renderer, 120, 100, 60, 255);
    SDL_RenderDrawRect(sdl_renderer, &box);

    Label_Draw(editor->dialog_title, DIALOG_X + (DIALOG_W / 2), DIALOG_Y + 20);

    switch (editor->dialog) {
    case DIALOG_OPEN:
        TabBar_Draw(editor->open_tabs, DIALOG_X + 20, DIALOG_Y + 40, DIALOG_W - 40);
        TextBox_Draw(editor->search_box);
        ListBox_Draw(editor->dialog_list);
        Label_Draw(editor->dialog_labels[0], DIALOG_X + 20, DIALOG_Y + 92 + (15 * LIST_ROW_H) + 6);
        Button_Draw(editor->dialog_ok);
        if (TabBar_GetSelected(editor->open_tabs) == OPEN_MY_MAPS) {
            Button_Draw(editor->dialog_extra); // Reset TG: the Testing Grounds map back to the original
        }
        break;
    case DIALOG_NEW:
        Label_Draw(editor->dialog_labels[0], DIALOG_X + 40, DIALOG_Y + 90);
        Label_Draw(editor->dialog_labels[1], DIALOG_X + 40, DIALOG_Y + 150);
        Label_Draw(editor->dialog_labels[2], DIALOG_X + 40, DIALOG_Y + 230);
        Button_Draw(editor->new_type_button);
        Button_Draw(editor->new_size_button);
        Button_Draw(editor->dialog_ok);
        Button_Draw(editor->dialog_extra);
        break;
    case DIALOG_SAVE:
        Label_Draw(editor->dialog_labels[0], DIALOG_X + 40, DIALOG_Y + 92);
        TextBox_Draw(editor->name_box);
        Label_Draw(editor->dialog_labels[1], DIALOG_X + 40, DIALOG_Y + 140);
        Label_Draw(editor->dialog_labels[2], DIALOG_X + 40, DIALOG_Y + 170);
        Button_Draw(editor->dialog_ok);
        Button_Draw(editor->dialog_extra);
        Button_Draw(editor->dialog_ds1);
        break;
    case DIALOG_NONE:
        break;
    }
    Button_Draw(editor->dialog_cancel);
}

// ---------------------------------------------------------------------------------------------------------------------
// Editing

static void Editor__Paint(SceneMapEditor *editor) {
    Map *map = editor->map;
    if (editor->tool == TOOL_UNITS) {
        CatalogCharacterInfo info;
        if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), editor->selected[TOOL_UNITS], &info)) {
            MapHistory_AddEntity(editor->history, map, info.spec->base, info.id, info.name,
                                 floorf(editor->hover_sx) + 0.5f, floorf(editor->hover_sy) + 0.5f);
            editor->dirty = true;
        }
        return;
    }

    const MapTileChoice *choice = Editor__SelectedChoice(editor);
    if (choice == NULL) {
        return;
    }
    const MapLayerKind kind  = Editor__LayerKind(editor->tool);
    int                layer = 0;
    if (kind == MAP_LAYER_WALL) {
        // Use the first free wall layer (a cell can hold a wall and, say, a roof); replace the top one if all are used
        layer = map->wall_layer_count - 1;
        for (int w = 0; w < map->wall_layer_count; w++) {
            const DS1Cell *cell = Map_GetCell(map, MAP_LAYER_WALL, w, editor->hover_tx, editor->hover_ty);
            if (cell->prop1 == 0 || (cell->orientation == choice->orientation && cell->style == choice->style &&
                                     cell->sequence == choice->sequence)) {
                layer = w;
                break;
            }
        }
    }
    const DS1Cell *cell = Map_GetCell(map, kind, layer, editor->hover_tx, editor->hover_ty);
    if (cell != NULL && cell->prop1 != 0 && cell->style == choice->style && cell->sequence == choice->sequence &&
        cell->orientation == choice->orientation) {
        return; // already there
    }
    MapHistory_SetCell(editor->history, map, kind, layer, editor->hover_tx, editor->hover_ty, choice);
    editor->dirty = true;
}

static void Editor__Erase(SceneMapEditor *editor) {
    Map *map = editor->map;
    if (editor->tool == TOOL_UNITS) {
        // The nearest unit within two sub-tiles
        int   nearest  = -1;
        float distance = 4.0f;
        for (int e = 0; e < map->entity_count; e++) {
            const float dx = map->entities[e].x - editor->hover_sx;
            const float dy = map->entities[e].y - editor->hover_sy;
            if ((dx * dx) + (dy * dy) < distance) {
                distance = (dx * dx) + (dy * dy);
                nearest  = e;
            }
        }
        if (nearest >= 0) {
            MapHistory_RemoveEntity(editor->history, map, nearest);
            editor->dirty = true;
        }
        return;
    }

    const MapLayerKind kind = Editor__LayerKind(editor->tool);
    if (kind == MAP_LAYER_WALL) {
        for (int w = map->wall_layer_count - 1; w >= 0; w--) {
            const DS1Cell *cell = Map_GetCell(map, MAP_LAYER_WALL, w, editor->hover_tx, editor->hover_ty);
            if (cell != NULL && cell->prop1 != 0) {
                MapHistory_SetCell(editor->history, map, MAP_LAYER_WALL, w, editor->hover_tx, editor->hover_ty, NULL);
                editor->dirty = true;
                return;
            }
        }
        return;
    }
    const DS1Cell *cell = Map_GetCell(map, kind, 0, editor->hover_tx, editor->hover_ty);
    if (cell != NULL && cell->prop1 != 0) {
        MapHistory_SetCell(editor->history, map, kind, 0, editor->hover_tx, editor->hover_ty, NULL);
        editor->dirty = true;
    }
}

static void Editor__UpdateMapView(SceneMapEditor *editor, const uint64_t elapsed) {
    const SDL_Rect view = {0, 0, VIEW_W, VIEW_H};
    int            mouse_x;
    int            mouse_y;
    bool           left;
    bool           middle;
    bool           right;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, &middle, &right);
    const bool over_view = Editor__Inside(mouse_x, mouse_y, view.x, view.y, view.w, view.h);

    // Zoom with the wheel around the mouse pointer
    if (over_view) {
        const int wheel = InputManager_ConsumeMouseWheel();
        if (wheel != 0) {
            float before_x;
            float before_y;
            MapView_ScreenToWorld(&editor->camera, &view, mouse_x, mouse_y, &before_x, &before_y);
            editor->zoom_index += wheel > 0 ? 1 : -1;
            editor->zoom_index =
                editor->zoom_index < 0 ? 0 : (editor->zoom_index >= ZOOM_COUNT ? ZOOM_COUNT - 1 : editor->zoom_index);
            editor->camera.zoom = zoom_levels[editor->zoom_index];
            float after_x;
            float after_y;
            MapView_ScreenToWorld(&editor->camera, &view, mouse_x, mouse_y, &after_x, &after_y);
            editor->camera.x += before_x - after_x;
            editor->camera.y += before_y - after_y;
        }
    }

    // Pan: middle drag, arrow keys or WASD
    if (middle && !editor->panning && over_view) {
        editor->panning      = true;
        editor->pan_mouse_x  = mouse_x;
        editor->pan_mouse_y  = mouse_y;
        editor->pan_camera_x = editor->camera.x;
        editor->pan_camera_y = editor->camera.y;
    }
    if (editor->panning) {
        if (!middle) {
            editor->panning = false;
        } else {
            editor->camera.x = editor->pan_camera_x - ((float)(mouse_x - editor->pan_mouse_x) / editor->camera.zoom);
            editor->camera.y = editor->pan_camera_y - ((float)(mouse_y - editor->pan_mouse_y) / editor->camera.zoom);
        }
    }
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    const float  step = PAN_SPEED * (float)elapsed / editor->camera.zoom;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
        editor->camera.x -= step;
    }
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
        editor->camera.x += step;
    }
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
        editor->camera.y -= step;
    }
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
        editor->camera.y += step;
    }

    // Toggles
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_G)) {
        editor->draw_flags ^= MAP_DRAW_GRID;
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_H)) {
        editor->draw_flags ^= MAP_DRAW_WALLS;
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_B)) {
        editor->draw_flags ^= MAP_DRAW_BLOCKED;
    }
    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_R)) {
        editor->draw_flags ^= MAP_DRAW_ROOFS;
    }

    // What's under the mouse
    float world_x;
    float world_y;
    MapView_ScreenToWorld(&editor->camera, &view, mouse_x, mouse_y, &world_x, &world_y);
    MapView_WorldToSubtile(world_x, world_y, &editor->hover_sx, &editor->hover_sy);
    editor->hover_tx    = (int)floorf(editor->hover_sx / MAP_SUBTILES);
    editor->hover_ty    = (int)floorf(editor->hover_sy / MAP_SUBTILES);
    editor->hover_valid = over_view && editor->hover_tx >= 0 && editor->hover_ty >= 0 &&
                          editor->hover_tx < editor->map->width && editor->hover_ty < editor->map->height;

    // Paint / erase (tiles while held, units once per click); a stroke, press to release, is one change
    if ((left && !editor->prev_left) || (right && !editor->prev_right)) {
        if (!editor->stroke && editor->hover_valid && !editor->panning) {
            MapHistory_Begin(editor->history);
            editor->stroke = true;
        }
    }
    if (editor->stroke && !left && !right) {
        editor->stroke = false;
        Editor__EndChange(editor);
    }
    if (editor->hover_valid && !editor->panning) {
        const bool continuous = editor->tool != TOOL_UNITS;
        if (left && (continuous || !editor->prev_left)) {
            Editor__Paint(editor);
        }
        if (right && (continuous || !editor->prev_right)) {
            Editor__Erase(editor);
        }
    }
    editor->prev_left  = left;
    editor->prev_right = right;
}

// ---------------------------------------------------------------------------------------------------------------------
// Update / render

static void Editor__Leave(void) {
    Editor__ForgetRecovery();       // leaving on purpose: nothing to offer back
    Scene_Set(SCENE_REF(MainMenu)); // the main menu releases the session (bestiary) once we're gone
}

// The answer to the confirm dialog's question
static void Editor__UpdateQuestion(SceneMapEditor *editor) {
    const ConfirmAnswer answer = ConfirmDialog_Update(editor->confirm);
    if (answer == CONFIRM_NONE) {
        return;
    }
    const EditorQuestion question = editor->question;
    editor->question              = QUESTION_NONE;
    if (question == QUESTION_RESET_TESTING) {
        if (answer == CONFIRM_FIRST) {
            char path[4200];
            TestingGrounds_MapPath(path, sizeof(path));
            if (!TestingGrounds_ResetMapFile()) {
                Editor__Message(editor, "Could not reset the Testing Grounds map");
                return;
            }
            if (strcmp(editor->file_path, path) == 0) { // it's the one open: open the original
                Map *map = Map_LoadFile(path);
                if (map != NULL) {
                    Editor__SetMap(editor, map, path);
                }
            }
            Editor__Message(editor, "Testing Grounds map reset to the original");
        }
        return;
    }
    if (question == QUESTION_RESTORE) {
        char path[4200];
        Editor__RecoveryPath(path, sizeof(path));
        if (answer == CONFIRM_FIRST) {
            LoadingScreen_Begin("Loading map...");
            Map *map = Map_LoadFile(path);
            LoadingScreen_End();
            if (map != NULL) {
                Editor__SetMap(editor, map, NULL);
                editor->dirty = true;
                Editor__Message(editor, "Unsaved changes restored (Save to keep them)");
            } else {
                Editor__Message(editor, "Could not read the recovery file");
            }
        } else {
            Editor__ForgetRecovery();
        }
        return;
    }
    // Leaving with unsaved changes: Save (where it was saved, or ask for a name), Leave, Cancel
    if (editor->dirty && answer == CONFIRM_FIRST) {
        if (editor->file_path[0] != '\0' && Map_SaveFile(editor->map, editor->file_path)) {
            Editor__Leave();
        } else {
            Editor__OpenDialog(editor, DIALOG_SAVE);
            Editor__Message(editor, "Name the map to save it, then press Back again");
        }
    } else if ((editor->dirty && answer == CONFIRM_SECOND) || (!editor->dirty && answer == CONFIRM_FIRST)) {
        Editor__Leave();
    }
}

void MapEditor_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneMapEditor *editor  = scene_ref;
    const uint64_t  now     = SDL_GetTicks64();
    const uint64_t  elapsed = now - editor->last_tick;
    editor->last_tick       = now;

    if (ConfirmDialog_IsOpen(editor->confirm)) {
        Editor__UpdateQuestion(editor);
        return;
    }
    if (editor->dialog != DIALOG_NONE) {
        Editor__UpdateDialog(editor);
        return;
    }

    // Undo / redo: Ctrl+Z, Ctrl+Y (or Ctrl+Shift+Z)
    const bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    if (ctrl && InputManager_ConsumeKeyPress(SDL_SCANCODE_Z)) {
        Editor__Undo(editor, (SDL_GetModState() & KMOD_SHIFT) != 0);
    } else if (ctrl && InputManager_ConsumeKeyPress(SDL_SCANCODE_Y)) {
        Editor__Undo(editor, true);
    }

    // Auto-save (Options › Gameplay › Debug): into the map's file, or the recovery file for a map never saved
    const int minutes = Settings_GetDebug()->editor_autosave_minutes;
    if (minutes > 0 && now - editor->last_autosave >= (uint64_t)minutes * 60000u) {
        editor->last_autosave = now;
        if (editor->dirty && !editor->stroke) {
            if (editor->file_path[0] != '\0' && Map_SaveFile(editor->map, editor->file_path)) {
                editor->dirty = false;
                Editor__ForgetRecovery();
                Editor__Message(editor, "Auto-saved");
            } else {
                char path[4200];
                Editor__RecoveryPath(path, sizeof(path));
                if (Map_SaveFile(editor->map, path)) {
                    Editor__Message(editor, "Auto-saved to the recovery file (Save to give it a name)");
                }
            }
        }
    }

    Editor__UpdateMapView(editor, elapsed);

    if (TabBar_Update(editor->tool_bar, PANEL_INNER_X, TOOLS_Y, PANEL_INNER_W)) {
        editor->tool = (EditorTool)TabBar_GetSelected(editor->tool_bar);
        Editor__RefreshList(editor);
    }
    if (editor->tool == TOOL_UNITS &&
        CatalogCharacters_UpdateCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W)) {
        editor->selected[TOOL_UNITS] = 0;
        Editor__RefreshList(editor);
    }
    if (ListBox_Update(editor->list)) {
        editor->selected[editor->tool] = ListBox_GetSelectedIndex(editor->list);
    }

    if (Button_Update(editor->fill_button)) {
        const MapTileChoice *choice = editor->tool == TOOL_FLOOR ? Editor__SelectedChoice(editor) : NULL;
        if (choice != NULL) {
            MapHistory_Begin(editor->history); // the whole fill is one change
            for (int ty = 0; ty < editor->map->height; ty++) {
                for (int tx = 0; tx < editor->map->width; tx++) {
                    MapHistory_SetCell(editor->history, editor->map, MAP_LAYER_FLOOR, 0, tx, ty, choice);
                }
            }
            Editor__EndChange(editor);
            editor->dirty = true;
        }
    }

    if (Button_Update(editor->new_button)) {
        Editor__OpenDialog(editor, DIALOG_NEW);
    } else if (Button_Update(editor->open_button)) {
        Editor__OpenDialog(editor, DIALOG_OPEN);
    } else if (Button_Update(editor->save_button)) {
        Editor__OpenDialog(editor, DIALOG_SAVE);
    } else if (Button_Update(editor->play_button)) {
        MapSession_ParkMap(editor->map, true);
        Scene_Set(SCENE_REF(TestingGrounds));
    } else if (Button_Update(editor->back_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        editor->question = QUESTION_LEAVE;
        if (editor->dirty) {
            ConfirmDialog_Open(editor->confirm, "Unsaved Changes",
                               "You have changes that aren't saved. If you leave now, you'll lose them.", "Save",
                               "Leave", "Cancel");
        } else {
            ConfirmDialog_Open(editor->confirm, "Main Menu", "Are you sure you wish to return to the main menu?", "Yes",
                               "No", NULL);
        }
    }
}

static void Editor__DrawPanel(SceneMapEditor *editor) {
    const SDL_Rect panel = {PANEL_X, 0, 800 - PANEL_X, 600};
    SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
    SDL_RenderDrawLine(sdl_renderer, PANEL_X, 0, PANEL_X, 600);

    Label_Draw(editor->title_label, PANEL_X + ((800 - PANEL_X) / 2), 16);
    TabBar_Draw(editor->tool_bar, PANEL_INNER_X, TOOLS_Y, PANEL_INNER_W);

    // Preview of the selected tile or unit
    const SDL_Rect preview = {PANEL_INNER_X, PREVIEW_Y, PANEL_INNER_W, PREVIEW_H};
    SDL_SetRenderDrawColor(sdl_renderer, 50, 48, 52, 255);
    SDL_RenderFillRect(sdl_renderer, &preview);
    if (editor->tool == TOOL_UNITS) {
        CatalogCharacterInfo info;
        if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), editor->selected[TOOL_UNITS], &info)) {
            Figure *figure = MapUnits_GetFigure(editor->units, info.spec->base, info.id, info.idle_mode);
            if (figure != NULL) {
                int xmin;
                int ymin;
                int xmax;
                int ymax;
                Figure_GetBounds(figure, &xmin, &ymin, &xmax, &ymax);
                const int   h = ymax - ymin > 0 ? ymax - ymin : 1;
                const float scale =
                    (float)(PREVIEW_H - 10) / (float)h < 1.0f ? (float)(PREVIEW_H - 10) / (float)h : 1.0f;
                const int frames = Figure_GetFramesPerDirection(figure);
                const int ms     = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
                const int frame  = frames > 0 ? (int)((SDL_GetTicks64() / (uint64_t)ms) % (uint64_t)frames) : 0;
                SDL_RenderSetClipRect(sdl_renderer, &preview);
                Figure_Draw(figure, MapUnits_Direction(figure, 0), frame,
                            preview.x + (preview.w / 2) - (int)((float)(xmin + ((xmax - xmin) / 2)) * scale),
                            preview.y + 5 - (int)((float)ymin * scale), scale);
                SDL_RenderSetClipRect(sdl_renderer, NULL);
            }
        }
        CatalogCharacters_DrawCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W);
    } else {
        const MapTileChoice *choice = Editor__SelectedChoice(editor);
        if (choice != NULL) {
            const SDL_Rect box = {preview.x + 4, preview.y + 4, preview.w - 8, preview.h - 8};
            MapRenderer_DrawTilePreview(editor->renderer, editor->map, choice->preview, &box);
        }
    }

    ListBox_Draw(editor->list);

    // Info lines
    const int info_y = Editor__ListY(editor) + (LIST_ROWS * LIST_ROW_H) + 6;
    char      text[128];
    snprintf(text, sizeof(text), "%s  %dx%d  act %d", editor->dirty ? "* unsaved" : "", editor->map->width,
             editor->map->height, editor->map->act + 1);
    Label_SetText(editor->info_labels[0], text);
    snprintf(text, sizeof(text), "%d tile sets, %d units", editor->map->dt1_count, editor->map->entity_count);
    Label_SetText(editor->info_labels[1], text);
    snprintf(text, sizeof(text), "Changes: %d / %d  (Ctrl+Z / Ctrl+Y)", MapHistory_GetPosition(editor->history),
             MapHistory_GetCount(editor->history));
    Label_SetText(editor->info_labels[2], text);
    for (int i = 0; i < 3; i++) {
        Label_Draw(editor->info_labels[i], PANEL_INNER_X, info_y + (i * 12));
    }

    if (editor->tool == TOOL_FLOOR) {
        Button_Draw(editor->fill_button);
    }
    Button_Draw(editor->back_button);
}

void MapEditor_Render(void *scene_ref) {
    SceneMapEditor *editor = scene_ref;
    const SDL_Rect  view   = {0, 0, VIEW_W, VIEW_H};

    SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 14, 255);
    SDL_RenderClear(sdl_renderer);

    MapDrawable drawables[MAX_DRAWABLES];
    const int   count = MapUnits_BuildDrawables(editor->units, editor->map, SDL_GetTicks64(), drawables, MAX_DRAWABLES);
    MapRenderer_Draw(editor->renderer, editor->map, &editor->camera, &view, drawables, count, editor->draw_flags);

    // Cursor: the tile (or sub-tile for units) that will be painted
    if (editor->hover_valid && editor->dialog == DIALOG_NONE) {
        SDL_RenderSetClipRect(sdl_renderer, &view);
        const SDL_Color highlight = {255, 230, 120, 220};
        MapRenderer_DrawTileOutline(&editor->camera, &view, editor->hover_tx, editor->hover_ty, highlight);
        if (editor->tool == TOOL_UNITS) {
            float wx;
            float wy;
            MapView_SubtileToWorld(floorf(editor->hover_sx) + 0.5f, floorf(editor->hover_sy) + 0.5f, &wx, &wy);
            int sx;
            int sy;
            MapView_WorldToScreen(&editor->camera, &view, wx, wy, &sx, &sy);
            const SDL_Rect dot = {sx - 3, sy - 2, 6, 4};
            SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 120, 255);
            SDL_RenderFillRect(sdl_renderer, &dot);
        }
        SDL_RenderSetClipRect(sdl_renderer, NULL);
    }

    // Status bar
    const SDL_Rect bar = {0, 0, VIEW_W, 14};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &bar);
    char status[200];
    if (editor->hover_valid) {
        snprintf(status, sizeof(status), "%s   tile %d,%d   sub-tile %d,%d   zoom %gx", editor->map->name,
                 editor->hover_tx, editor->hover_ty, (int)floorf(editor->hover_sx), (int)floorf(editor->hover_sy),
                 (double)editor->camera.zoom);
    } else {
        snprintf(status, sizeof(status), "%s   zoom %gx", editor->map->name, (double)editor->camera.zoom);
    }
    Label_SetText(editor->status_label, status);
    Label_Draw(editor->status_label, 4, 2);
    const SDL_Rect help_bar = {0, VIEW_H - 14, VIEW_W, 14};
    SDL_RenderFillRect(sdl_renderer, &help_bar);
    Label_Draw(editor->help_label, 4, VIEW_H - 12);

    // File buttons
    const SDL_Rect bottom = {0, VIEW_H, VIEW_W, 600 - VIEW_H};
    SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
    SDL_RenderFillRect(sdl_renderer, &bottom);
    Button_Draw(editor->new_button);
    Button_Draw(editor->open_button);
    Button_Draw(editor->save_button);
    Button_Draw(editor->play_button);

    Editor__DrawPanel(editor);

    if (SDL_GetTicks64() < editor->message_until) {
        Label_Draw(editor->message_label, VIEW_W / 2, 40);
    }

    if (editor->dialog != DIALOG_NONE) {
        Editor__DrawDialog(editor);
    }
    ConfirmDialog_Draw(editor->confirm);
}
