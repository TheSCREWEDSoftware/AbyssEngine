#ifndef ABYSS_SERVER_H
#define ABYSS_SERVER_H

#include "Protocol.h"
#include <stdbool.h>
#include <stdint.h>

// The game server: the one place that holds the game. It takes connections, checks who may join (version,
// password, room, the character rules), keeps the players' characters when it's dedicated, knows where everyone
// is and passes it on (15 times a second), relays chat, answers LAN searches and tells the lobby server about the
// game when it's public.
//
// Hosted: it runs inside the host's game (Multiplayer_Host), the host joining it like everyone else; characters
// stay on each player's computer, tagged with the host's id when they're made there. Dedicated
// (AbyssEngine --server): it runs on its own and keeps the characters itself, in server/characters/<player's id>/.

typedef struct NetServer NetServer;

NetServer         *NetServer_Start(const NetSettings *settings, bool dedicated, char *error, int error_size);
void               NetServer_Update(NetServer *server, uint64_t now_ms);
void               NetServer_Stop(NetServer **server); // tells everyone first
const NetSettings *NetServer_Settings(const NetServer *server);
int                NetServer_PlayerCount(const NetServer *server); // in the game
const char        *NetServer_Realm(const NetServer *server);       // the id characters made here are tagged with

// The dedicated server's folder (server/ next to abyss.ini) and its settings file there (server.ini)
const char *NetServer_Folder(void);
bool        NetServer_LoadSettings(NetSettings *settings); // writes a commented server.ini first if there's none

#endif // ABYSS_SERVER_H
