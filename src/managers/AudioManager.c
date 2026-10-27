#include "AudioManager.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Logging.h"
#include "../managers/VideoManager.h"
#include <stdio.h>
#include <string.h>

#define AUDIO_MAX_VOICES 32

typedef struct AudioVoice {
    const SoundEffect *sound;    // NULL = free
    int                position; // next sample (interleaved left/right)
    float              volume;
    bool               ui;
} AudioVoice;

typedef struct AudioManager {
    bool                audio_available;
    bool                audio_mute;
    SDL_AudioDeviceID   audio_device_id;
    SDL_AudioSpec       audio_spec;
    enum AVSampleFormat out_sample_format;
    AVChannelLayout     channel_layout;
    struct AudioStream *background_music;
    float               volume[AUDIO_SET_VOLUME_TYPE_MAX];
    float               volume_actual[AUDIO_SET_VOLUME_TYPE_MAX];
    AudioVoice          voices[AUDIO_MAX_VOICES];
    AudioVoice          speech; // one line of speech at a time (NPCs), at the speech volume
} AudioManager;

AudioManager *audio_manager;

void AudioManager_FillBuffer(void *userdata, Uint8 *stream, int len) {
    memset(stream, 0, len);

    if (len & 1) {
        LOG_WARN("Audio buffer length is not even, dropping samples...");
        return;
    }

    AudioManager *manager = (AudioManager *)userdata;

    if (audio_manager->audio_mute) {
        return;
    }

    float volume_bgm    = audio_manager->volume_actual[AUDIO_SET_VOLUME_TYPE_MUSIC];
    float volume_master = audio_manager->volume_actual[AUDIO_SET_VOLUME_TYPE_MASTER];
    float volume_sfx    = audio_manager->volume_actual[AUDIO_SET_VOLUME_TYPE_SFX];
    float volume_ui     = audio_manager->volume_actual[AUDIO_SET_VOLUME_TYPE_UI];
    float volume_speech = audio_manager->volume_actual[AUDIO_SET_VOLUME_TYPE_SPEECH];

    for (int i = 0; i < len; i += 2) {
        int32_t sample = 0;

        if (VideoManager_IsPlayingVideo()) {
            sample = VideoManager_GetAudioSample();
        } else {
            if (manager->background_music != NULL) {
                sample += (int32_t)((double)AudioStream_GetSample(manager->background_music) * volume_bgm);
            }
        }

        // Speech
        if (manager->speech.sound != NULL) {
            sample += (int32_t)((float)manager->speech.sound->samples[manager->speech.position] *
                                manager->speech.volume * volume_speech);
            if (++manager->speech.position >= manager->speech.sound->frame_count * 2) {
                manager->speech.sound = NULL;
            }
        }

        // Sound effects (16-bit stereo at the device rate, like the output)
        for (int v = 0; v < AUDIO_MAX_VOICES; v++) {
            AudioVoice *voice = &manager->voices[v];
            if (voice->sound == NULL) {
                continue;
            }
            const float volume  = voice->volume * (voice->ui ? volume_ui : volume_sfx);
            sample             += (int32_t)((float)voice->sound->samples[voice->position] * volume);
            if (++voice->position >= voice->sound->frame_count * 2) {
                voice->sound = NULL;
            }
        }

        if (sample < -32768) {
            sample = -32768;
        } else if (sample > 32767) {
            sample = 32767;
        }

        *(int16_t *)&stream[i] = (int16_t)((float)sample * volume_master);
    }
}

