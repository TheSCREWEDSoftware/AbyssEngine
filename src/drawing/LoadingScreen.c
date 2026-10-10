#include "LoadingScreen.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/ResourcePaths.h"
#include "Label.h"
#include "Sprite.h"
#include <SDL2/SDL.h>
#include <stdio.h>

#define LOADING_SHOW_AFTER 120 // ms before the screen appears
#define LOADING_FRAME_MS   50  // redraw at most this often
#define LOADING_ANIM_MS    80  // per animation frame

static int      loading_depth     = 0;
static bool     loading_drawing   = false;
static uint64_t loading_start     = 0;
static uint64_t loading_last_draw = 0;
static char     loading_text[128] = "";
static Sprite  *loading_sprite    = NULL;
static Label   *loading_label     = NULL;

static void LoadingScreen__Draw(const uint64_t now) {
    loading_drawing = true; // creating the sprite reads files, which would come back here

    if (loading_sprite == NULL && FileManager_FileExists(LOADING_SCREEN)) {
        loading_sprite = Sprite_Create(LOADING_SCREEN, PALETTE_LOADING);
    }
    if (loading_label == NULL) {
        loading_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(loading_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Label_SetColor(loading_label, 0xC7, 0xB3, 0x77, 0xFF);
    }

    SDL_PumpEvents(); // keep the window responsive ("not responding" otherwise)
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);

    int bottom = 330;
    if (loading_sprite != NULL && Sprite_GetFrameCount(loading_sprite) > 0) {
        const int frames = (int)Sprite_GetFrameCount(loading_sprite);
        const int frame  = (int)(((now - loading_start) / LOADING_ANIM_MS) % (uint64_t)frames);
        int       w;
        int       h;
        Sprite_GetFrameSize(loading_sprite, frame, &w, &h);
        Sprite_DrawScaled(loading_sprite, frame, 400 - (w / 2), 300 - (h / 2), 1.0f);
        bottom = 300 + (h / 2) + 16;
    }
    Label_SetText(loading_label, loading_text);
    Label_Draw(loading_label, 400, bottom);
    SDL_RenderPresent(sdl_renderer);

    loading_drawing = false;
}

void LoadingScreen_Tick(void) {
    if (loading_depth == 0 || loading_drawing) {
        return;
    }
    const uint64_t now = SDL_GetTicks64();
    if (now - loading_start < LOADING_SHOW_AFTER || now - loading_last_draw < LOADING_FRAME_MS) {
        return;
    }
    loading_last_draw = now;
    LoadingScreen__Draw(now);
}

void LoadingScreen_Begin(const char *text) {
    if (loading_depth++ > 0) {
        return; // already loading; keep the outer text
    }
    loading_start     = SDL_GetTicks64();
    loading_last_draw = 0;
    snprintf(loading_text, sizeof(loading_text), "%s", text != NULL ? text : "Loading...");
    FileManager_SetReadHook(LoadingScreen_Tick);
}

void LoadingScreen_End(void) {
    if (loading_depth > 0 && --loading_depth == 0) {
        FileManager_SetReadHook(NULL);
    }
}

void LoadingScreen_Release(void) {
    if (loading_sprite != NULL) {
        Sprite_Destroy(&loading_sprite);
    }
    Label_Destroy(&loading_label);
}
