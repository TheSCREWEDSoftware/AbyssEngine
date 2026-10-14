#include "SceneCharacterCreate.h"
#include "../audio/Sounds.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../drawing/TextBox.h"
#include "../game/Character.h"
#include "../managers/InputManager.h"
#include "../net/Client.h"
#include "SceneCharacterSelect.h"
#include "SceneGame.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The "Select Hero Class" screen: the seven heroes round the campfire. Click one and they step forward; type a name,
// tick Expansion / Hardcore, OK to start in the Rogue Encampment.

#define CREATE_BACKGROUND "/data/global/ui/FrontEnd/charactercreationscreenEXP.dc6"
#define CREATE_FIRE       "/data/global/ui/FrontEnd/fire.DC6"
#define CREATE_CHECKBOX   "/data/global/ui/FrontEnd/clickbox.DC6"
#define CREATE_TEXTBOX    "/data/global/ui/FrontEnd/textbox.dc6"
#define FRONTEND_MS       40 // the front end's animations run at 25 frames a second

typedef enum HeroState { HERO_IDLE, HERO_FORWARD, HERO_SELECTED, HERO_BACK } HeroState;

typedef enum HeroAnim {
    ANIM_IDLE,       // NU1: standing round the fire
    ANIM_HOVER,      // NU2: the same, lit up
    ANIM_SELECTED,   // NU3: stepped forward
    ANIM_SELECTED_S, // NU3s: its glow (only some heroes)
    ANIM_FORWARD,    // FW: stepping forward
    ANIM_FORWARD_S,
    ANIM_BACK, // BW: stepping back
    ANIM_BACK_S,
    ANIM_COUNT
} HeroAnim;

static const struct {
    CharacterClass class_id;
    const char    *folder;
    const char    *prefix;
    int            x; // where they stand (the sprites' anchor)
    int            y;
    const char    *description[2];
} hero_layout[CHARACTER_CLASS_COUNT] = {
    {CHARACTER_AMAZON,
     "amazon",                         "AM",
     100,                                         339,
     {"Skilled with the spear and the bow, she is", "a very versatile fighter."}                                          },
    {CHARACTER_ASSASSIN,
     "assassin",                       "AS",
     231,                                         365,
     {"Schooled in the Martial Arts, her", "mind and body are deadly weapons."}                                           },
    {CHARACTER_NECROMANCER,
     "necromancer",                    "NE",
     300,                                         335,
     {"Summoning undead minions and cursing", "his enemies are his specialties."}                                         },
    {CHARACTER_BARBARIAN,
     "barbarian",                      "BA",
     400,                                         330,
     {"He is unequaled in close-quarters", "combat and mastery of weapons."}                                              },
    {CHARACTER_PALADIN,     "paladin", "PA", 521, 338, {"He is a natural party leader, holy", "man, and blessed warrior."}},
    {CHARACTER_SORCERESS,
     "sorceress",                      "SO",
     626,                                         352,
     {"She has mastered the elemental magicks --", "fire, lightning, and ice."}                                           },
    {CHARACTER_DRUID,
     "druid",                          "DZ",
     720,                                         370,
     {"Commanding the forces of nature, he summons", "wild beasts and raging storms to his side."}                        },
};

static const char *anim_suffix[ANIM_COUNT] = {"NU1", "NU2", "NU3", "NU3s", "FW", "FWs", "BW", "BWs"};

typedef struct Hero {
    CharacterClass class_id;
    Sprite        *anims[ANIM_COUNT]; // NULL where a hero has no such file
    int            x;
    int            y;
    HeroState      state;
    int            frame;
    uint64_t       frame_time;
    bool           hovered;
} Hero;

typedef struct SceneCharacterCreate {
    bool     waiting; // multiplayer, dedicated server: it is making the character
    Sprite  *background;
    Sprite  *fire;
    Sprite  *checkbox;
    Hero     heroes[CHARACTER_CLASS_COUNT];
    int      selected; // index into heroes, -1 = none
    Label   *title_label;
    Label   *class_label;
    Label   *description_labels[2];
    Label   *name_caption;
    Label   *expansion_label;
    Label   *hardcore_label;
    Label   *error_label;
    TextBox *name_box;
    Button  *exit_button;
    Button  *ok_button;
    bool     expansion;
    bool     hardcore;
    bool     prev_left;
    char     error[96];
    uint64_t error_until;
} SceneCharacterCreate;