void AudioManager_InitSingleton(void) {
    LOG_INFO("Initializing audio...");

    audio_manager = malloc(sizeof(AudioManager));
    memset(audio_manager, 0, sizeof(AudioManager));

    audio_manager->audio_mute = false;

    SDL_AudioSpec want = {.freq     = 48000,
                          .format   = AUDIO_S16,
                          .channels = 2,
                          .samples  = 512,
                          .callback = AudioManager_FillBuffer,
                          .userdata = audio_manager};

    LOG_DEBUG("Requested audio spec: %d Hz, %d channels, %d samples", want.freq, want.channels, want.samples);

    if ((audio_manager->audio_device_id =
             SDL_OpenAudioDevice(NULL, 0, &want, &audio_manager->audio_spec,
                                 SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE)) < 2) {
        LOG_WARN("Failed to open audio: %s", SDL_GetError());
        audio_manager->audio_available = false;
        return;
    }

    audio_manager->audio_available = true;

    LOG_DEBUG("Obtained audio spec: %d Hz, %d channels, %d samples", audio_manager->audio_spec.freq,
              audio_manager->audio_spec.channels, audio_manager->audio_spec.samples);

    switch (audio_manager->audio_spec.format) {
    case AUDIO_U8:
        audio_manager->out_sample_format = AV_SAMPLE_FMT_U8;
        break;
    case AUDIO_S16SYS:
        audio_manager->out_sample_format = AV_SAMPLE_FMT_S16;
        break;
    case AUDIO_S32SYS:
        audio_manager->out_sample_format = AV_SAMPLE_FMT_S32;
        break;
    case AUDIO_F32SYS:
        audio_manager->out_sample_format = AV_SAMPLE_FMT_FLT;
        break;
    default:
        LOG_FATAL("Invalid audio spec format: 0x%04X", audio_manager->audio_spec.format);
    }

    AudioManager_SetVolume(AUDIO_SET_VOLUME_TYPE_MASTER, AbyssConfiguration_GetMasterVolume());
    AudioManager_SetVolume(AUDIO_SET_VOLUME_TYPE_MUSIC, AbyssConfiguration_GetMusicVolume());
    AudioManager_SetVolume(AUDIO_SET_VOLUME_TYPE_SFX, AbyssConfiguration_GetSfxVolume());
    AudioManager_SetVolume(AUDIO_SET_VOLUME_TYPE_UI, AbyssConfiguration_GetUiVolume());
    AudioManager_SetVolume(AUDIO_SET_VOLUME_TYPE_SPEECH, AbyssConfiguration_GetSpeechVolume());

    av_channel_layout_default(&audio_manager->channel_layout, audio_manager->audio_spec.channels);
    SDL_PauseAudioDevice(audio_manager->audio_device_id, SDL_FALSE);
}

void AudioManager_DestroySingleton(void) {
    LOG_INFO("Finalizing audio...");

    if (audio_manager->audio_available) {
        SDL_PauseAudioDevice(audio_manager->audio_device_id, SDL_TRUE);
        SDL_CloseAudioDevice(audio_manager->audio_device_id);
    }

    if (audio_manager->background_music != NULL) {
        AudioStream_Destroy(&audio_manager->background_music);
    }

    free(audio_manager);
    audio_manager = NULL;
}

void AudioManager_Update(void) {}

static char current_music_path[4096];

void AudioManager_PlayMusic(const char *path, const bool loop) {
    if (audio_manager == NULL) {
        return; // no sound (a dedicated server, tests)
    }
    // Keep the current track playing when a scene asks for the music that is already on (e.g. returning to a menu).
    if (audio_manager->background_music != NULL && strcmp(current_music_path, path) == 0) {
        AudioStream_SetLoop(audio_manager->background_music, loop);
        return;
    }

    snprintf(current_music_path, sizeof(current_music_path), "%s", path);

    // Without the music MPQs (d2music.mpq, d2xmusic.mpq) there's just no music
    AudioStream *music = FileManager_FileExists(path) ? AudioStream_Create(path) : NULL;
    if (music == NULL) {
        LOG_WARN("No music: '%s' isn't in the MPQs", path);
    } else {
        AudioStream_SetLoop(music, loop);
        AudioStream_Play(music);
    }

    // Swapped while the audio thread isn't reading it
    AudioStream *old = audio_manager->background_music;
    SDL_LockAudioDevice(audio_manager->audio_device_id);
    audio_manager->background_music = music;
    SDL_UnlockAudioDevice(audio_manager->audio_device_id);
    if (old != NULL) {
        AudioStream_Destroy(&old);
    }
}
void AudioManager_SetVolume(enum AudioSetVolumeType audio_set_volume_type, float volume) {
    if (audio_manager == NULL) {
        return;
    }
    audio_manager->volume[audio_set_volume_type]        = volume;
    audio_manager->volume_actual[audio_set_volume_type] = powf(volume, 2.0f);

    if (audio_manager->volume[audio_set_volume_type] > 1.0f) {
        audio_manager->volume[audio_set_volume_type] = 1.0f;
    } else if (audio_manager->volume[audio_set_volume_type] < 0.0f) {
        audio_manager->volume[audio_set_volume_type] = 0.0f;
    }

    if (audio_manager->volume_actual[audio_set_volume_type] > 1.0f) {
        audio_manager->volume_actual[audio_set_volume_type] = 1.0f;
    } else if (audio_manager->volume_actual[audio_set_volume_type] < 0.0f) {
        audio_manager->volume_actual[audio_set_volume_type] = 0.0f;
    }
}

