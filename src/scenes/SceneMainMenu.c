#include "SceneMainMenu.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/Revision.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../managers/AudioManager.h"
#include "../managers/InputManager.h"
#include "../map/MapSession.h"
#include "SceneCatalog.h"
#include "SceneCharacterSelect.h"
#include "SceneFreeRoam.h"
#include "SceneMapEditor.h"
#include "SceneTestingGrounds.h"
#include <stdlib.h>
#include <string.h>

typedef struct SceneMainMenu {
    Sprite *background_sprite;
    Sprite *d2logo_black_left_sprite;
    Sprite *d2logo_black_right_sprite;
    Sprite *d2logo_fire_left_sprite;
    Sprite *d2logo_fire_right_sprite;
    Label  *disclaimer_label;
    Label  *copyright_label;
    Label  *build_label;
    Button *single_player_button;
    Button *multiplayer_button;
    Button *catalog_button;
    Button *map_editor_button;
    // "Map Editor" submenu, shown in place of the main buttons
    bool    map_menu_open;
    Button *editor_button;
    Button *testing_grounds_button;
    Button *free_roam_button;
    Button *map_back_button;
    Button *credits_button;
    Button *cinematics_button;
    Button *exit_button;
} SceneMainMenu;

static void MainMenu__OnExitClicked(void *user_data);
static void MainMenu__OnSinglePlayerClicked(void *user_data);
static void MainMenu__OnCatalogClicked(void *user_data);
static void MainMenu__OnMapEditorClicked(void *user_data);
static void MainMenu__OnEditorClicked(void *user_data);
static void MainMenu__OnTestingGroundsClicked(void *user_data);
static void MainMenu__OnFreeRoamClicked(void *user_data);
static void MainMenu__OnMapBackClicked(void *user_data);

DEFINE_SCENE_CALLBACKS(MainMenu);

