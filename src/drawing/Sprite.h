#ifndef ABYSS_SPRITE_H
#define ABYSS_SPRITE_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Sprite Sprite;

Sprite *Sprite_Create(const char *path, const char *palette_name);
// With its colours shifted: each pixel's palette index looked up in remap[256] first (an item's tint)
Sprite  *Sprite_CreateShifted(const char *path, const char *palette_name, const uint8_t *remap);
void     Sprite_Destroy(Sprite **sprite);
void     Sprite_SetBlendMode(const Sprite *sprite, SDL_BlendMode blend_mode);
void     Sprite_SetAlphaMod(const Sprite *sprite, uint8_t alpha); // how much of it is drawn (blended and added)
void     Sprite_SetPlayLength(Sprite *sprite, double play_length);
void     Sprite_Draw(const Sprite *sprite, uint8_t frame_index, int x, int y);
void     Sprite_DrawMulti(const Sprite *sprite, uint8_t frame_index, int x, int y, int frames_x, int frames_y);
void     Sprite_DrawAnimated(Sprite *sprite, int x, int y);
uint32_t Sprite_GetFrameCount(const Sprite *sprite);
uint32_t Sprite_GetDirectionCount(const Sprite *sprite);
void     Sprite_GetFrameOffset(const Sprite *sprite, int frame_index, int *offset_x, int *offset_y);
// Draws one frame with its top-left corner at (x, y), scaled; ignores the frame offsets.
void Sprite_DrawScaled(const Sprite *sprite, int frame_index, int x, int y, float scale);
// Draws one frame stretched into the given rectangle; ignores the frame offsets.
void Sprite_DrawStretched(const Sprite *sprite, int frame_index, int x, int y, int width, int height);
void Sprite_GetFrameSize(const Sprite *sprite, int frame_index, int *width, int *height);
// Draws part of a frame (section, in the frame's pixels) with the frame's top-left corner at (x, y)
void Sprite_DrawSection(const Sprite *sprite, int frame_index, int x, int y, const SDL_Rect *section);

#endif // ABYSS_SPRITE_H