void AudioManager_PlaySound(const SoundEffect *sound, const float volume, const bool ui) {
    if (audio_manager == NULL || !audio_manager->audio_available || sound == NULL || sound->frame_count <= 0) {
        return;
    }
    SDL_LockAudioDevice(audio_manager->audio_device_id);
    AudioVoice *chosen = NULL;
    int         left   = 0;
    for (int v = 0; v < AUDIO_MAX_VOICES; v++) {
        const AudioVoice *voice = &audio_manager->voices[v];
        // The same sound just started: don't stack it (several monsters hit on the same frame)
        if (voice->sound == sound && voice->position < 2048) {
            SDL_UnlockAudioDevice(audio_manager->audio_device_id);
            return;
        }
    }
    // A free voice; when all are busy, the one closest to finishing makes room
    for (int v = 0; v < AUDIO_MAX_VOICES; v++) {
        AudioVoice *voice = &audio_manager->voices[v];
        if (voice->sound == NULL) {
            chosen = voice;
            break;
        }
        const int remaining = (voice->sound->frame_count * 2) - voice->position;
        if (chosen == NULL || remaining < left) {
            chosen = voice;
            left   = remaining;
        }
    }
    chosen->sound    = sound;
    chosen->position = 0;
    chosen->volume   = volume;
    chosen->ui       = ui;
    SDL_UnlockAudioDevice(audio_manager->audio_device_id);
}

void AudioManager_PlaySpeech(const SoundEffect *sound, const float volume) {
    if (audio_manager == NULL || !audio_manager->audio_available) {
        return;
    }
    SDL_LockAudioDevice(audio_manager->audio_device_id);
    audio_manager->speech.sound    = sound != NULL && sound->frame_count > 0 ? sound : NULL;
    audio_manager->speech.position = 0;
    audio_manager->speech.volume   = volume;
    SDL_UnlockAudioDevice(audio_manager->audio_device_id);
}

void AudioManager_StopSpeech(void) { AudioManager_PlaySpeech(NULL, 0.0f); }

void AudioManager_StopSounds(void) {
    if (audio_manager == NULL) {
        return;
    }
    if (audio_manager->audio_available) {
        SDL_LockAudioDevice(audio_manager->audio_device_id);
    }
    for (int v = 0; v < AUDIO_MAX_VOICES; v++) {
        audio_manager->voices[v].sound = NULL;
    }
    audio_manager->speech.sound = NULL;
    if (audio_manager->audio_available) {
        SDL_UnlockAudioDevice(audio_manager->audio_device_id);
    }
}

int AudioManager_GetOutputRate(void) {
    return audio_manager != NULL && audio_manager->audio_available ? audio_manager->audio_spec.freq : 0;
}

SDL_AudioSpec AudioManager_GetAudioSpec(void) { return audio_manager->audio_spec; }

AVChannelLayout AudioManager_GetChannelLayout(void) { return audio_manager->channel_layout; }

enum AVSampleFormat AudioManager_GetSampleFormat(void) { return audio_manager->out_sample_format; }
