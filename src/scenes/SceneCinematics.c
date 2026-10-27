#include "SceneCinematics.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../managers/InputManager.h"
#include "../managers/VideoManager.h"
#include "SceneMainMenu.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static const struct {
    const char *name;
    const char *path;
} films[] = {
    {"Diablo II",               "/data/local/video/d2intro640x480.bik"   },
    {"Act II",                  "/data/local/video/act02start640x480.bik"},
    {"Act III",                 "/data/local/video/act03start640x480.bik"},
    {"Act IV",                  "/data/local/video/act04start640x480.bik"},
    {"Ending",                  "/data/local/video/act04end640x480.bik"  },
    {"Lord of Destruction",     "/data/local/video/d2x_intro_640x480.bik"},
    {"Lord of Destruction End", "/data/local/video/d2x_out_640x480.bik"  },
};
#define FILM_COUNT (int)(sizeof(films) / sizeof(films[0]))

typedef struct SceneCinematics {
    Sprite *background;
    Label  *title;
    Label  *note;
    Button *buttons[FILM_COUNT];
    Button *back;
} SceneCinematics;

DEFINE_SCENE_CALLBACKS(Cinematics);

void *Cinematics_Create(void) {
    SceneCinematics *scene = calloc(1, sizeof(SceneCinematics));
    FAIL_IF_NULL(scene);
    scene->background = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    scene->title      = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetText(scene->title, "Cinematics");
    Label_SetAlignment(scene->title, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(scene->title, 0xC7, 0xB3, 0x77, 0xFF);
    for (int f = 0; f < FILM_COUNT; f++) {
        scene->buttons[f] = Button_Create(BUTTON_TYPE_WIDE, films[f].name);
        Button_SetPosition(scene->buttons[f], 264, 120 + (f * 48));
        Button_SetEnabled(scene->buttons[f], FileManager_FileExists(films[f].path));
    }
    scene->note = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->note, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetText(scene->note, "Greyed out: not in the game's files here (d2video.mpq, d2exp.mpq).");
    Label_SetColor(scene->note, 0xA0, 0xA0, 0xA0, 0xFF);
    scene->back = Button_Create(BUTTON_TYPE_MEDIUM, "Back");
    Button_SetPosition(scene->back, 336, 520);
    return scene;
}

void Cinematics_Render(void *scene_ref) {
    SceneCinematics *scene = scene_ref;
    Sprite_DrawMulti(scene->background, 0, 0, 0, 4, 3);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 120);
    SDL_RenderFillRect(sdl_renderer, NULL);
    Label_Draw(scene->title, 400, 70);
    for (int f = 0; f < FILM_COUNT; f++) {
        Button_Draw(scene->buttons[f]);
    }
    Button_Draw(scene->back);
    bool missing = false;
    for (int f = 0; f < FILM_COUNT; f++) {
        missing = missing || !FileManager_FileExists(films[f].path);
    }
    if (missing) {
        Label_Draw(scene->note, 400, 480);
    }
}

void Cinematics_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneCinematics *scene = scene_ref;
    for (int f = 0; f < FILM_COUNT; f++) {
        if (Button_Update(scene->buttons[f])) {
            VideoManager_PlayVideo(films[f].path);
            return;
        }
    }
    if (Button_Update(scene->back) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        Scene_Set(SCENE_REF(MainMenu));
    }
}

void Cinematics_Free(void **scene_ref) {
    SceneCinematics *scene = *scene_ref;
    Sprite_Destroy(&scene->background);
    Label_Destroy(&scene->title);
    Label_Destroy(&scene->note);
    for (int f = 0; f < FILM_COUNT; f++) {
        Button_Destroy(&scene->buttons[f]);
    }
    Button_Destroy(&scene->back);
    free(scene);
    *scene_ref = NULL;
}
