#ifndef ABYSS_AUDIOMAN_H
#define ABYSS_AUDIOMAN_H

#include "../audio/AudioStream.h"
#include "../audio/SoundEffect.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

enum AudioSetVolumeType {
    AUDIO_SET_VOLUME_TYPE_MASTER,
    AUDIO_SET_VOLUME_TYPE_MUSIC,
    AUDIO_SET_VOLUME_TYPE_SFX,
    AUDIO_SET_VOLUME_TYPE_UI,
    AUDIO_SET_VOLUME_TYPE_SPEECH, // kept for when speech plays (NPC greetings, quest dialogue)
    AUDIO_SET_VOLUME_TYPE_MAX
};

void AudioManager_InitSingleton(void);
void AudioManager_DestroySingleton(void);
void AudioManager_Update(void);
void AudioManager_PlayMusic(const char *path, bool loop);
void AudioManager_SetVolume(enum AudioSetVolumeType audio_set_volume_type, float volume);
void AudioManager_FillBuffer(void *userdata, Uint8 *stream, int len);
// Sound effects: mixed over the music; the sound must stay alive while it plays (see AudioManager_StopSounds).
void AudioManager_PlaySound(const SoundEffect *sound, float volume, bool ui);
void AudioManager_StopSounds(void);
// Speech (NPCs talking): one at a time, a new line cuts off the last; at the Speech volume
void                AudioManager_PlaySpeech(const SoundEffect *sound, float volume);
void                AudioManager_StopSpeech(void);
int                 AudioManager_GetOutputRate(void); // 0 when there's no audio device
SDL_AudioSpec       AudioManager_GetAudioSpec(void);
AVChannelLayout     AudioManager_GetChannelLayout(void);
enum AVSampleFormat AudioManager_GetSampleFormat(void);

#endif // ABYSS_AUDIOMAN_H
