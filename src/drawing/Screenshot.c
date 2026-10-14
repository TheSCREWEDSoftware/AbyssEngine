#include "Screenshot.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#endif

bool Screenshot_Save(char *saved_path, const int size) {
    char folder[4096];
    snprintf(folder, sizeof(folder), "%s/screenshots", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
    _mkdir(folder);
#else
    mkdir(folder, 0755);
#endif
    const time_t now = time(NULL);
    struct tm    local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char path[4200];
    snprintf(path, sizeof(path), "%s/Abyss_%04d-%02d-%02d_%02d-%02d-%02d.bmp", folder, local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);

    int          width;
    int          height;
    SDL_Texture *target = SDL_GetRenderTarget(sdl_renderer); // the frame's 800x600 picture (see Picture.h), if any
    if (target != NULL) {
        SDL_QueryTexture(target, NULL, NULL, &width, &height);
    } else if (SDL_GetRendererOutputSize(sdl_renderer, &width, &height) != 0) {
        return false;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) {
        return false;
    }
    // Read the whole window (black bars and all), not the 800x600 the game draws in
    int logical_w;
    int logical_h;
    SDL_RenderGetLogicalSize(sdl_renderer, &logical_w, &logical_h);
    if (target == NULL) {
        SDL_RenderSetLogicalSize(sdl_renderer, 0, 0);
    }
    const bool read =
        SDL_RenderReadPixels(sdl_renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0;
    if (target == NULL) {
        SDL_RenderSetLogicalSize(sdl_renderer, logical_w, logical_h);
    }
    const bool saved = read && SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    if (saved) {
        LOG_INFO("Screenshot: %s", path);
        if (saved_path != NULL) {
            snprintf(saved_path, (size_t)size, "%s", path);
        }
    } else {
        LOG_WARN("Could not save a screenshot to %s", path);
    }
    return saved;
}
