#ifndef ABYSS_MULTIPLAYER_H
#define ABYSS_MULTIPLAYER_H

#include "Client.h"
#include "Protocol.h"
#include "Server.h"
#include <stdbool.h>

// Multiplayer from the game's side: hosting (our own server, which we then join like everyone else), the update
// the main loop calls every frame, and Browse's two lists (games on the local network, games the lobby server
// knows).

bool Multiplayer_Host(const NetSettings *settings, char *error, int error_size); // starts the server and joins it
bool Multiplayer_IsHosting(void);
const NetServer *Multiplayer_Server(void);
void             Multiplayer_Leave(void); // leaves the game (and stops the server when hosting)
void             Multiplayer_Update(void);
bool             Multiplayer_Active(void); // in (or getting into) a multiplayer game

// The host's settings as last used (host.ini next to abyss.ini)
void Multiplayer_LoadHostSettings(NetSettings *settings);
void Multiplayer_SaveHostSettings(const NetSettings *settings);

// Browse
typedef enum BrowseSource { BROWSE_LAN, BROWSE_ONLINE } BrowseSource;
void        Browse_Start(BrowseSource source); // asks again (clears the list)
void        Browse_Update(void);
bool        Browse_Searching(void);
int         Browse_Games(const NetGameInfo **games);
const char *Browse_Status(void); // "Searching...", "3 games", "No lobby server set (...)"
void        Browse_Stop(void);

#endif // ABYSS_MULTIPLAYER_H
