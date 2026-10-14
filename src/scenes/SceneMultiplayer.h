#ifndef ABYSS_SCENE_MULTIPLAYER_H
#define ABYSS_SCENE_MULTIPLAYER_H

#include "Scene.h"

// Main menu › Multiplayer: Browse (games on the local network, or online through the lobby server), Direct IP
// (an address, with or without a port), and Host (your own game, with its settings). Joining goes on to the
// character screen; src/net has how the game itself is shared.
DESCRIBE_SCENE_CALLBACKS(MultiplayerMenu);

void *MultiplayerMenu_Create(void);
void  MultiplayerMenu_Render(void *scene_ref);
void  MultiplayerMenu_Update(void *scene_ref, uint64_t delta);
void  MultiplayerMenu_Free(void **scene_ref);

#endif // ABYSS_SCENE_MULTIPLAYER_H
