#include "SceneCharacterSelect.h"
#include "../audio/Sounds.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Character.h"
#include "../managers/InputManager.h"
#include "../map/MapUnits.h"
#include "../net/Multiplayer.h"
#include "SceneCharacterCreate.h"
#include "SceneGame.h"
#include "SceneMainMenu.h"
#include "SceneMultiplayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Single Player character list: up to eight saved characters on screen (two columns of four, scroll for more),
// each standing in its slot. Create New Character, Delete Character, Exit, OK (or double click) to play.

#define SELECT_BACKGROUND "/data/global/ui/CharSelect/characterselectscreenEXP.dc6"
#define SELECT_BOX        "/data/global/ui/CharSelect/charselectbox.DC6"
#define SELECT_POPUP      "/data/global/ui/FrontEnd/PopUpOKCancel.DC6"
#define SLOT_COLUMNS      2
#define SLOT_ROWS         4
#define SLOT_COUNT        (SLOT_COLUMNS * SLOT_ROWS)
#define SLOT_X0           40
#define SLOT_X1           318
#define SLOT_Y0           88
#define SLOT_STEP_Y       92
#define SLOT_W            272
#define SLOT_H            93
#define DOUBLE_CLICK_MS   400

typedef struct Slot {
    Figure *figure; // standing in town
    Label  *name_label;
    Label  *class_label;
    Label  *extra_label; // "Expansion Character", "Hardcore", "Dead"
} Slot;

typedef struct SceneCharacterSelect {
    Sprite    *background;
    Sprite    *box;
    Sprite    *popup;
    Character *characters;
    int        count;
    int        selected; // index into characters, -1 = none
    int        scroll;   // first row shown
    Slot       slots[SLOT_COUNT];
    int        slots_for_scroll; // the scroll position the slots were built for (-1 = not built)
    Label     *title_label;
    Label     *popup_label;
    Label     *empty_label;
    Label     *popup_note;
    Button    *create_button;
    Button    *delete_button;
    Button    *exit_button;
    Button    *ok_button;
    Button    *popup_yes;
    Button    *popup_no;
    Button    *scroll_up;
    Button    *scroll_down;
    bool       confirming_delete;
    // Multiplayer: the game's characters (yours on a dedicated server, or your own checked against its rules)
    bool     online;
    bool     waiting; // picked: the server is deciding
    int      list_version;
    char     status[160];
    Label   *status_label;
    Label   *game_label;
    bool     prev_left;
    uint64_t last_click;
    int      last_click_index;
} SceneCharacterSelect;

DEFINE_SCENE_CALLBACKS(CharacterSelect);

// ---------------------------------------------------------------------------------------------------------------------

static void CharacterSelect__FreeSlots(SceneCharacterSelect *scene) {
    for (int s = 0; s < SLOT_COUNT; s++) {
        Figure_Destroy(&scene->slots[s].figure);
        Label_Destroy(&scene->slots[s].name_label);
        Label_Destroy(&scene->slots[s].class_label);
        Label_Destroy(&scene->slots[s].extra_label);
    }
    scene->slots_for_scroll = -1;
}

// Multiplayer: whether this game's rules let the character in (why not, for the player)
static bool CharacterSelect__Allowed(const SceneCharacterSelect *scene, const Character *character, const char **why) {
    if (!scene->online) {
        if (why != NULL) {
            *why = NULL;
        }
        return true;
    }
    return NetSettings_Allows(NetClient_Settings(), character, NetClient_Realm(), why);
}