DEFINE_SCENE_CALLBACKS(CharacterCreate);

#define NAME_X      318
#define NAME_Y      503
#define NAME_W      169
#define NAME_H      26
#define CHECK_X     320
#define EXPANSION_Y 540
#define HARDCORE_Y  562
#define CHECK_SIZE  16

// ---------------------------------------------------------------------------------------------------------------------

static int CharacterCreate__FrameCount(const Sprite *sprite) {
    return sprite != NULL ? (int)Sprite_GetFrameCount(sprite) : 0;
}

static void CharacterCreate__SetState(Hero *hero, const HeroState state, const uint64_t now) {
    hero->state      = state;
    hero->frame      = 0;
    hero->frame_time = now;
}

static void CharacterCreate__PlaySound(const Hero *hero, const bool select) {
    char name[64];
    snprintf(name, sizeof(name), "cursor_%s_%s", Character_ClassName(hero->class_id), select ? "select" : "deselect");
    for (char *c = name; *c != '\0'; c++) {
        *c = (char)tolower((unsigned char)*c);
    }
    if (!Sounds_Play(name, SOUND_KIND_UI)) {
        Sounds_Play("cursor_select", SOUND_KIND_UI);
    }
}

void *CharacterCreate_Create(void) {
    Cursor_SetVisible(true);
    SceneCharacterCreate *scene = calloc(1, sizeof(SceneCharacterCreate));
    FAIL_IF_NULL(scene);
    scene->selected  = -1;
    scene->expansion = true;

    scene->background = Sprite_Create(CREATE_BACKGROUND, PALETTE_FECHAR);
    scene->fire       = Sprite_Create(CREATE_FIRE, PALETTE_FECHAR);
    Sprite_SetBlendMode(scene->fire, SDL_BLENDMODE_ADD);
    Sprite_SetPlayLength(scene->fire, (double)Sprite_GetFrameCount(scene->fire) * FRONTEND_MS / 1000.0);
    scene->checkbox = Sprite_Create(CREATE_CHECKBOX, PALETTE_FECHAR);

    const uint64_t now = SDL_GetTicks64();
    for (int h = 0; h < CHARACTER_CLASS_COUNT; h++) {
        Hero *hero     = &scene->heroes[h];
        hero->class_id = hero_layout[h].class_id;
        hero->x        = hero_layout[h].x;
        hero->y        = hero_layout[h].y;
        for (int a = 0; a < ANIM_COUNT; a++) {
            char path[128];
            snprintf(path, sizeof(path), "/data/global/ui/FrontEnd/%s/%s%s.DC6", hero_layout[h].folder,
                     hero_layout[h].prefix, anim_suffix[a]);
            hero->anims[a] = FileManager_FileExists(path) ? Sprite_Create(path, PALETTE_FECHAR) : NULL;
            if (hero->anims[a] != NULL && (a == ANIM_SELECTED_S || a == ANIM_FORWARD_S || a == ANIM_BACK_S)) {
                Sprite_SetBlendMode(hero->anims[a], SDL_BLENDMODE_ADD); // glows and sparks
            }
        }
        CharacterCreate__SetState(hero, HERO_IDLE, now);
        hero->frame = rand() % (CharacterCreate__FrameCount(hero->anims[ANIM_IDLE]) + 1); // not all in step
    }

    scene->title_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetAlignment(scene->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetText(scene->title_label, "Select Hero Class");
    scene->class_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetAlignment(scene->class_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int i = 0; i < 2; i++) {
        scene->description_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(scene->description_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }
    scene->name_caption = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(scene->name_caption, "Character Name");
    Label_SetColor(scene->name_caption, 0xC7, 0xB3, 0x77, 0xFF);
    scene->expansion_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(scene->expansion_label, "Expansion Character");
    scene->hardcore_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(scene->hardcore_label, "Hardcore");
    scene->error_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->error_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(scene->error_label, 0xFF, 0x50, 0x50, 0xFF);

    scene->name_box = TextBox_Create(FONTS_FONT16, NAME_X, NAME_Y, NAME_W, NAME_H, "");

    scene->exit_button = Button_Create(BUTTON_TYPE_MEDIUM, "Exit");
    Button_SetPosition(scene->exit_button, 33, 537);
    scene->ok_button = Button_Create(BUTTON_TYPE_MEDIUM, "OK");
    Button_SetPosition(scene->ok_button, 630, 537);
    Button_SetEnabled(scene->ok_button, false);
    return scene;
}

void CharacterCreate_Free(void **scene_ref) {
    SceneCharacterCreate *scene = *(SceneCharacterCreate **)scene_ref;
    for (int h = 0; h < CHARACTER_CLASS_COUNT; h++) {
        for (int a = 0; a < ANIM_COUNT; a++) {
            Sprite_Destroy(&scene->heroes[h].anims[a]);
        }
    }
    Sprite_Destroy(&scene->background);
    Sprite_Destroy(&scene->fire);
    Sprite_Destroy(&scene->checkbox);
    Label_Destroy(&scene->title_label);
    Label_Destroy(&scene->class_label);
    Label_Destroy(&scene->description_labels[0]);
    Label_Destroy(&scene->description_labels[1]);
    Label_Destroy(&scene->name_caption);
    Label_Destroy(&scene->expansion_label);
    Label_Destroy(&scene->hardcore_label);
    Label_Destroy(&scene->error_label);
    TextBox_Destroy(&scene->name_box);
    Button_Destroy(&scene->exit_button);
    Button_Destroy(&scene->ok_button);
    free(scene);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------

// The animation a hero shows now (and its glow, if any)
static void CharacterCreate__Anims(const Hero *hero, Sprite **main, Sprite **overlay) {
    *overlay = NULL;
    switch (hero->state) {
    case HERO_FORWARD:
        *main    = hero->anims[ANIM_FORWARD];
        *overlay = hero->anims[ANIM_FORWARD_S];
        break;
    case HERO_SELECTED:
        *main    = hero->anims[ANIM_SELECTED];
        *overlay = hero->anims[ANIM_SELECTED_S];
        break;
    case HERO_BACK:
        *main    = hero->anims[ANIM_BACK] != NULL ? hero->anims[ANIM_BACK] : hero->anims[ANIM_IDLE];
        *overlay = hero->anims[ANIM_BACK_S];
        break;
    default:
        *main = hero->hovered && hero->anims[ANIM_HOVER] != NULL ? hero->anims[ANIM_HOVER] : hero->anims[ANIM_IDLE];
        break;
    }
}

static bool CharacterCreate__HitTest(const Hero *hero, const int x, const int y) {
    // Roughly the body: the sprites stand on (x, y + 50) and are about 180 pixels tall
    return x >= hero->x - 38 && x <= hero->x + 38 && y >= hero->y - 125 && y <= hero->y + 50;
}

static void CharacterCreate__Select(SceneCharacterCreate *scene, const int index, const uint64_t now) {
    if (index == scene->selected) {
        return;
    }
    if (scene->selected >= 0) {
        Hero *previous = &scene->heroes[scene->selected];
        CharacterCreate__SetState(previous, HERO_BACK, now);
        CharacterCreate__PlaySound(previous, false);
    }
    scene->selected = index;
    if (index >= 0) {
        Hero *hero = &scene->heroes[index];
        CharacterCreate__SetState(hero, HERO_FORWARD, now);
        CharacterCreate__PlaySound(hero, true);
        if (Character_ClassIsExpansion(hero->class_id)) {
            scene->expansion = true; // the Druid and the Assassin only exist in Lord of Destruction
        }
        TextBox_Focus(scene->name_box);
        Label_SetText(scene->class_label, Character_ClassName(hero->class_id));
        for (int i = 0; i < 2; i++) {
            const int layout = index;
            Label_SetText(scene->description_labels[i], hero_layout[layout].description[i]);
        }
    }
}

static void CharacterCreate__ShowError(SceneCharacterCreate *scene, const char *text) {
    snprintf(scene->error, sizeof(scene->error), "%s", text);
    Label_SetText(scene->error_label, text);
    scene->error_until = SDL_GetTicks64() + 4000;
    Sounds_Play("cursor_error", SOUND_KIND_UI);
}

static void CharacterCreate__Confirm(SceneCharacterCreate *scene) {
    if (scene->selected < 0) {
        return;
    }
    const char *name   = TextBox_GetText(scene->name_box);
    const char *reason = NULL;
    if (!Character_IsValidName(name, &reason)) {
        CharacterCreate__ShowError(scene, reason);
        return;
    }
    if (NetClient_Stage() == NETC_CHARACTERS && NetClient_ServerKeepsCharacters()) {
        // A dedicated server makes and keeps it: wait for its answer
        NetClient_Create(name, scene->heroes[scene->selected].class_id, scene->expansion, scene->hardcore);
        scene->waiting = true;
        return;
    }
    if (Character_Exists(name)) {
        CharacterCreate__ShowError(scene, "A character with that name already exists.");
        return;
    }
    Character character;
    Character_Create(&character, name, scene->heroes[scene->selected].class_id, scene->expansion, scene->hardcore);
    const bool online = NetClient_Stage() == NETC_CHARACTERS;
    if (online) {
        // Made in someone's game: marked as made there (games that only take their own characters let it in)
        snprintf(character.realm, sizeof(character.realm), "%s", NetClient_Realm());
    }
    if (!Character_Save(&character)) {
        CharacterCreate__ShowError(scene, "Could not save the character.");
        return;
    }
    Character_SetCurrent(&character);
    Scene_Set(online ? SCENE_REF(CharacterSelect) : SCENE_REF(Game)); // multiplayer: pick it there to join
}

void CharacterCreate_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneCharacterCreate *scene = scene_ref;
    const uint64_t        now   = SDL_GetTicks64();

    int  mouse_x;
    int  mouse_y;
    bool left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool clicked = left && !scene->prev_left;
    scene->prev_left   = left;

    // Animations: stepping forward ends in the selected pose, stepping back in the idle one
    int hovered = -1;
    for (int h = 0; h < CHARACTER_CLASS_COUNT; h++) {
        Hero *hero = &scene->heroes[h];
        while (now - hero->frame_time >= FRONTEND_MS) {
            hero->frame_time += FRONTEND_MS;
            hero->frame++;
            Sprite *main;
            Sprite *overlay;
            CharacterCreate__Anims(hero, &main, &overlay);
            const int frames = CharacterCreate__FrameCount(main);
            if (hero->frame >= frames) {
                if (hero->state == HERO_FORWARD) {
                    CharacterCreate__SetState(hero, HERO_SELECTED, hero->frame_time);
                } else if (hero->state == HERO_BACK) {
                    CharacterCreate__SetState(hero, HERO_IDLE, hero->frame_time);
                } else {
                    hero->frame = 0;
                }
            }
        }
        hero->hovered = false;
        if (hero->state == HERO_IDLE && CharacterCreate__HitTest(hero, mouse_x, mouse_y)) {
            hovered = h;
        }
    }
    if (hovered >= 0) {
        scene->heroes[hovered].hovered = true;
        if (clicked) {
            CharacterCreate__Select(scene, hovered, now);
        }
    }

    // Checkboxes
    if (clicked && mouse_x >= CHECK_X && mouse_x < CHECK_X + CHECK_SIZE + 160) {
        if (mouse_y >= EXPANSION_Y && mouse_y < EXPANSION_Y + CHECK_SIZE) {
            const bool locked =
                scene->selected >= 0 && Character_ClassIsExpansion(scene->heroes[scene->selected].class_id);
            scene->expansion = locked ? true : !scene->expansion;
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        } else if (mouse_y >= HARDCORE_Y && mouse_y < HARDCORE_Y + CHECK_SIZE) {
            scene->hardcore = !scene->hardcore;
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        }
    }

    if (scene->waiting) {
        bool        ok     = false;
        const char *result = NetClient_TakeResult(&ok);
        if (NetClient_Stage() != NETC_CHARACTERS) {
            Scene_Set(SCENE_REF(CharacterSelect)); // lost the connection: it sends us on
            return;
        }
        if (result != NULL) {
            scene->waiting = false;
            if (ok) {
                Scene_Set(SCENE_REF(CharacterSelect));
                return;
            }
            CharacterCreate__ShowError(scene, result);
        }
        return;
    }
    TextBox_Update(scene->name_box);
    Button_SetEnabled(scene->ok_button, scene->selected >= 0 && strlen(TextBox_GetText(scene->name_box)) >= 2);

    if (Button_Update(scene->ok_button) ||
        (InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN) && scene->selected >= 0)) {
        CharacterCreate__Confirm(scene);
        return;
    }
    if (Button_Update(scene->exit_button) ||
        (strlen(TextBox_GetText(scene->name_box)) == 0 && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE))) {
        TextBox_Blur(scene->name_box);
        Scene_Set(SCENE_REF(CharacterSelect));
    }
}

