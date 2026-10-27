#ifndef ABYSS_SOUNDS_H
#define ABYSS_SOUNDS_H

#include <stdbool.h>

// The game's sound effects by name (sounds.txt): "cursor_button_click", "zombie_attack_1", ... A name that starts a
// group ("Group Size" > 1) plays one of the group's sounds at random. Files are loaded on first use and kept.

typedef enum SoundKind { SOUND_KIND_SFX, SOUND_KIND_UI } SoundKind;

typedef enum MonsterSound { MONSTER_SOUND_ATTACK, MONSTER_SOUND_HIT, MONSTER_SOUND_DEATH } MonsterSound;

bool Sounds_Play(const char *name, SoundKind kind); // false if there's no such sound
// A sound in the world, dx / dy sub-tiles from the listener: quieter further away (full within
// SOUND_FULL_DISTANCE, silent past SOUND_SILENT_DISTANCE) and from the side it's on
#define SOUND_FULL_DISTANCE   6.0f
#define SOUND_SILENT_DISTANCE 40.0f
bool Sounds_PlayAt(const char *name, SoundKind kind, float dx, float dy);
bool Sounds_Exists(const char *name);
// Speech (sounds.txt names too: "akara_act1_intro"): one at a time, at the Speech volume. Returns how long it lasts in
// milliseconds (0: it's in the table but can't be played, no speech MPQ or no sound device; -1: no such sound)
int  Sounds_PlaySpeech(const char *name);
void Sounds_StopSpeech(void);
// The area's background loop (sounds.txt name; NULL or "": none)
void Sounds_PlayAmbience(const char *name);
// The name of a monster's sound (monstats.txt "MonSound" -> MonSounds.txt), NULL if it has none
const char *Sounds_MonsterSound(const char *monstats_id, MonsterSound which);
void        Sounds_Release(void);

#endif // ABYSS_SOUNDS_H