// Figures and labels for the characters on screen
static void CharacterSelect__BuildSlots(SceneCharacterSelect *scene) {
    CharacterSelect__FreeSlots(scene);
    scene->slots_for_scroll = scene->scroll;
    for (int s = 0; s < SLOT_COUNT; s++) {
        const int index = (scene->scroll * SLOT_COLUMNS) + s;
        if (index >= scene->count) {
            break;
        }
        const Character *character = &scene->characters[index];
        Slot            *slot      = &scene->slots[s];

        FigureSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.base = "chars";
        snprintf(spec.token, sizeof(spec.token), "%s", Character_ClassToken(character->class_id));
        snprintf(spec.weapon_class, sizeof(spec.weapon_class), "hth");
        for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
            if (c != COF_COMPONENT_RH && c != COF_COMPONENT_LH && c != COF_COMPONENT_SH) {
                snprintf(spec.armor[c], sizeof(spec.armor[c]), "lit");
            }
        }
        slot->figure = Figure_Create(&spec, "tn", PALETTE_UNITS);
        if (slot->figure == NULL) {
            slot->figure = Figure_Create(&spec, "nu", PALETTE_UNITS);
        }

        slot->name_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetText(slot->name_label, character->name);
        Label_SetColor(slot->name_label, 0xC7, 0xB3, 0x77, 0xFF);

        char text[64];
        snprintf(text, sizeof(text), "Level %d %s", character->level, Character_ClassName(character->class_id));
        slot->class_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetText(slot->class_label, text);

        slot->extra_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        if (!CharacterSelect__Allowed(scene, character, NULL) && !character->dead) {
            Label_SetText(slot->extra_label, "Not allowed in this game");
            Label_SetColor(slot->extra_label, 0xFF, 0x60, 0x60, 0xFF);
        } else if (character->dead) {
            Label_SetText(slot->extra_label, "Dead");
            Label_SetColor(slot->extra_label, 0xFF, 0x40, 0x40, 0xFF);
        } else if (character->hardcore) {
            Label_SetText(slot->extra_label, character->expansion ? "Hardcore Expansion Character" : "Hardcore");
            Label_SetColor(slot->extra_label, 0xFF, 0x60, 0x60, 0xFF);
        } else if (character->expansion) {
            Label_SetText(slot->extra_label, "Expansion Character");
            Label_SetColor(slot->extra_label, 0x40, 0xFF, 0x40, 0xFF);
        } else {
            Label_SetText(slot->extra_label, "");
        }
    }
}

static void CharacterSelect__Reload(SceneCharacterSelect *scene) {
    free(scene->characters);
    scene->characters = NULL;
    if (scene->online && NetClient_ServerKeepsCharacters()) {
        const Character *list  = NULL;
        const int        count = NetClient_Characters(&list);
        scene->characters      = calloc((size_t)(count > 0 ? count : 1), sizeof(Character));
        FAIL_IF_NULL(scene->characters);
        memcpy(scene->characters, list, sizeof(Character) * (size_t)count);
        scene->count        = count;
        scene->list_version = NetClient_CharacterListVersion();
    } else {
        scene->count = Character_ListSaved(&scene->characters);
    }
    scene->selected = scene->count > 0 ? 0 : -1;
    scene->scroll   = 0;
    CharacterSelect__BuildSlots(scene);
}

static void CharacterSelect__UpdateTitle(SceneCharacterSelect *scene) {
    Label_SetText(scene->title_label, scene->selected >= 0 ? scene->characters[scene->selected].name : "");
    const char *why      = NULL;
    const bool  playable = scene->selected >= 0 && !scene->characters[scene->selected].dead &&
                          CharacterSelect__Allowed(scene, &scene->characters[scene->selected], &why);
    Button_SetEnabled(scene->ok_button, playable && !scene->waiting);
    if (scene->online && !scene->waiting) {
        snprintf(scene->status, sizeof(scene->status), "%s", why != NULL ? why : "");
    }
    Button_SetEnabled(scene->delete_button, scene->selected >= 0);
    const int rows = (scene->count + SLOT_COLUMNS - 1) / SLOT_COLUMNS;
    Button_SetVisible(scene->scroll_up, scene->scroll > 0);
    Button_SetVisible(scene->scroll_down, scene->scroll + SLOT_ROWS < rows);
}