void *MainMenu_Create(void) {
    Cursor_SetVisible(true);

    MapSession_Release(); // back from the map editor / testing grounds: free what they shared

    AudioManager_PlayMusic(MUSIC_TITLE, true);

    SceneMainMenu *result = malloc(sizeof(SceneMainMenu));
    FAIL_IF_NULL(result);
    memset(result, 0, sizeof(SceneMainMenu));

    result->background_sprite         = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    result->d2logo_black_left_sprite  = Sprite_Create(D2LOGO_BLACK_LEFT, PALETTE_UNITS);
    result->d2logo_black_right_sprite = Sprite_Create(D2LOGO_BLACK_RIGHT, PALETTE_UNITS);
    result->d2logo_fire_left_sprite   = Sprite_Create(D2LOGO_FIRE_LEFT, PALETTE_UNITS);
    result->d2logo_fire_right_sprite  = Sprite_Create(D2LOGO_FIRE_RIGHT, PALETTE_UNITS);

    Sprite_SetBlendMode(result->d2logo_black_left_sprite, SDL_BLENDMODE_BLEND);
    Sprite_SetBlendMode(result->d2logo_black_right_sprite, SDL_BLENDMODE_BLEND);
    Sprite_SetBlendMode(result->d2logo_fire_left_sprite, SDL_BLENDMODE_ADD);
    Sprite_SetBlendMode(result->d2logo_fire_right_sprite, SDL_BLENDMODE_ADD);

    result->disclaimer_label = Label_Create(FONTS_FONTFORMAL10, PALETTE_SKY);
    Label_SetText(result->disclaimer_label, ABYSS_TEXT_MAIN_MENU_DISCLAIMER);
    Label_SetColor(result->disclaimer_label, 0xFF, 0xFF, 0x8C, 0xFF);
    Label_SetAlignment(result->disclaimer_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    result->copyright_label = Label_Create(FONTS_FONTFORMAL10, PALETTE_SKY);
    Label_SetText(result->copyright_label, ABYSS_COPYRIGHT);
    Label_SetColor(result->copyright_label, 0xFF, 0xFF, 0x8C, 0xFF);
    Label_SetAlignment(result->copyright_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    // Button positions match the original 800x600 main menu.
    result->single_player_button = Button_Create(BUTTON_TYPE_WIDE, "Single Player");
    Button_SetPosition(result->single_player_button, 264, 290);
    Button_SetClickHandler(result->single_player_button, MainMenu__OnSinglePlayerClicked, NULL);

    result->multiplayer_button = Button_Create(BUTTON_TYPE_WIDE, "Multiplayer");
    Button_SetPosition(result->multiplayer_button, 264, 330);

    result->catalog_button = Button_Create(BUTTON_TYPE_WIDE, "Catalog");
    Button_SetPosition(result->catalog_button, 264, 380);
    Button_SetClickHandler(result->catalog_button, MainMenu__OnCatalogClicked, NULL);
#ifndef ABYSS_DEBUG_SHOW_CATALOG
    Button_SetVisible(result->catalog_button, false); // CMake option DEBUG_SHOW_CATALOG is off
#endif

    result->map_editor_button = Button_Create(BUTTON_TYPE_WIDE, "Map Editor");
    Button_SetPosition(result->map_editor_button, 264, 420);
    Button_SetClickHandler(result->map_editor_button, MainMenu__OnMapEditorClicked, result);
#ifndef ABYSS_DEBUG_SHOW_MAP_EDITOR
    Button_SetVisible(result->map_editor_button, false); // CMake option DEBUG_SHOW_MAP_EDITOR is off
#endif

    result->editor_button = Button_Create(BUTTON_TYPE_WIDE, "Map Editor");
    Button_SetPosition(result->editor_button, 264, 290);
    Button_SetClickHandler(result->editor_button, MainMenu__OnEditorClicked, NULL);
    result->testing_grounds_button = Button_Create(BUTTON_TYPE_WIDE, "Testing Grounds");
    Button_SetPosition(result->testing_grounds_button, 264, 330);
    Button_SetClickHandler(result->testing_grounds_button, MainMenu__OnTestingGroundsClicked, NULL);
    result->free_roam_button = Button_Create(BUTTON_TYPE_WIDE, "Free Roam");
    Button_SetPosition(result->free_roam_button, 264, 370);
    Button_SetClickHandler(result->free_roam_button, MainMenu__OnFreeRoamClicked, NULL);
    result->map_back_button = Button_Create(BUTTON_TYPE_WIDE, "Back");
    Button_SetPosition(result->map_back_button, 264, 420);
    Button_SetClickHandler(result->map_back_button, MainMenu__OnMapBackClicked, result);

    result->credits_button = Button_Create(BUTTON_TYPE_SHORT, "Credits");
    Button_SetPosition(result->credits_button, 264, 505);

    result->cinematics_button = Button_Create(BUTTON_TYPE_SHORT, "Cinematics");
    Button_SetPosition(result->cinematics_button, 401, 505);

    result->exit_button = Button_Create(BUTTON_TYPE_WIDE, "Exit Diablo II");
    Button_SetPosition(result->exit_button, 264, 535);
    Button_SetClickHandler(result->exit_button, MainMenu__OnExitClicked, NULL);

    // Not implemented yet: shown, but greyed out.
    Button_SetEnabled(result->multiplayer_button, false);
    Button_SetEnabled(result->credits_button, false);
    Button_SetEnabled(result->cinematics_button, false);

    result->build_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetText(result->build_label, Revision_GetFullVersion());
    Label_SetColor(result->build_label, 0xFF, 0xFF, 0xFF, 0xFF);
    Label_SetAlignment(result->build_label, LABEL_ALIGN_END, LABEL_ALIGN_BEGIN);
    return result;
}

void MainMenu_Render(void *scene_ref) {
    const SceneMainMenu *mainmenu = (SceneMainMenu *)scene_ref;

    Sprite_DrawMulti(mainmenu->background_sprite, 0, 0, 0, 4, 3);
    Sprite_DrawAnimated(mainmenu->d2logo_black_left_sprite, 400, 120);
    Sprite_DrawAnimated(mainmenu->d2logo_black_right_sprite, 400, 120);
    Sprite_DrawAnimated(mainmenu->d2logo_fire_left_sprite, 400, 120);
    Sprite_DrawAnimated(mainmenu->d2logo_fire_right_sprite, 400, 120);

    Label_Draw(mainmenu->copyright_label, 400, 578);
    Label_Draw(mainmenu->disclaimer_label, 400, 590);
    Label_Draw(mainmenu->build_label, 797, 1);

    if (mainmenu->map_menu_open) {
        Button_Draw(mainmenu->editor_button);
        Button_Draw(mainmenu->testing_grounds_button);
        Button_Draw(mainmenu->free_roam_button);
        Button_Draw(mainmenu->map_back_button);
    } else {
        Button_Draw(mainmenu->single_player_button);
        Button_Draw(mainmenu->multiplayer_button);
        Button_Draw(mainmenu->catalog_button);
        Button_Draw(mainmenu->map_editor_button);
    }
    Button_Draw(mainmenu->credits_button);
    Button_Draw(mainmenu->cinematics_button);
    Button_Draw(mainmenu->exit_button);
}

void MainMenu_Update(void *scene_ref, const uint64_t delta) {
    (void)(delta);
    const SceneMainMenu *mainmenu = (SceneMainMenu *)scene_ref;

    if (mainmenu->map_menu_open) {
        Button_Update(mainmenu->editor_button);
        Button_Update(mainmenu->testing_grounds_button);
        Button_Update(mainmenu->free_roam_button);
        Button_Update(mainmenu->map_back_button);
    } else {
        Button_Update(mainmenu->single_player_button);
        Button_Update(mainmenu->multiplayer_button);
        Button_Update(mainmenu->catalog_button);
        Button_Update(mainmenu->map_editor_button);
    }
    Button_Update(mainmenu->credits_button);
    Button_Update(mainmenu->cinematics_button);
    Button_Update(mainmenu->exit_button);
}

static void MainMenu__OnSinglePlayerClicked(void *user_data) {
    (void)(user_data);
    Scene_Set(SCENE_REF(CharacterSelect));
}

static void MainMenu__OnCatalogClicked(void *user_data) {
    (void)(user_data);
    Scene_Set(SCENE_REF(Catalog));
}

static void MainMenu__OnMapEditorClicked(void *user_data) {
    SceneMainMenu *mainmenu = user_data;
    mainmenu->map_menu_open = true;
    InputManager_ResetMouseButtons(); // don't let this click also press the button that replaces it
}

static void MainMenu__OnMapBackClicked(void *user_data) {
    SceneMainMenu *mainmenu = user_data;
    mainmenu->map_menu_open = false;
    InputManager_ResetMouseButtons();
}

static void MainMenu__OnEditorClicked(void *user_data) {
    (void)(user_data);
    Scene_Set(SCENE_REF(MapEditor));
}

static void MainMenu__OnTestingGroundsClicked(void *user_data) {
    (void)(user_data);
    Scene_Set(SCENE_REF(TestingGrounds));
}

static void MainMenu__OnFreeRoamClicked(void *user_data) {
    (void)(user_data);
    Scene_Set(SCENE_REF(FreeRoam));
}

static void MainMenu__OnExitClicked(void *user_data) {
    (void)(user_data);
    LOG_INFO("Exit requested from the main menu.");
    running = false;
}

void MainMenu_Free(void **scene_ref) {
    SceneMainMenu *mainmenu = *((SceneMainMenu **)scene_ref);

    Button_Destroy(&mainmenu->single_player_button);
    Button_Destroy(&mainmenu->multiplayer_button);
    Button_Destroy(&mainmenu->catalog_button);
    Button_Destroy(&mainmenu->map_editor_button);
    Button_Destroy(&mainmenu->editor_button);
    Button_Destroy(&mainmenu->testing_grounds_button);
    Button_Destroy(&mainmenu->free_roam_button);
    Button_Destroy(&mainmenu->map_back_button);
    Button_Destroy(&mainmenu->credits_button);
    Button_Destroy(&mainmenu->cinematics_button);
    Button_Destroy(&mainmenu->exit_button);
    Label_Destroy(&mainmenu->copyright_label);
    Label_Destroy(&mainmenu->disclaimer_label);
    Label_Destroy(&mainmenu->build_label);
    Sprite_Destroy(&mainmenu->d2logo_black_left_sprite);
    Sprite_Destroy(&mainmenu->d2logo_black_right_sprite);
    Sprite_Destroy(&mainmenu->d2logo_fire_left_sprite);
    Sprite_Destroy(&mainmenu->d2logo_fire_right_sprite);
    Sprite_Destroy(&mainmenu->background_sprite);

    free(*scene_ref);
    *scene_ref = NULL;
}
