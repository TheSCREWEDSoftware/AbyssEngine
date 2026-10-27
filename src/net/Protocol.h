#ifndef ABYSS_PROTOCOL_H
#define ABYSS_PROTOCOL_H

#include "../game/Character.h"
#include "NetMessage.h"
#include <stdbool.h>
#include <stdint.h>

// Abyss Engine multiplayer: one server holds the game (settings, players, characters, where everyone is) and
// passes it on. It runs inside the hosting player's game, or on its own (AbyssEngine --server). Clients send what
// they do; the server checks it and tells everyone.

#define NET_PROTOCOL_VERSION                                                                                           \
    7 // 2: items on the ground, monsters and combat; 3: chests and doors, parties; 4: magic;
      // 5: elemental damage in blows; 6: skills cast; 7: what players wear (looks), sockets
#define NET_DEFAULT_PORT     4000 // Diablo II's own TCP/IP game port
#define NET_DISCOVERY_PORT   4001 // LAN: "is anyone hosting?" over UDP broadcast
#define NET_LOBBY_PORT       4002 // abyss-lobby: the list of online games
#define NET_MAX_PLAYERS      255  // the host picks 1 to 255 (8 by default, like Diablo II)
#define NET_DEFAULT_PLAYERS  8
#define NET_NAME_MAX         32 // game names and passwords
#define NET_ID_MAX           40 // a computer's id (client and realm ids)
#define NET_CHAT_MAX         160
#define NET_STATE_HZ         15    // positions sent this often
#define NET_GROUND_ITEMS_MAX 4096  // items on the ground in a game (every area)
#define NET_USED_OBJECTS_MAX 8192  // chests, doors, shrines opened in a game (every area)
#define NET_PARTY_RANGE      60.0f // sub-tiles: party members this close to a kill share its experience

// Message types
typedef enum NetMessageType {
    // Client -> server
    MSG_HELLO = 1,   // version, client id, password
    MSG_CHAR_PICK,   // the character to play: hosted games send it whole, dedicated servers just the name
    MSG_CHAR_CREATE, // dedicated server: name, class, expansion, hardcore
    MSG_CHAR_DELETE, // dedicated server: name
    MSG_CHAR_SAVE,   // dedicated server: the character as it is now (the game saved)
    MSG_STATE,       // where I am: map, x, y, turn, mode (map: the map's key, the same for an act's joined outdoors)
    MSG_CHAT,        // text
    MSG_LEAVE,       // leaving on purpose
    // Items on the ground: the server keeps them, so two players can't pick up the same one
    MSG_ITEM_DROP, // map, x, y, item (code, defense, quantity)
    MSG_ITEM_PICK, // item id: "I'd like it" (the first to ask gets it)
    // Monsters: on each map, the player with the lowest id runs them ("the map's owner") and the others show what
    // they're told. The server passes these on.
    MSG_MONSTERS,       // owner: map, count, then index, x, y, hp, max hp, state, turn, vanished for each
    MSG_HIT_MONSTER,    // map, monster index, damage (to the map's owner, who takes it off)
    MSG_MONSTER_KILLED, // owner: map, monster index, killer's player id (the killer and their party get the experience)
    MSG_HIT_PLAYER,   // player id, pvp, attack rating, level, damage: physical, fire, cold, lightning, poison (the one
                      // hit
                      // rolls whether it lands, and resists the elements)
    MSG_OBJECT_USE,   // map, object index: "I'd like to open it" (the first to ask opens it, for everyone)
    MSG_PARTY_INVITE, // player id
    MSG_PARTY_ACCEPT, // the inviter's player id
    MSG_PARTY_LEAVE,
    MSG_CAST,  // map, skill (skills.txt row, 0xFFFF: the bow's attack), level, from x, y, at x, y: the others see it
               // (the caster does the damage)
    MSG_LOOKS, // what I wear and hold (Character_LooksToText), when it changes
    // Server -> client
    MSG_WELCOME = 64, // the game's settings, whether characters are kept here, the realm id
    MSG_REJECT,       // why not (and the connection closes)
    MSG_CHAR_LIST,    // dedicated server: how many characters you have here (MSG_CHAR_ENTRY each follows)
    MSG_CHAR_RESULT,  // ok or why not; ok: the character as the server has it
    MSG_ENTER,        // in you go: your player id, the world seed
    MSG_PLAYER,       // someone in the game: id, name, class, level, hardcore, colour, party (when they join or change)
    MSG_PLAYER_GONE,  // id (also to the character screen)
    MSG_STATES,       // everyone's position
    MSG_CHAT_FROM,    // id (0: the server), text
    MSG_CHAR_ENTRY,   // one of your characters on the server (after MSG_CHAR_LIST)
    MSG_ITEM_ADD,     // an item on the ground: id, dropper's id (0: was there already), map, x, y, item
    MSG_ITEM_GONE,    // id, who picked it up
    // Passed on from another player: their id first, then what they sent
    MSG_MONSTERS_FROM,
    MSG_HIT_MONSTER_FROM,
    MSG_MONSTER_KILLED_FROM, // to the killer and their party nearby, with how many share it at the end
    MSG_HIT_YOU,             // only to the one hit
    MSG_OBJECT_USED,         // who opened it (0: was open already), map, object index
    MSG_PARTY_INVITED,       // who invites you
    MSG_CAST_FROM,           // someone's skill, to see
} NetMessageType;

