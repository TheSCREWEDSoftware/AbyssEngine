#include "SceneCredits.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../managers/AudioManager.h"
#include "../managers/InputManager.h"
#include "SceneMainMenu.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CREDITS_LINE_H 18
#define CREDITS_SPEED  32.0f // pixels a second
#define CREDITS_MUSIC  "/data/global/music/common/credits.wav"
#define CREDITS_MAX    4000

typedef struct SceneCredits {
    Sprite *background;
    Label  *label;
    char  **lines;
    int     line_count;
    float   scroll; // how far up it has rolled (pixels)
    bool    prev_click;
} SceneCredits;

DEFINE_SCENE_CALLBACKS(Credits);

// The text file's lines (it's the game's: Windows line ends, maybe 8-bit text)
static void Credits__Load(SceneCredits *scene, const char *name) {
    char path[160];
    snprintf(path, sizeof(path), "/data/local/UI/%s/%s", AbyssConfiguration_GetLocale(), name);
    if (!FileManager_FileExists(path)) {
        snprintf(path, sizeof(path), "/data/local/UI/ENG/%s", name);
        if (!FileManager_FileExists(path)) {
            return;
        }
    }
    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    char          *text   = malloc(size + 1);
    FAIL_IF_NULL(text);
    MpqStream_Read(stream, text, 0, size);
    MpqStream_Destroy(&stream);
    text[size] = '\0';
    // The expansion's is UTF-16 (a byte-order mark, then two bytes a letter): its letters are all in the first byte
    if (size >= 2 && (uint8_t)text[0] == 0xFF && (uint8_t)text[1] == 0xFE) {
        uint32_t o = 0;
        for (uint32_t i = 2; i + 1 < size; i += 2) {
            text[o++] = text[i + 1] == 0 ? text[i] : '?';
        }
        text[o] = '\0';
    }
    scene->lines = calloc(CREDITS_MAX, sizeof(char *));
    FAIL_IF_NULL(scene->lines);
    char *line = text;
    while (line != NULL && *line != '\0' && scene->line_count < CREDITS_MAX) {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        const size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\r') {
            line[length - 1] = '\0';
        }
        for (char *c = line; *c != '\0'; c++) {
            *c = (unsigned char)*c >= 0x80 || (unsigned char)*c < 0x20 ? ' ' : *c; // what the font has
        }
        scene->lines[scene->line_count] = strdup(line);
        FAIL_IF_NULL(scene->lines[scene->line_count]);
        scene->line_count++;
        line = end != NULL ? end + 1 : NULL;
    }
    free(text);
}

void *Credits_Create(void) {
    SceneCredits *scene = calloc(1, sizeof(SceneCredits));
    FAIL_IF_NULL(scene);
    scene->background = Sprite_Create("/data/global/ui/CharSelect/creditsbckgexpand.dc6", PALETTE_SKY);
    if (scene->background == NULL) {
        scene->background = Sprite_Create("/data/global/ui/CharSelect/creditsbckg.dc6", PALETTE_SKY);
    }
    scene->label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(scene->label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Credits__Load(scene, "ExpansionCredits.txt");
    if (scene->line_count == 0) {
        Credits__Load(scene, "credits.txt");
    }
    scene->scroll = -560.0f; // starts at the bottom of the screen
    if (FileManager_FileExists(CREDITS_MUSIC)) {
        AudioManager_PlayMusic(CREDITS_MUSIC, true);
    }
    InputManager_ClearPresses();
    bool left = false;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    scene->prev_click = left;
    return scene;
}

void Credits_Render(void *scene_ref) {
    SceneCredits *scene = scene_ref;
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    Sprite_DrawMulti(scene->background, 0, 0, 0, 4, 3);
    if (scene->line_count == 0) {
        Label_SetText(scene->label, "The game's credits aren't in its files here.");
        Label_SetColor(scene->label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(scene->label, 400, 300);
        return;
    }
    const int first = scene->scroll > 0 ? (int)(scene->scroll / CREDITS_LINE_H) : 0;
    for (int l = first; l < scene->line_count; l++) {
        const int y = (int)((float)(l * CREDITS_LINE_H) - scene->scroll) + 20;
        if (y > 600) {
            break;
        }
        const char *line    = scene->lines[l];
        const bool  heading = line[0] == '*';
        Label_SetText(scene->label, heading ? line + 1 : line);
        // Fading in at the bottom and out at the top
        const int     edge  = y < 80 ? y : (y > 520 ? 600 - y : 80);
        const uint8_t alpha = (uint8_t)(edge <= 0 ? 0 : (edge >= 80 ? 255 : edge * 255 / 80));
        if (heading) {
            Label_SetColor(scene->label, 0xC7, 0xB3, 0x77, alpha);
        } else {
            Label_SetColor(scene->label, 0xFF, 0xFF, 0xFF, alpha);
        }
        Label_Draw(scene->label, 400, y);
    }
}

void Credits_Update(void *scene_ref, const uint64_t delta) {
    SceneCredits *scene  = scene_ref;
    scene->scroll       += CREDITS_SPEED * (float)delta / 1000.0f;
    bool left            = false;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool click    = left && !scene->prev_click;
    scene->prev_click   = left;
    const bool finished = scene->scroll > (float)(scene->line_count * CREDITS_LINE_H) + 40.0f;
    InputPress press;
    bool       key = false;
    while (InputManager_PollPress(&press)) {
        key = true;
    }
    if (click || key || finished) {
        Scene_Set(SCENE_REF(MainMenu)); // (its music comes back with it)
    }
}

void Credits_Free(void **scene_ref) {
    SceneCredits *scene = *scene_ref;
    for (int l = 0; l < scene->line_count; l++) {
        free(scene->lines[l]);
    }
    free(scene->lines);
    Sprite_Destroy(&scene->background);
    Label_Destroy(&scene->label);
    free(scene);
    *scene_ref = NULL;
}