void *CharacterSelect_Create(void) {
    Cursor_SetVisible(true);
    SceneCharacterSelect *scene = calloc(1, sizeof(SceneCharacterSelect));
    FAIL_IF_NULL(scene);
    scene->background       = Sprite_Create(SELECT_BACKGROUND, PALETTE_SKY);
    scene->box              = Sprite_Create(SELECT_BOX, PALETTE_SKY);
    scene->popup            = Sprite_Create(SELECT_POPUP, PALETTE_FECHAR);
    scene->slots_for_scroll = -1;
    scene->last_click_index = -1;

    scene->title_label = Label_Create(FONTS_FONT42, PALETTE_STATIC);
    Label_SetAlignment(scene->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(scene->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    scene->popup_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->popup_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    scene->popup_note = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->popup_note, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetText(scene->popup_note, "This can't be undone.");
    scene->empty_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->empty_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetText(scene->empty_label, "No characters yet: click Create New.");

    scene->create_button = Button_Create(BUTTON_TYPE_TALL, "Create New");
    Button_SetPosition(scene->create_button, 33, 468);
    scene->delete_button = Button_Create(BUTTON_TYPE_TALL, "Delete");
    Button_SetPosition(scene->delete_button, 433, 468);
    scene->exit_button = Button_Create(BUTTON_TYPE_MEDIUM, "Exit");
    Button_SetPosition(scene->exit_button, 33, 537);
    scene->ok_button = Button_Create(BUTTON_TYPE_MEDIUM, "OK");
    Button_SetPosition(scene->ok_button, 627, 537);
    scene->popup_yes = Button_Create(BUTTON_TYPE_CANCEL, "Yes");
    Button_SetPosition(scene->popup_yes, 282, 333);
    scene->popup_no = Button_Create(BUTTON_TYPE_CANCEL, "No");
    Button_SetPosition(scene->popup_no, 422, 333);
    scene->scroll_up = Button_Create(BUTTON_TYPE_CANCEL, "Up");
    Button_SetPosition(scene->scroll_up, 352, 470);
    scene->scroll_down = Button_Create(BUTTON_TYPE_CANCEL, "Down");
    Button_SetPosition(scene->scroll_down, 352, 505);

    scene->online       = NetClient_Stage() == NETC_CHARACTERS;
    scene->status_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->status_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    scene->game_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->game_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(scene->game_label, 0xA0, 0xA0, 0xA0, 0xFF);
    if (scene->online) {
        const NetSettings *game = NetClient_Settings();
        char               text[160];
        snprintf(text, sizeof(text), "%s: %s, %s%s", game->name, NetSettings_DifficultyName(game->difficulty),
                 NetClient_ServerKeepsCharacters() ? "characters kept on the server" : "your characters",
                 Multiplayer_IsHosting() ? " (you're hosting)" : "");
        Label_SetText(scene->game_label, text);
        Button_SetText(scene->exit_button, "Leave");
    }
    CharacterSelect__Reload(scene);
    CharacterSelect__UpdateTitle(scene);
    return scene;
}

void CharacterSelect_Free(void **scene_ref) {
    Label_Destroy(&((SceneCharacterSelect *)*scene_ref)->status_label);
    Label_Destroy(&((SceneCharacterSelect *)*scene_ref)->game_label);
    SceneCharacterSelect *scene = *(SceneCharacterSelect **)scene_ref;
    CharacterSelect__FreeSlots(scene);
    free(scene->characters);
    Sprite_Destroy(&scene->background);
    Sprite_Destroy(&scene->box);
    Sprite_Destroy(&scene->popup);
    Label_Destroy(&scene->title_label);
    Label_Destroy(&scene->popup_label);
    Label_Destroy(&scene->empty_label);
    Label_Destroy(&scene->popup_note);
    Button_Destroy(&scene->create_button);
    Button_Destroy(&scene->delete_button);
    Button_Destroy(&scene->exit_button);
    Button_Destroy(&scene->ok_button);
    Button_Destroy(&scene->popup_yes);
    Button_Destroy(&scene->popup_no);
    Button_Destroy(&scene->scroll_up);
    Button_Destroy(&scene->scroll_down);
    free(scene);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------

static void CharacterSelect__SlotRect(const int slot, SDL_Rect *rect) {
    rect->x = (slot % SLOT_COLUMNS) == 0 ? SLOT_X0 : SLOT_X1;
    rect->y = SLOT_Y0 + ((slot / SLOT_COLUMNS) * SLOT_STEP_Y);
    rect->w = SLOT_W;
    rect->h = SLOT_H;
}

static void CharacterSelect__Play(SceneCharacterSelect *scene) {
    if (scene->selected < 0 || scene->characters[scene->selected].dead) {
        return;
    }
    if (scene->online) {
        const char *why = NULL;
        if (!CharacterSelect__Allowed(scene, &scene->characters[scene->selected], &why)) {
            snprintf(scene->status, sizeof(scene->status), "%s", why);
            Sounds_Play("cursor_error", SOUND_KIND_UI);
            return;
        }
        NetClient_Pick(&scene->characters[scene->selected]); // the server says yes or no
        scene->waiting = true;
        snprintf(scene->status, sizeof(scene->status), "Joining...");
        CharacterSelect__UpdateTitle(scene);
        return;
    }
    Character_SetCurrent(&scene->characters[scene->selected]);
    Scene_Set(SCENE_REF(Game));
}

// Keeps the selected character on screen
static void CharacterSelect__ScrollTo(SceneCharacterSelect *scene, const int index) {
    const int row = index / SLOT_COLUMNS;
    if (row < scene->scroll) {
        scene->scroll = row;
    } else if (row >= scene->scroll + SLOT_ROWS) {
        scene->scroll = row - SLOT_ROWS + 1;
    }
}

void CharacterSelect_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneCharacterSelect *scene = scene_ref;

    if (scene->online) {
        const NetClientStage stage = NetClient_Stage();
        if (stage == NETC_IN_GAME) {
            Scene_Set(SCENE_REF(Game)); // the server let us in
            return;
        }
        if (stage == NETC_FAILED || stage == NETC_OFF) {
            Scene_Set(SCENE_REF(MultiplayerMenu)); // it says why
            return;
        }
        bool        ok     = false;
        const char *result = NetClient_TakeResult(&ok);
        if (result != NULL && !ok) {
            snprintf(scene->status, sizeof(scene->status), "%s", result);
            scene->waiting = false;
            Sounds_Play("cursor_error", SOUND_KIND_UI);
            CharacterSelect__UpdateTitle(scene);
        }
        if (NetClient_ServerKeepsCharacters() && scene->list_version != NetClient_CharacterListVersion()) {
            CharacterSelect__Reload(scene);
            CharacterSelect__UpdateTitle(scene);
        }
        if (scene->waiting) {
            return;
        }
    }

    if (scene->confirming_delete) {
        if (Button_Update(scene->popup_yes) || InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN)) {
            if (scene->online && NetClient_ServerKeepsCharacters()) {
                NetClient_Delete(scene->characters[scene->selected].name); // the list comes back without it
            } else {
                Character_Delete(scene->characters[scene->selected].name);
            }
            scene->confirming_delete = false;
            CharacterSelect__Reload(scene);
            CharacterSelect__UpdateTitle(scene);
        } else if (Button_Update(scene->popup_no) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
            scene->confirming_delete = false;
        }
        return;
    }

    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked = left && !scene->prev_left;
    scene->prev_left   = left;

    if (clicked) {
        for (int s = 0; s < SLOT_COUNT; s++) {
            SDL_Rect rect;
            CharacterSelect__SlotRect(s, &rect);
            const int index = (scene->scroll * SLOT_COLUMNS) + s;
            if (index < scene->count && mouse_x >= rect.x && mouse_x < rect.x + rect.w && mouse_y >= rect.y &&
                mouse_y < rect.y + rect.h) {
                const uint64_t now      = SDL_GetTicks64();
                const bool     second   = index == scene->last_click_index && now - scene->last_click < DOUBLE_CLICK_MS;
                scene->selected         = index;
                scene->last_click       = now;
                scene->last_click_index = index;
                Sounds_Play("cursor_select", SOUND_KIND_UI);
                CharacterSelect__UpdateTitle(scene);
                if (second) {
                    CharacterSelect__Play(scene);
                    return;
                }
            }
        }
    }

    // Keyboard: arrows move, Enter plays, Delete deletes
    if (scene->count > 0) {
        int move  = 0;
        move     += InputManager_ConsumeKeyPress(SDL_SCANCODE_RIGHT) ? 1 : 0;
        move     -= InputManager_ConsumeKeyPress(SDL_SCANCODE_LEFT) ? 1 : 0;
        move     += InputManager_ConsumeKeyPress(SDL_SCANCODE_DOWN) ? SLOT_COLUMNS : 0;
        move     -= InputManager_ConsumeKeyPress(SDL_SCANCODE_UP) ? SLOT_COLUMNS : 0;
        if (move != 0) {
            int next        = scene->selected + move;
            next            = next < 0 ? 0 : (next >= scene->count ? scene->count - 1 : next);
            scene->selected = next;
            CharacterSelect__ScrollTo(scene, next);
            CharacterSelect__UpdateTitle(scene);
        }
    }
    const int wheel = InputManager_ConsumeMouseWheel();
    const int rows  = (scene->count + SLOT_COLUMNS - 1) / SLOT_COLUMNS;
    if (Button_Update(scene->scroll_up) || wheel > 0) {
        scene->scroll = scene->scroll > 0 ? scene->scroll - 1 : 0;
        CharacterSelect__UpdateTitle(scene);
    }
    if (Button_Update(scene->scroll_down) || wheel < 0) {
        scene->scroll = scene->scroll + SLOT_ROWS < rows ? scene->scroll + 1 : scene->scroll;
        CharacterSelect__UpdateTitle(scene);
    }
    if (scene->slots_for_scroll != scene->scroll) {
        CharacterSelect__BuildSlots(scene);
    }

    if (Button_Update(scene->ok_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN)) {
        CharacterSelect__Play(scene);
        return;
    }
    if (Button_Update(scene->delete_button) ||
        (scene->selected >= 0 && InputManager_ConsumeKeyPress(SDL_SCANCODE_DELETE))) {
        char text[96];
        snprintf(text, sizeof(text), "Delete %s?", scene->characters[scene->selected].name);
        Label_SetText(scene->popup_label, text);
        scene->confirming_delete = true;
        InputManager_ResetMouseButtons();
        return;
    }
    if (Button_Update(scene->create_button)) {
        Scene_Set(SCENE_REF(CharacterCreate));
        return;
    }
    if (Button_Update(scene->exit_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        if (scene->online) {
            Multiplayer_Leave();
            Scene_Set(SCENE_REF(MultiplayerMenu));
        } else {
            Scene_Set(SCENE_REF(MainMenu));
        }
    }
}

void CharacterSelect_Render(void *scene_ref) {
    SceneCharacterSelect *scene = scene_ref;
    Sprite_DrawMulti(scene->background, 0, 0, 0, 4, 3);
    Label_Draw(scene->title_label, 317, 44);

    const uint64_t now = SDL_GetTicks64();
    for (int s = 0; s < SLOT_COUNT; s++) {
        const int index = (scene->scroll * SLOT_COLUMNS) + s;
        if (index >= scene->count) {
            break;
        }
        SDL_Rect rect;
        CharacterSelect__SlotRect(s, &rect);
        if (index == scene->selected) {
            Sprite_DrawMulti(scene->box, 0, rect.x, rect.y, 2, 1);
        }
        const Slot *slot = &scene->slots[s];
        if (slot->figure != NULL) {
            const int frames = Figure_GetFramesPerDirection(slot->figure);
            const int ms =
                Figure_GetFrameMilliseconds(slot->figure) > 0 ? Figure_GetFrameMilliseconds(slot->figure) : 40;
            const int frame = frames > 0 ? (int)((now / (uint64_t)ms) % (uint64_t)frames) : 0;
            Figure_Draw(slot->figure, MapUnits_Direction(slot->figure, 0), frame, rect.x + 40, rect.y + 82, 1.0f);
        }
        Label_Draw(slot->name_label, rect.x + 90, rect.y + 12);
        Label_Draw(slot->class_label, rect.x + 90, rect.y + 32);
        Label_Draw(slot->extra_label, rect.x + 90, rect.y + 52);
    }
    if (scene->count == 0) {
        Label_Draw(scene->empty_label, 317, 260);
    }

    if (scene->online) {
        Label_Draw(scene->game_label, 317, 72);
        if (scene->status[0] != '\0') {
            Label_SetText(scene->status_label, scene->status);
            Label_SetColor(scene->status_label, scene->waiting ? 0xFF : 0xFF, scene->waiting ? 0xE8 : 0x70,
                           scene->waiting ? 0xA0 : 0x60, 0xFF);
            Label_Draw(scene->status_label, 317, 455);
        }
    }
    Button_Draw(scene->create_button);
    Button_Draw(scene->delete_button);
    Button_Draw(scene->exit_button);
    Button_Draw(scene->ok_button);
    Button_Draw(scene->scroll_up);
    Button_Draw(scene->scroll_down);

    if (scene->confirming_delete) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 140);
        SDL_RenderFillRect(sdl_renderer, NULL);
        Sprite_DrawMulti(scene->popup, 0, 268, 212, 2, 1);
        Label_Draw(scene->popup_label, 400, 262);
        Label_Draw(scene->popup_note, 400, 284);
        Button_Draw(scene->popup_yes);
        Button_Draw(scene->popup_no);
    }
}
