#ifndef ABYSS_SOUND_EFFECT_H
#define ABYSS_SOUND_EFFECT_H

#include <stdint.h>

// A short sound (WAV from the MPQs), decoded once into 16-bit stereo at the output rate, ready to mix.
typedef struct SoundEffect {
    int16_t *samples; // interleaved left/right
    int      frame_count;
} SoundEffect;

SoundEffect *SoundEffect_Load(const char *path, int output_rate); // NULL if missing or not PCM WAV
void         SoundEffect_Destroy(SoundEffect **sound);

#endif // ABYSS_SOUND_EFFECT_H
