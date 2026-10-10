#include "SoundEffect.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include <stdlib.h>
#include <string.h>

static uint32_t SoundEffect__U32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t SoundEffect__U16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

SoundEffect *SoundEffect_Load(const char *path, const int output_rate) {
    if (output_rate <= 0 || !FileManager_FileExists(path)) {
        return NULL;
    }
    MpqStream     *stream = FileManager_OpenFile(path);
    const uint32_t size   = MpqStream_GetSize(stream);
    uint8_t       *data   = malloc(size > 0 ? size : 1);
    FAIL_IF_NULL(data);
    MpqStream_Read(stream, data, 0, size);
    MpqStream_Destroy(&stream);

    if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        free(data);
        return NULL;
    }

    // Find the format and the sample data
    int            format   = 0;
    int            channels = 0;
    int            rate     = 0;
    int            bits     = 0;
    const uint8_t *pcm      = NULL;
    uint32_t       pcm_size = 0;
    uint32_t       position = 12;
    while (position + 8 <= size) {
        const uint32_t chunk_size = SoundEffect__U32(data + position + 4);
        const uint8_t *chunk      = data + position + 8;
        if (position + 8 + chunk_size > size) {
            pcm_size = size - (position + 8); // truncated data chunk: use what's there
        }
        if (memcmp(data + position, "fmt ", 4) == 0 && chunk_size >= 16) {
            format   = SoundEffect__U16(chunk);
            channels = SoundEffect__U16(chunk + 2);
            rate     = (int)SoundEffect__U32(chunk + 4);
            bits     = SoundEffect__U16(chunk + 14);
        } else if (memcmp(data + position, "data", 4) == 0) {
            pcm      = chunk;
            pcm_size = position + 8 + chunk_size > size ? size - (position + 8) : chunk_size;
        }
        position += 8 + chunk_size + (chunk_size & 1);
    }

    if (format != 1 || pcm == NULL || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16)) {
        free(data);
        return NULL; // only plain PCM (what the game uses)
    }

    const int bytes_per_frame = channels * (bits / 8);
    const int in_frames       = (int)(pcm_size / (uint32_t)bytes_per_frame);
    if (in_frames <= 0) {
        free(data);
        return NULL;
    }

    // Resample (linear) to the output rate, as stereo
    const double step       = (double)rate / (double)output_rate;
    const int    out_frames = (int)((double)in_frames / step);
    SoundEffect *sound      = calloc(1, sizeof(SoundEffect));
    FAIL_IF_NULL(sound);
    sound->frame_count = out_frames > 0 ? out_frames : 1;
    sound->samples     = malloc(sizeof(int16_t) * 2 * (size_t)sound->frame_count);
    FAIL_IF_NULL(sound->samples);

    for (int f = 0; f < sound->frame_count; f++) {
        const double source = (double)f * step;
        const int    i0     = (int)source;
        const int    i1     = i0 + 1 < in_frames ? i0 + 1 : i0;
        const double t      = source - (double)i0;
        for (int c = 0; c < 2; c++) {
            const int channel = channels == 2 ? c : 0;
            double    a;
            double    b;
            if (bits == 16) {
                a = (int16_t)SoundEffect__U16(pcm + ((size_t)i0 * bytes_per_frame) + (channel * 2));
                b = (int16_t)SoundEffect__U16(pcm + ((size_t)i1 * bytes_per_frame) + (channel * 2));
            } else {
                a = ((int)pcm[((size_t)i0 * bytes_per_frame) + channel] - 128) * 256;
                b = ((int)pcm[((size_t)i1 * bytes_per_frame) + channel] - 128) * 256;
            }
            sound->samples[(f * 2) + c] = (int16_t)(a + ((b - a) * t));
        }
    }

    free(data);
    return sound;
}

void SoundEffect_Destroy(SoundEffect **sound) {
    if (sound == NULL || *sound == NULL) {
        return;
    }
    free((*sound)->samples);
    free(*sound);
    *sound = NULL;
}
