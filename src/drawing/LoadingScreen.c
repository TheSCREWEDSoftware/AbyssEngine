#include "LoadingScreen.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/ResourcePaths.h"
#include "Label.h"
#include "Sprite.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOADING_SHOW_AFTER  120 // ms before the screen appears
#define LOADING_FRAME_MS    33  // redraw at most this often
#define LOADING_MAX_KNOWN   64  // remembered loads
#define LOADING_GUESS_READS 400 // first time: reads for the bar to reach ~63%
#define LOADING_FILE        "loading.txt"
#define BAR_WIDTH           240
#define BAR_HEIGHT          8

typedef struct KnownLoad {
    char caption[96];
    int  reads;
} KnownLoad;

static int       loading_depth     = 0;
static bool      loading_drawing   = false;
static uint64_t  loading_start     = 0;
static uint64_t  loading_last_draw = 0;
static char      loading_text[96]  = "";
static int       loading_reads     = 0;
static int       loading_expected  = 0; // 0 = unknown
static float     loading_shown     = 0; // progress shown so far (never goes back)
static Sprite   *loading_sprite    = NULL;
static Label    *loading_label     = NULL;
static KnownLoad known_loads[LOADING_MAX_KNOWN];
static int       known_count = -1; // -1 = not read yet

// ---------------------------------------------------------------------------------------------------------------------
// Remembered loads

static void LoadingScreen__Path(char *path, const size_t size) {
    snprintf(path, size, "%s/%s", AbyssConfiguration_GetConfigFolder(), LOADING_FILE);
}

static void LoadingScreen__ReadKnown(void) {
    known_count = 0;
    char path[4200];
    LoadingScreen__Path(path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[160];
    while (known_count < LOADING_MAX_KNOWN && fgets(line, sizeof(line), file) != NULL) {
        char *tab = strchr(line, '\t');
        if (tab == NULL) {
            continue;
        }
        *tab = '\0';
        snprintf(known_loads[known_count].caption, sizeof(known_loads[0].caption), "%.95s", line);
        known_loads[known_count].reads = atoi(tab + 1);
        known_count++;
    }
    fclose(file);
}

static void LoadingScreen__WriteKnown(void) {
    char path[4200];
    LoadingScreen__Path(path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    for (int i = 0; i < known_count; i++) {
        fprintf(file, "%s\t%d\n", known_loads[i].caption, known_loads[i].reads);
    }
    fclose(file);
}

static KnownLoad *LoadingScreen__Find(const char *caption) {
    if (known_count < 0) {
        LoadingScreen__ReadKnown();
    }
    for (int i = 0; i < known_count; i++) {
        if (strcmp(known_loads[i].caption, caption) == 0) {
            return &known_loads[i];
        }
    }
    return NULL;
}

static void LoadingScreen__Remember(const char *caption, const int reads) {
    KnownLoad *known = LoadingScreen__Find(caption);
    if (known == NULL) {
        if (known_count >= LOADING_MAX_KNOWN) {
            return;
        }
        known = &known_loads[known_count++];
        snprintf(known->caption, sizeof(known->caption), "%s", caption);
    }
    if (known->reads != reads) {
        known->reads = reads;
        LoadingScreen__WriteKnown();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing

static float LoadingScreen__Progress(void) {
    float progress;
    if (loading_expected > 0) {
        progress = (float)loading_reads / (float)loading_expected;
    } else {
        progress = 1.0f - expf(-(float)loading_reads / LOADING_GUESS_READS); // creeps towards the end
    }
    progress = progress > 0.98f ? 0.98f : progress; // full only when actually done
    if (progress < loading_shown) {
        progress = loading_shown;
    }
    loading_shown = progress;
    return progress;
}

static void LoadingScreen__Draw(void) {
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

    const float progress = LoadingScreen__Progress();
    int         bottom   = 330;
    if (loading_sprite != NULL && Sprite_GetFrameCount(loading_sprite) > 0) {
        // The picture's animation (the door opening) follows the progress instead of looping
        const int frames = (int)Sprite_GetFrameCount(loading_sprite);
        int       frame  = (int)(progress * (float)frames);
        frame            = frame >= frames ? frames - 1 : frame;
        int w;
        int h;
        Sprite_GetFrameSize(loading_sprite, frame, &w, &h);
        Sprite_DrawScaled(loading_sprite, frame, 400 - (w / 2), 300 - (h / 2), 1.0f);
        bottom = 300 + (h / 2) + 14;
    }

    // Progress bar
    const SDL_Rect frame_rect = {400 - (BAR_WIDTH / 2) - 2, bottom - 2, BAR_WIDTH + 4, BAR_HEIGHT + 4};
    const SDL_Rect back       = {400 - (BAR_WIDTH / 2), bottom, BAR_WIDTH, BAR_HEIGHT};
    const SDL_Rect fill       = {400 - (BAR_WIDTH / 2), bottom, (int)(progress * BAR_WIDTH), BAR_HEIGHT};
    SDL_SetRenderDrawColor(sdl_renderer, 120, 100, 60, 255);
    SDL_RenderDrawRect(sdl_renderer, &frame_rect);
    SDL_SetRenderDrawColor(sdl_renderer, 30, 22, 12, 255);
    SDL_RenderFillRect(sdl_renderer, &back);
    SDL_SetRenderDrawColor(sdl_renderer, 199, 179, 119, 255);
    SDL_RenderFillRect(sdl_renderer, &fill);

    Label_SetText(loading_label, loading_text);
    Label_Draw(loading_label, 400, bottom + BAR_HEIGHT + 16);
    SDL_RenderPresent(sdl_renderer);

    loading_drawing = false;
}

void LoadingScreen_Tick(void) {
    if (loading_depth == 0 || loading_drawing) {
        return;
    }
    loading_reads++;
    const uint64_t now = SDL_GetTicks64();
    if (now - loading_start < LOADING_SHOW_AFTER || now - loading_last_draw < LOADING_FRAME_MS) {
        return;
    }
    loading_last_draw = now;
    LoadingScreen__Draw();
}

void LoadingScreen_Begin(const char *text) {
    if (loading_depth++ > 0) {
        return; // already loading; keep the outer caption
    }
    loading_start     = SDL_GetTicks64();
    loading_last_draw = 0;
    loading_reads     = 0;
    loading_shown     = 0;
    snprintf(loading_text, sizeof(loading_text), "%s", text != NULL ? text : "Loading...");
    const KnownLoad *known = LoadingScreen__Find(loading_text);
    loading_expected       = known != NULL ? known->reads : 0;
    FileManager_SetReadHook(LoadingScreen_Tick);
}

void LoadingScreen_End(void) {
    if (loading_depth == 0 || --loading_depth > 0) {
        return;
    }
    FileManager_SetReadHook(NULL);
    // Remember how big this load was, for an accurate bar next time (only loads that were slow enough to show)
    if (loading_last_draw != 0 || loading_reads > 50) {
        LoadingScreen__Remember(loading_text, loading_reads);
    }
    if (loading_last_draw != 0) {
        loading_reads = loading_expected = 1; // finish the bar and the picture on the way out
        loading_shown                    = 1.0f;
        loading_drawing                  = false;
        LoadingScreen__Draw();
    }
}

void LoadingScreen_Release(void) {
    if (loading_sprite != NULL) {
        Sprite_Destroy(&loading_sprite);
    }
    Label_Destroy(&loading_label);
}
