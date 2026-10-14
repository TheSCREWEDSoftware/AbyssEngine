#include "Picture.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

#define PICTURE_W 800
#define PICTURE_H 600

static SDL_Texture *picture        = NULL;
static bool         drawing        = false;
static bool         warned_blend   = false;
static char         scale_mode[16] = "";

void Picture_Initialize(void) {
    if (!SDL_RenderTargetSupported(sdl_renderer)) {
        LOG_INFO("No render targets: scaling and the picture settings apply from the next start only");
        return;
    }
    picture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, PICTURE_W, PICTURE_H);
    if (picture == NULL) {
        LOG_WARN("Could not make the frame picture: %s", SDL_GetError());
    }
}

void Picture_Finalize(void) {
    if (picture != NULL) {
        SDL_DestroyTexture(picture);
        picture = NULL;
    }
}

void Picture_BeginFrame(void) { drawing = picture != NULL && SDL_SetRenderTarget(sdl_renderer, picture) == 0; }

// A blend the renderer may not have (the software one has none of these): false when it can't
static bool Picture__Blend(const SDL_BlendFactor src, const SDL_BlendFactor dst, const SDL_BlendOperation op,
                           SDL_Texture *texture) {
    const SDL_BlendMode mode =
        SDL_ComposeCustomBlendMode(src, dst, op, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    const int result =
        texture != NULL ? SDL_SetTextureBlendMode(texture, mode) : SDL_SetRenderDrawBlendMode(sdl_renderer, mode);
    if (result != 0 && !warned_blend) {
        warned_blend = true;
        LOG_WARN("This renderer can't blend for brightness / contrast / gamma: %s", SDL_GetError());
    }
    return result == 0;
}

static void Picture__Fill(const float level) {
    const uint8_t v = (uint8_t)lroundf(fminf(fmaxf(level, 0.0f), 1.0f) * 255.0f);
    SDL_SetRenderDrawColor(sdl_renderer, v, v, v, 255);
    SDL_RenderFillRect(sdl_renderer, NULL);
}

// The picture turned over: 1 - v (white times one minus what's there)
static bool Picture__Invert(void) {
    if (!Picture__Blend(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD, NULL)) {
        return false;
    }
    Picture__Fill(1.0f);
    return true;
}

// v·f
static void Picture__Gain(const float factor) {
    if (factor < 1.0f) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_MOD);
        Picture__Fill(factor);
    } else if (factor > 1.0f &&
               Picture__Blend(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, NULL)) {
        Picture__Fill(factor - 1.0f); // v·(f-1) + v
    }
}

// v + offset
static void Picture__Offset(const float offset) {
    if (offset > 0.0f) {
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_ADD);
        Picture__Fill(offset);
    } else if (offset < 0.0f && Picture__Invert()) {
        // Down: not every renderer subtracts, so turn it over (1 - v), add, and turn it back: 1 - (1 - v + o)
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_ADD);
        Picture__Fill(-offset);
        Picture__Invert();
    }
}

void Picture_EndFrame(void) {
    if (!drawing) {
        return;
    }
    drawing = false;
    SDL_SetRenderTarget(sdl_renderer, NULL); // the window again, with its 800x600 logical size (black bars)

    // Scaling, live
    const char *quality = AbyssConfiguration_GetScaleQuality();
    if (strcmp(quality, scale_mode) != 0) {
        snprintf(scale_mode, sizeof(scale_mode), "%s", quality);
        SDL_SetTextureScaleMode(picture, strcmp(quality, "nearest") == 0 ? SDL_ScaleModeNearest
                                         : strcmp(quality, "best") == 0  ? SDL_ScaleModeBest
                                                                         : SDL_ScaleModeLinear);
    }

    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(sdl_renderer);
    SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorMod(picture, 255, 255, 255);
    SDL_SetTextureAlphaMod(picture, 255);
    SDL_RenderCopy(sdl_renderer, picture, NULL, NULL);

    int brightness;
    int contrast;
    int gamma;
    AbyssConfiguration_GetPicture(&brightness, &contrast, &gamma);
    if (brightness == 50 && contrast == 50 && gamma == 50) {
        return;
    }

    // Gamma (v = the picture, 0..1): higher is brighter in the dark parts. Curved by blending the picture with
    // itself: darker v - t·v(1-v), brighter v + t·v(1-v), t set so the middle grey lands where v^e would put it.
    const float exponent = powf(2.0f, (float)(50 - gamma) / 50.0f); // 0.5 .. 2
    const float t        = fabsf(powf(0.5f, exponent) - 0.5f) * 4.0f;
    if (gamma != 50 && t > 0.0f) {
        const uint8_t mod = (uint8_t)lroundf(fminf(t, 1.0f) * 255.0f);
        SDL_SetTextureColorMod(picture, mod, mod, mod); // the source is t·v
        SDL_SetTextureAlphaMod(picture, mod);           // and its alpha t
        const bool ok = gamma < 50 ? Picture__Blend(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                                    SDL_BLENDOPERATION_ADD, picture) // t·v·v + v(1-t)
                                   : Picture__Blend(SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE,
                                                    SDL_BLENDOPERATION_ADD, picture); // t·v(1-v) + v
        if (ok) {
            SDL_RenderCopy(sdl_renderer, picture, NULL, NULL);
        }
    }

    // Contrast around the middle grey, then brightness: v·f + (0.5(1-f) + offset)
    const float factor = powf(2.0f, (float)(contrast - 50) / 50.0f); // 0.5 .. 2
    const float offset = (float)(brightness - 50) / 200.0f + (0.5f * (1.0f - factor));
    // Above 1 the gain would clip what the offset pulls back down, so a lowering offset goes first there:
    // (v + offset/f)·f is the same line
    if (factor > 1.0f && offset < 0.0f) {
        Picture__Offset(offset / factor);
        Picture__Gain(factor);
    } else {
        Picture__Gain(factor);
        Picture__Offset(offset);
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
}