typedef enum NetDifficulty { DIFFICULTY_NORMAL, DIFFICULTY_NIGHTMARE, DIFFICULTY_HELL, DIFFICULTY_COUNT } NetDifficulty;
typedef enum NetHardcore { HARDCORE_ANY, HARDCORE_SOFTCORE_ONLY, HARDCORE_ONLY, HARDCORE_RULE_COUNT } NetHardcore;
typedef enum NetSpeed { SPEED_NORMAL, SPEED_FAST, SPEED_FASTER, SPEED_FASTEST, SPEED_COUNT } NetSpeed;

// A game's settings (the host picks them; Browse shows them)
typedef struct NetSettings {
    char     name[NET_NAME_MAX];
    char     password[NET_NAME_MAX]; // "" = none
    int      max_players;            // 1..8
    int      difficulty;             // NetDifficulty
    bool     new_characters_only;    // level 1, no experience
    bool     made_here_only;         // characters made in this host's games (its realm)
    int      hardcore;               // NetHardcore
    int      level_min;              // 1..99
    int      level_max;
    bool     pvp;             // players can hurt each other (D1's friendly fire, D2's hostile)
    int      speed;           // NetSpeed: D1's game speed
    int      inventory_pages; // 1..255
    uint32_t seed;            // the world; 0 = a new one when the game starts
    bool     public_game;     // listed on the lobby server (Online)
    bool     lan;             // answers LAN searches
    uint16_t port;
} NetSettings;

void NetSettings_Defaults(NetSettings *settings);
void NetSettings_Write(const NetSettings *settings, NetWriter *writer, bool with_password);
// An item (dropped, picked up): its code, defense, quantity, quality, names and magic
void        NetItem_Write(NetWriter *writer, const Item *item);
void        NetItem_Read(NetReader *reader, Item *item);
void        NetSettings_Read(NetSettings *settings, NetReader *reader);
const char *NetSettings_DifficultyName(int difficulty);
const char *NetSettings_SpeedName(int speed);
const char *NetSettings_HardcoreName(int hardcore);
float       NetSettings_SpeedScale(int speed); // how much faster the game runs (D1: 20, 30, 40, 50 frames a second)

// Whether a character may join a game with these settings; why not, for the player
bool NetSettings_Allows(const NetSettings *settings, const Character *character, const char *realm, const char **why);

// This computer's id: made once, kept in client.id next to abyss.ini (a dedicated server's realm id is its own)
const char *Net_ClientId(void);

// Player colours: each player picks one (Options › Multiplayer); the others see their name and circle in it
#define NET_COLOR_COUNT 12
const char *NetColor_Name(int color);
void        NetColor_Get(int color, uint8_t *r, uint8_t *g, uint8_t *b);

// One game, as Browse lists it (LAN answers and the lobby's list)
typedef struct NetGameInfo {
    char     address[NET_ADDRESS_MAX];
    uint16_t port;
    char     name[NET_NAME_MAX];
    int      players;
    int      max_players;
    int      difficulty;
    bool     locked; // has a password
    bool     dedicated;
    int      speed;
    int      hardcore;
    int      level_min;
    int      level_max;
    bool     new_characters_only;
    bool     made_here_only;
    bool     pvp;
    uint32_t instance; // this run of the server (the same game heard twice, by two addresses, is one)
} NetGameInfo;

// The one-line description servers send to LAN searches and the lobby: "ABYSS <version> <port> <players> ..."
int  NetGameInfo_Format(const NetGameInfo *info, char *text, int size);
bool NetGameInfo_Parse(const char *text, NetGameInfo *info); // address is left as it is

#endif // ABYSS_PROTOCOL_H