void CharacterCreate_Render(void *scene_ref) {
    SceneCharacterCreate *scene = scene_ref;
    Sprite_DrawMulti(scene->background, 0, 0, 0, 4, 3);

    // Back to front: the ones standing further back first, the one stepping forward last
    int order[CHARACTER_CLASS_COUNT];
    for (int i = 0; i < CHARACTER_CLASS_COUNT; i++) {
        order[i] = i;
    }
    for (int i = 1; i < CHARACTER_CLASS_COUNT; i++) {
        for (int j = i; j > 0; j--) {
            const Hero *a     = &scene->heroes[order[j - 1]];
            const Hero *b     = &scene->heroes[order[j]];
            const bool  a_out = a->state != HERO_IDLE;
            const bool  b_out = b->state != HERO_IDLE;
            if (a_out > b_out || (a_out == b_out && a->y > b->y)) {
                const int t  = order[j - 1];
                order[j - 1] = order[j];
                order[j]     = t;
            }
        }
    }
    bool fire_drawn = false;
    for (int i = 0; i < CHARACTER_CLASS_COUNT; i++) {
        const Hero *hero = &scene->heroes[order[i]];
        if (!fire_drawn && (hero->state != HERO_IDLE || hero->y > 340)) {
            Sprite_DrawAnimated(scene->fire, 380, 335);
            fire_drawn = true;
        }
        Sprite *main;
        Sprite *overlay;
        CharacterCreate__Anims(hero, &main, &overlay);
        if (main != NULL) {
            const int frames = CharacterCreate__FrameCount(main);
            Sprite_Draw(main, (uint8_t)(frames > 0 ? hero->frame % frames : 0), hero->x, hero->y);
        }
        if (overlay != NULL) {
            const int frames = CharacterCreate__FrameCount(overlay);
            Sprite_Draw(overlay, (uint8_t)(frames > 0 ? hero->frame % frames : 0), hero->x, hero->y);
        }
    }
    if (!fire_drawn) {
        Sprite_DrawAnimated(scene->fire, 380, 335);
    }

    Label_Draw(scene->title_label, 400, 24);
    if (scene->selected >= 0) {
        Label_Draw(scene->class_label, 400, 64);
        Label_Draw(scene->description_labels[0], 400, 96);
        Label_Draw(scene->description_labels[1], 400, 114);

        Label_Draw(scene->name_caption, NAME_X, NAME_Y - 18);
        TextBox_Draw(scene->name_box);
        Sprite_DrawScaled(scene->checkbox, scene->expansion ? 1 : 0, CHECK_X, EXPANSION_Y, 1.0f);
        Label_Draw(scene->expansion_label, CHECK_X + CHECK_SIZE + 8, EXPANSION_Y);
        Sprite_DrawScaled(scene->checkbox, scene->hardcore ? 1 : 0, CHECK_X, HARDCORE_Y, 1.0f);
        Label_Draw(scene->hardcore_label, CHECK_X + CHECK_SIZE + 8, HARDCORE_Y);
    }
    if (SDL_GetTicks64() < scene->error_until) {
        Label_Draw(scene->error_label, 400, 470);
    }
    Button_Draw(scene->exit_button);
    Button_Draw(scene->ok_button);
}
