#ifndef ABYSS_CLIENT_H
#define ABYSS_CLIENT_H

#include "Protocol.h"
#include <stdbool.h>
#include <stdint.h>

// This player's side of a multiplayer game: one connection to the game's server (someone else's, our own when
// hosting, or a dedicated one). Multiplayer_Update pumps it every frame.

typedef enum NetClientStage {
    NETC_OFF,        // not in a multiplayer game
    NETC_CONNECTING, // reaching the server
    NETC_HELLO,      // connected, waiting to be let in
    NETC_CHARACTERS, // let in: pick a character (the character screen)
    NETC_ENTERING,   // picked: waiting for the server to agree
    NETC_IN_GAME,
    NETC_FAILED, // NetClient_Error says why (rejected, lost the connection)
} NetClientStage;

typedef struct NetPlayer {
    int            id;
    char           name[CHARACTER_NAME_MAX];
    CharacterClass class_id;
    int            level;
    bool           hardcore;
    int            color;     // NetColor: theirs
    int            party;     // their party's id, 0 = none
    bool           has_state; // where they are (MSG_STATES)
    int            place;     // Places index (same seed: same map)
    float          x;         // sub-tiles
    float          y;
    float          from_x; // shown moving smoothly from the last position to this one
    float          from_y;
    uint64_t       moved_at;
    int            turn;
    char           mode[4]; // "tn", "nu", "wl", "rn", ...
} NetPlayer;

bool           NetClient_Connect(const char *host, uint16_t port, const char *password);
void           NetClient_Disconnect(void); // says goodbye; back to NETC_OFF
void           NetClient_Update(uint64_t now);
NetClientStage NetClient_Stage(void);
const char    *NetClient_Error(void);
const char    *NetClient_Address(void); // "host:port"

// After the welcome
const NetSettings *NetClient_Settings(void);
bool               NetClient_ServerKeepsCharacters(void);        // dedicated: characters live on the server
const char        *NetClient_Realm(void);                        // what characters made in this game are tagged with
int                NetClient_Characters(const Character **list); // dedicated: yours on the server
int                NetClient_CharacterListVersion(void);         // goes up when the list changes
const char        *NetClient_TakeResult(bool *ok);               // the server's answer to pick/create (NULL: none)

void NetClient_Pick(const Character *character);                                                 // to play
void NetClient_Create(const char *name, CharacterClass class_id, bool expansion, bool hardcore); // dedicated
void NetClient_Delete(const char *name);                                                         // dedicated

// In the game
int        NetClient_MyId(void);
uint32_t   NetClient_Seed(void);
void       NetClient_SendState(int place, float x, float y, int turn, const char *mode);
void       NetClient_SendChat(const char *text);
int        NetClient_Players(NetPlayer **players);   // the others in the game
bool       NetClient_TakeChat(char *text, int size); // the next line to show ("Name: hello"), false when none
NetPlayer *NetClient_Player(int id);
// The game's own messages (Protocol.h: items, monsters, hits): sending, and reading what came in, in order (the
// reader is good until the next call)
void NetClient_Send(uint8_t type, const NetWriter *writer);
bool NetClient_TakeGameMessage(uint8_t *type, NetReader *reader);
int  NetClient_PlayersVersion(void); // goes up when someone joins or leaves
// Someone's playing a character of this name (seen from the character screen too)
bool NetClient_NameInGame(const char *name);
int  NetClient_MyParty(void);    // our party's id, 0 = none
int  NetClient_InviteFrom(void); // who last invited us to their party (0: nobody)
void NetClient_ClearInvite(void);

#endif // ABYSS_CLIENT_H
