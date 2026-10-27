#include "Server.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "../common/Mods.h"
#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#endif

#define MAX_CONNECTIONS  (NET_MAX_PLAYERS + 4) // a few more, to tell them the game is full
#define HELLO_TIMEOUT_MS 10000                 // to say who they are
#define NET_BEHIND_BYTES (256 * 1024)          // queued for a player: they're not keeping up (skip what can be skipped)
#define LOBBY_EVERY_MS   30000                 // re-register a public game this often
#define LOBBY_TIMEOUT_MS 5000
#define MAX_SERVER_CHARS 32 // characters one player keeps on a dedicated server

typedef enum PeerStage { STAGE_HELLO, STAGE_CHARACTERS, STAGE_IN_GAME } PeerStage;

typedef struct Peer {
    NetConnection *connection;
    PeerStage      stage;
    uint64_t       since;
    char           client_id[NET_ID_MAX];
    int            id;    // player id in the game (1..255)
    int            color; // NetColor
    Character      character;
    bool           has_state;
    int            place;
    float          x;
    float          y;
    int            turn;
    char           mode[4];
    int            life; // percent, and their level (MSG_STATE)
    int            level;
    int            party;                           // the party's id (its first member's player id), 0 = none
    char           looks[CHARACTER_LOOKS_TEXT];     // what they wear (Character_LooksToText), sent with MSG_PLAYER
    int            invite_from;                     // the last player who invited them
    uint64_t       hostile_at[NET_MAX_PLAYERS + 1]; // by player id: when they declared hostility to them (0: not)
    uint8_t        portal[32];                      // their Town Portal (MSG_PORTAL as they sent it), for newcomers
    int            portal_size;                     // 0: none open
} Peer;

// An item lying on the ground somewhere in the game
typedef struct ServerItem {
    uint32_t id;
    int      map;
    float    x;
    float    y;
    Item     item;
} ServerItem;

struct NetServer {
    NetSettings settings;
    ServerItem  items[NET_GROUND_ITEMS_MAX];
    int         item_count;
    int         used_maps[NET_USED_OBJECTS_MAX]; // opened chests, doors, shrines: map and object index
    int         used_objects[NET_USED_OBJECTS_MAX];
    int         used_count;
    uint32_t    next_item_id;
    bool        dedicated;
    char        realm[NET_ID_MAX];
    NetSocket   listener;
    NetSocket   discovery;
    Peer        peers[MAX_CONNECTIONS];
    int         next_player_id;
    uint64_t    last_states;
    uint32_t    instance; // a random number for this run (LAN answers by two addresses are one game)
    // Telling the lobby server about the game (public games)
    NetSocket lobby;
    uint64_t  lobby_started;
    uint64_t  lobby_next;
    char      lobby_line[480];
};

// ---------------------------------------------------------------------------------------------------------------------
// Folders: server/ (server.ini, server.id) and server/characters/<player's id>/<name>.abc

static void Server__MakeFolder(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

const char *NetServer_Folder(void) {
    static char folder[4200];
    snprintf(folder, sizeof(folder), "%s/server", AbyssConfiguration_GetConfigFolder());
    Server__MakeFolder(folder);
    return folder;
}

// Only letters, digits and '-' make it into a folder name (ids are hex anyway)
static void Server__OwnerFolder(const char *client_id, char *path, const size_t size) {
    char safe[NET_ID_MAX];
    int  n = 0;
    for (const char *c = client_id; *c != '\0' && n < NET_ID_MAX - 1; c++) {
        if (isalnum((unsigned char)*c) || *c == '-') {
            safe[n++] = *c;
        }
    }
    safe[n] = '\0';
    char characters[4300];
    snprintf(characters, sizeof(characters), "%s/characters", NetServer_Folder());
    Server__MakeFolder(characters);
    snprintf(path, size, "%s/%s", characters, n > 0 ? safe : "unknown");
    Server__MakeFolder(path);
}

static void Server__CharacterPath(const char *client_id, const char *name, char *path, const size_t size) {
    char folder[4400];
    Server__OwnerFolder(client_id, folder, sizeof(folder));
    snprintf(path, size, "%s/%s.abc", folder, name);
}

static bool Server__ReadFile(const char *path, char *text, const size_t size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    const size_t length = fread(text, 1, size - 1, file);
    fclose(file);
    text[length] = '\0';
    return true;
}

static bool Server__WriteCharacter(const char *client_id, const Character *character) {
    char path[4600];
    Server__CharacterPath(client_id, character->name, path, sizeof(path));
    char *text = malloc(CHARACTER_TEXT_MAX);
    if (text == NULL) {
        return false;
    }
    const size_t length = Character_WriteText(character, text, CHARACTER_TEXT_MAX);
    FILE        *file   = length < CHARACTER_TEXT_MAX ? fopen(path, "w") : NULL;
    bool         ok     = false;
    if (file != NULL) {
        ok = fwrite(text, 1, length, file) == length;
        fclose(file);
    }
    free(text);
    return ok;
}

// Names of the .abc files in a folder (without ".abc"); returns how many
static int Server__ListFolder(const char *folder, char names[][CHARACTER_NAME_MAX], const int max) {
    int count = 0;
#ifdef _WIN32
    char pattern[4800];
    snprintf(pattern, sizeof(pattern), "%s\\*.abc", folder);
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            const size_t length = strlen(found.cFileName);
            if (count < max && length > 4 && length - 4 < CHARACTER_NAME_MAX) {
                snprintf(names[count++], CHARACTER_NAME_MAX, "%.*s", (int)(length - 4), found.cFileName);
            }
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(folder);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const size_t length = strlen(entry->d_name);
            if (count < max && length > 4 && strcmp(entry->d_name + length - 4, ".abc") == 0 &&
                length - 4 < CHARACTER_NAME_MAX) {
                snprintf(names[count++], CHARACTER_NAME_MAX, "%.*s", (int)(length - 4), entry->d_name);
            }
        }
        closedir(dir);
    }
#endif
    return count;
}

// Whether a name is in one player's folder (regardless of case: names are file names on Windows)
static bool Server__NameInFolder(const char *folder, const char *name) {
    char      names[MAX_SERVER_CHARS][CHARACTER_NAME_MAX];
    const int count = Server__ListFolder(folder, names, MAX_SERVER_CHARS);
    for (int i = 0; i < count; i++) {
        if (strcasecmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

// Whether any player on this server has a character with this name (names are unique on a server, like a realm)
static bool Server__NameTaken(const char *name) {
    char characters[4300];
    snprintf(characters, sizeof(characters), "%s/characters", NetServer_Folder());
    bool taken = false;
#ifdef _WIN32
    char pattern[4400];
    snprintf(pattern, sizeof(pattern), "%s\\*", characters);
    WIN32_FIND_DATAA found;
    HANDLE           handle = FindFirstFileA(pattern, &found);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.cFileName[0] != '.' && !taken) {
                char folder[4700];
                snprintf(folder, sizeof(folder), "%s/%s", characters, found.cFileName);
                taken = Server__NameInFolder(folder, name);
            }
        } while (FindNextFileA(handle, &found));
        FindClose(handle);
    }
#else
    DIR *dir = opendir(characters);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && !taken) {
            if (entry->d_name[0] != '.') {
                char folder[4700];
                snprintf(folder, sizeof(folder), "%s/%s", characters, entry->d_name);
                taken = Server__NameInFolder(folder, name);
            }
        }
        closedir(dir);
    }
#endif
    return taken;
}

// ---------------------------------------------------------------------------------------------------------------------
// server.ini

static const char *server_ini_template =
    "# Abyss Engine dedicated server (AbyssEngine --server). Restart the server after changing this.\n"
    "[Server]\n"
    "Name = Abyss server          # the game's name in Browse\n"
    "Password =                   # empty: anyone can join\n"
    "Port = 4000                  # TCP port players connect to (Diablo II's own is 4000)\n"
    "MaxPlayers = 8               # 1 to 8\n"
    "Difficulty = Normal          # Normal, Nightmare or Hell\n"
    "NewCharactersOnly = false    # only level 1 characters with no experience\n"
    "MadeHereOnly = true          # only characters made on this server (the server keeps them)\n"
    "Hardcore = Any               # Any, Softcore or Hardcore\n"
    "LevelMin = 1\n"
    "LevelMax = 99\n"
    "PvP = false                  # players can hurt each other\n"
    "Speed = Normal               # Normal, Fast, Faster or Fastest (Diablo's game speed)\n"
    "InventoryPages = 1           # 1 to 255\n"
    "StashPages = 1               # 1 to 255\n"
    "Seed = 0                     # the world; 0: a new one each start\n"
    "Public = false               # list it on the lobby server ([Network] Lobby in abyss.ini)\n"
    "LAN = true                   # answer LAN searches\n";

static bool Server__Bool(const char *value) {
    return strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcmp(value, "1") == 0;
}

bool NetServer_LoadSettings(NetSettings *settings) {
    NetSettings_Defaults(settings);
    snprintf(settings->name, sizeof(settings->name), "Abyss server");
    settings->made_here_only = true;
    char path[4300];
    snprintf(path, sizeof(path), "%s/server.ini", NetServer_Folder());
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        file = fopen(path, "w");
        if (file != NULL) {
            fputs(server_ini_template, file);
            fclose(file);
            LOG_INFO("Wrote %s (edit it to set the server up)", path);
        }
        return true;
    }
    char line[512];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }
        *equals     = '\0';
        char *key   = line;
        char *value = equals + 1;
        while (isspace((unsigned char)*key)) {
            key++;
        }
        while (isspace((unsigned char)*value)) {
            value++;
        }
        for (char *end = key + strlen(key); end > key && isspace((unsigned char)end[-1]); end--) {
            end[-1] = '\0';
        }
        for (char *end = value + strlen(value); end > value && isspace((unsigned char)end[-1]); end--) {
            end[-1] = '\0';
        }
        const int number = atoi(value);
        if (strcasecmp(key, "Name") == 0 && value[0] != '\0') {
            snprintf(settings->name, sizeof(settings->name), "%.31s", value);
        } else if (strcasecmp(key, "Password") == 0) {
            snprintf(settings->password, sizeof(settings->password), "%.31s", value);
        } else if (strcasecmp(key, "Port") == 0 && number > 0 && number < 65536) {
            settings->port = (uint16_t)number;
        } else if (strcasecmp(key, "MaxPlayers") == 0) {
            settings->max_players = number < 1 ? 1 : (number > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : number);
        } else if (strcasecmp(key, "Difficulty") == 0) {
            for (int d = 0; d < DIFFICULTY_COUNT; d++) {
                settings->difficulty = strcasecmp(value, NetSettings_DifficultyName(d)) == 0 ? d : settings->difficulty;
            }
        } else if (strcasecmp(key, "NewCharactersOnly") == 0) {
            settings->new_characters_only = Server__Bool(value);
        } else if (strcasecmp(key, "MadeHereOnly") == 0) {
            settings->made_here_only = Server__Bool(value);
        } else if (strcasecmp(key, "Hardcore") == 0) {
            settings->hardcore = strcasecmp(value, "Softcore") == 0   ? HARDCORE_SOFTCORE_ONLY
                                 : strcasecmp(value, "Hardcore") == 0 ? HARDCORE_ONLY
                                                                      : HARDCORE_ANY;
        } else if (strcasecmp(key, "LevelMin") == 0) {
            settings->level_min = number < 1 ? 1 : (number > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : number);
        } else if (strcasecmp(key, "LevelMax") == 0) {
            settings->level_max = number < 1 ? 1 : (number > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : number);
        } else if (strcasecmp(key, "PvP") == 0) {
            settings->pvp = Server__Bool(value);
        } else if (strcasecmp(key, "Speed") == 0) {
            for (int s = 0; s < SPEED_COUNT; s++) {
                settings->speed = strcasecmp(value, NetSettings_SpeedName(s)) == 0 ? s : settings->speed;
            }
        } else if (strcasecmp(key, "InventoryPages") == 0) {
            settings->inventory_pages = number < 1 ? 1 : (number > 255 ? 255 : number);
        } else if (strcasecmp(key, "StashPages") == 0) {
            settings->stash_pages = number < 1 ? 1 : (number > 255 ? 255 : number);
        } else if (strcasecmp(key, "Seed") == 0) {
            settings->seed = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcasecmp(key, "Public") == 0) {
            settings->public_game = Server__Bool(value);
        } else if (strcasecmp(key, "LAN") == 0) {
            settings->lan = Server__Bool(value);
        }
    }
    fclose(file);
    if (settings->level_max < settings->level_min) {
        settings->level_max = settings->level_min;
    }
    return true;
}

// The dedicated server's own id (characters made on it are tagged with it), kept in server/server.id
static void Server__DedicatedRealm(char *realm, const size_t size) {
    char path[4300];
    snprintf(path, sizeof(path), "%s/server.id", NetServer_Folder());
    realm[0]   = '\0';
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        if (fgets(realm, (int)size, file) != NULL) {
            realm[strcspn(realm, "\r\n ")] = '\0';
        }
        fclose(file);
    }
    if (strlen(realm) < 16) {
        // A new one, different from this computer's client id (a player here is not the server)
        snprintf(realm, size, "srv-%s", Net_ClientId());
        for (char *c = realm + 4; *c != '\0'; c++) {
            *c = "0123456789abcdef"[(rand() ^ (*c * 31)) & 15];
        }
        file = fopen(path, "w");
        if (file != NULL) {
            fprintf(file, "%s\n", realm);
            fclose(file);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Sending

static void Server__Send(Peer *peer, const uint8_t type, const NetWriter *writer) {
    NetConnection_Send(peer->connection, type, writer);
}

static void Server__Reject(Peer *peer, const char *reason) {
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_String(&writer, reason);
    Server__Send(peer, MSG_REJECT, &writer);
    NetConnection_Flush(peer->connection);
    peer->connection->closed = true; // dropped on the next update
}

static void Server__Broadcast(NetServer *server, const uint8_t type, const NetWriter *writer, const Peer *except) {
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection != NULL && peer->stage == STAGE_IN_GAME && peer != except) {
            Server__Send(peer, type, writer);
        }
    }
}

// To the players still on the character screen: who's in the game (so a character already playing shows as such)
static void Server__TellWaiting(NetServer *server, const uint8_t type, const NetWriter *writer) {
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection != NULL && peer->stage == STAGE_CHARACTERS) {
            Server__Send(peer, type, writer);
        }
    }
}

static void Server__Chat(NetServer *server, const int from, const char *text, const Peer *except) {
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)from);
    NetWriter_String(&writer, text);
    Server__Broadcast(server, MSG_CHAT_FROM, &writer, except);
}

static void Server__WriteItem(NetWriter *writer, const ServerItem *item, const int dropper) {
    NetWriter_U32(writer, item->id);
    NetWriter_U8(writer, (uint8_t)dropper);
    NetWriter_I32(writer, item->map);
    NetWriter_F32(writer, item->x);
    NetWriter_F32(writer, item->y);
    NetItem_Write(writer, &item->item);
}

static void Server__WritePlayer(NetWriter *writer, const Peer *peer) {
    NetWriter_U8(writer, (uint8_t)peer->id);
    NetWriter_String(writer, peer->character.name);
    NetWriter_U8(writer, (uint8_t)peer->character.class_id);
    NetWriter_U8(writer, (uint8_t)peer->character.level);
    NetWriter_U8(writer, peer->character.hardcore ? 1 : 0);
    NetWriter_U8(writer, (uint8_t)peer->color);
    NetWriter_U8(writer, (uint8_t)peer->party);
    NetWriter_String(writer, peer->looks);
}

static int Server__InGame(const NetServer *server) {
    int count = 0;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        count += server->peers[p].connection != NULL && server->peers[p].stage != STAGE_HELLO ? 1 : 0;
    }
    return count;
}

// Dedicated: the player's characters here, whole (the character screen shows them standing in their slots): how many,
// then one message each (a character with many inventory pages is most of a message on its own)
static void Server__SendCharacterList(Peer *peer) {
    char folder[4400];
    Server__OwnerFolder(peer->client_id, folder, sizeof(folder));
    char       names[MAX_SERVER_CHARS][CHARACTER_NAME_MAX];
    const int  count  = Server__ListFolder(folder, names, MAX_SERVER_CHARS);
    char      *text   = malloc(CHARACTER_TEXT_MAX);
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (text == NULL || writer == NULL) {
        free(text);
        free(writer);
        return;
    }
    int readable = 0;
    for (int i = 0; i < count; i++) {
        char path[5200];
        snprintf(path, sizeof(path), "%s/%.15s.abc", folder, names[i]);
        if (Server__ReadFile(path, text, CHARACTER_TEXT_MAX)) {
            if (readable != i) {
                memcpy(names[readable], names[i], CHARACTER_NAME_MAX);
            }
            readable++;
        }
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)readable);
    Server__Send(peer, MSG_CHAR_LIST, writer);
    for (int i = 0; i < readable; i++) {
        char path[5200];
        snprintf(path, sizeof(path), "%s/%.15s.abc", folder, names[i]);
        if (Server__ReadFile(path, text, CHARACTER_TEXT_MAX)) {
            NetWriter_Begin(writer);
            NetWriter_String(writer, text);
            Server__Send(peer, MSG_CHAR_ENTRY, writer);
        }
    }
    free(writer);
    free(text);
}

static void Server__Result(Peer *peer, const char *error, const Character *character) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    char      *text   = malloc(CHARACTER_TEXT_MAX);
    if (writer == NULL || text == NULL) {
        free(writer);
        free(text);
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, error == NULL ? 1 : 0);
    NetWriter_String(writer, error != NULL ? error : "");
    if (error == NULL && character != NULL) {
        Character_WriteText(character, text, CHARACTER_TEXT_MAX);
        NetWriter_String(writer, text);
    }
    Server__Send(peer, MSG_CHAR_RESULT, writer);
    free(writer);
    free(text);
}

// ---------------------------------------------------------------------------------------------------------------------
// Joining and leaving

static void Server__Enter(NetServer *server, Peer *peer) {
    peer->stage     = STAGE_IN_GAME;
    peer->has_state = false;
    CharacterLooks looks;
    Character_Looks(&peer->character, &looks);
    Character_LooksToText(&looks, peer->looks, sizeof(peer->looks));
    // The next id nobody in the game has (ids are 1..255; a game holds at most 255)
    for (int tries = 0; tries < 255; tries++) {
        const int id           = server->next_player_id;
        server->next_player_id = id >= 255 ? 1 : id + 1;
        bool taken             = false;
        for (int p = 0; p < MAX_CONNECTIONS && !taken; p++) {
            taken = server->peers[p].connection != NULL && server->peers[p].stage == STAGE_IN_GAME &&
                    &server->peers[p] != peer && server->peers[p].id == id;
        }
        if (!taken) {
            peer->id = id;
            break;
        }
    }

    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)peer->id);
    NetWriter_U32(&writer, server->settings.seed);
    Server__Send(peer, MSG_ENTER, &writer);
    // Everyone already here, to the newcomer; the newcomer, to everyone
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        const Peer *other = &server->peers[p];
        if (other->connection != NULL && other->stage == STAGE_IN_GAME && other != peer) {
            NetWriter_Begin(&writer);
            Server__WritePlayer(&writer, other);
            Server__Send(peer, MSG_PLAYER, &writer);
        }
    }
    NetWriter_Begin(&writer);
    Server__WritePlayer(&writer, peer);
    Server__Broadcast(server, MSG_PLAYER, &writer, peer);
    Server__TellWaiting(server, MSG_PLAYER, &writer);
    for (int i = 0; i < server->used_count; i++) { // what's been opened already
        NetWriter_Begin(&writer);
        NetWriter_U8(&writer, 0);
        NetWriter_I32(&writer, server->used_maps[i]);
        NetWriter_U16(&writer, (uint16_t)server->used_objects[i]);
        Server__Send(peer, MSG_OBJECT_USED, &writer);
    }
    for (int p = 0; p < MAX_CONNECTIONS; p++) { // the Town Portals open
        const Peer *other = &server->peers[p];
        if (other->connection != NULL && other->stage == STAGE_IN_GAME && other != peer && other->portal_size > 0) {
            NetWriter_Begin(&writer);
            NetWriter_U8(&writer, (uint8_t)other->id);
            NetWriter_Bytes(&writer, other->portal, other->portal_size);
            Server__Send(peer, MSG_PORTAL_FROM, &writer);
        }
    }
    for (int i = 0; i < server->item_count; i++) { // what's lying around already
        NetWriter_Begin(&writer);
        Server__WriteItem(&writer, &server->items[i], 0);
        Server__Send(peer, MSG_ITEM_ADD, &writer);
    }
    char text[96];
    snprintf(text, sizeof(text), "%s (%s) joined the game.", peer->character.name,
             Character_ClassName(peer->character.class_id));
    Server__Chat(server, 0, text, NULL);
    LOG_INFO("Server: %s joined from %s", peer->character.name, peer->connection->address);
}

static Peer *Server__PeerById(NetServer *server, const int id) {
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection != NULL && peer->stage == STAGE_IN_GAME && peer->id == id) {
            return peer;
        }
    }
    return NULL;
}

// Everyone hears about a player again (their party changed)
static void Server__TellPlayer(NetServer *server, const Peer *peer) {
    NetWriter writer;
    NetWriter_Begin(&writer);
    Server__WritePlayer(&writer, peer);
    Server__Broadcast(server, MSG_PLAYER, &writer, NULL);
}

static void Server__LeaveParty(NetServer *server, Peer *peer) {
    const int party = peer->party;
    if (party == 0) {
        return;
    }
    peer->party = 0;
    if (peer->stage == STAGE_IN_GAME) {
        Server__TellPlayer(server, peer);
    }
    // One left on their own: no party any more
    Peer *last  = NULL;
    int   count = 0;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *other = &server->peers[p];
        if (other->connection != NULL && other->stage == STAGE_IN_GAME && other->party == party) {
            last = other;
            count++;
        }
    }
    if (count == 1) {
        last->party = 0;
        Server__TellPlayer(server, last);
    }
}

static void Server__Drop(NetServer *server, Peer *peer) {
    if (peer->connection == NULL) {
        return;
    }
    if (peer->stage == STAGE_IN_GAME) {
        Server__LeaveParty(server, peer);
        for (int p = 0; p < MAX_CONNECTIONS && peer->id > 0 && peer->id <= NET_MAX_PLAYERS; p++) {
            server->peers[p].hostile_at[peer->id] = 0; // (whoever gets this id next starts at peace)
        }
        NetWriter writer;
        NetWriter_Begin(&writer);
        NetWriter_U8(&writer, (uint8_t)peer->id);
        Server__Broadcast(server, MSG_PLAYER_GONE, &writer, peer);
        Server__TellWaiting(server, MSG_PLAYER_GONE, &writer);
        char text[96];
        snprintf(text, sizeof(text), "%s left the game.", peer->character.name);
        Server__Chat(server, 0, text, peer);
        LOG_INFO("Server: %s left", peer->character.name);
    }
    NetConnection_Destroy(&peer->connection);
    memset(peer, 0, sizeof(Peer));
}

static bool Server__NameInGame(const NetServer *server, const char *name) {
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        const Peer *peer = &server->peers[p];
        if (peer->connection != NULL && peer->stage == STAGE_IN_GAME && strcasecmp(peer->character.name, name) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Messages

// server/bans.txt: player ids (one a line, '#' notes) that can't join
static bool Server__Banned(const char *client_id) {
    char path[4300];
    snprintf(path, sizeof(path), "%s/bans.txt", NetServer_Folder());
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    char line[160];
    bool banned = false;
    while (!banned && fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, " \t\r\n#")] = '\0';
        banned                          = line[0] != '\0' && strcmp(line, client_id) == 0;
    }
    fclose(file);
    return banned;
}

static void Server__Hello(NetServer *server, Peer *peer, NetReader *reader) {
    const int version = NetReader_U16(reader);
    char      password[NET_NAME_MAX];
    NetReader_String(reader, peer->client_id, sizeof(peer->client_id));
    NetReader_String(reader, password, sizeof(password));
    peer->color = NetReader_U8(reader) % NET_COLOR_COUNT;
    char mods[512];
    NetReader_String(reader, mods, sizeof(mods));
    if (version != NET_PROTOCOL_VERSION) {
        Server__Reject(peer, "The host runs a different version of Abyss Engine.");
        return;
    }
    char ours[512];
    Mods_ActiveSignature(ours, sizeof(ours));
    if (strcmp(mods, ours) != 0) {
        // Everyone's game has to read the same tables and maps (or the monsters and items wouldn't match)
        char why[600];
        snprintf(why, sizeof(why), ours[0] != '\0' ? "The game is played with these mods: %.250s" : "%s",
                 ours[0] != '\0' ? ours : "The game is played without mods: turn yours off (Options > Mods).");
        Server__Reject(peer, why);
        return;
    }
    if (peer->client_id[0] == '\0') {
        Server__Reject(peer, "No player id.");
        return;
    }
    if (Server__Banned(peer->client_id)) {
        Server__Reject(peer, "You're banned from this server.");
        return;
    }
    if (server->settings.password[0] != '\0' && strcmp(password, server->settings.password) != 0) {
        Server__Reject(peer, "Wrong password.");
        return;
    }
    if (Server__InGame(server) >= server->settings.max_players) {
        Server__Reject(peer, "The game is full.");
        return;
    }
    peer->stage = STAGE_CHARACTERS;
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetSettings_Write(&server->settings, &writer, false);
    NetWriter_U8(&writer, server->dedicated ? 1 : 0); // whether characters are kept here
    NetWriter_String(&writer, server->realm);
    Server__Send(peer, MSG_WELCOME, &writer);
    if (server->dedicated) {
        Server__SendCharacterList(peer);
    }
    // Who's playing now (and, on, who joins and leaves while this player picks)
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        const Peer *other = &server->peers[p];
        if (other->connection != NULL && other->stage == STAGE_IN_GAME) {
            NetWriter_Begin(&writer);
            Server__WritePlayer(&writer, other);
            Server__Send(peer, MSG_PLAYER, &writer);
        }
    }
}

static void Server__Pick(NetServer *server, Peer *peer, NetReader *reader) {
    char *payload = NetReader_StringAlloc(reader);
    if (payload == NULL) {
        return;
    }
    Character character;
    bool      found = false;
    if (server->dedicated) {
        char path[4600];
        Server__CharacterPath(peer->client_id, payload, path, sizeof(path));
        char *text = malloc(CHARACTER_TEXT_MAX);
        found =
            text != NULL && Server__ReadFile(path, text, CHARACTER_TEXT_MAX) && Character_ReadText(text, &character);
        free(text);
    } else {
        found = Character_ReadText(payload, &character); // hosted: the player's own, sent whole
    }
    free(payload);
    const char *why = NULL;
    if (!found) {
        Server__Result(peer, "That character could not be read.", NULL);
    } else if (!NetSettings_Allows(&server->settings, &character, server->realm, &why)) {
        Server__Result(peer, why, NULL);
    } else if (Server__NameInGame(server, character.name)) {
        Server__Result(peer, "A character with that name is already in the game.", NULL);
    } else {
        peer->character = character;
        Server__Result(peer, NULL, &character);
        Server__Enter(server, peer);
    }
}

static void Server__Create(NetServer *server, Peer *peer, NetReader *reader) {
    char name[CHARACTER_NAME_MAX];
    NetReader_String(reader, name, sizeof(name));
    const int   class_id  = NetReader_U8(reader);
    const bool  expansion = NetReader_U8(reader) != 0;
    const bool  hardcore  = NetReader_U8(reader) != 0;
    const char *why       = NULL;
    char        folder[4400];
    Server__OwnerFolder(peer->client_id, folder, sizeof(folder));
    char names[MAX_SERVER_CHARS][CHARACTER_NAME_MAX];
    if (!server->dedicated) {
        why = "This game doesn't keep characters.";
    } else if (class_id >= CHARACTER_CLASS_COUNT) {
        why = "No such class.";
    } else if (!Character_IsValidName(name, &why)) {
        // why says what's wrong
    } else if (Server__NameTaken(name)) {
        why = "That name is taken on this server.";
    } else if (Server__ListFolder(folder, names, MAX_SERVER_CHARS) >= MAX_SERVER_CHARS) {
        why = "You have as many characters here as you can.";
    }
    if (why != NULL) {
        Server__Result(peer, why, NULL);
        return;
    }
    Character character;
    Character_Create(&character, name, (CharacterClass)class_id, expansion, hardcore);
    snprintf(character.realm, sizeof(character.realm), "%s", server->realm);
    if (!Server__WriteCharacter(peer->client_id, &character)) {
        Server__Result(peer, "The server could not save the character.", NULL);
        return;
    }
    LOG_INFO("Server: %s made %s (%s)", peer->connection->address, name, Character_ClassName(character.class_id));
    Server__SendCharacterList(peer);
    Server__Result(peer, NULL, &character);
}

static void Server__Delete(NetServer *server, Peer *peer, NetReader *reader) {
    char name[CHARACTER_NAME_MAX];
    NetReader_String(reader, name, sizeof(name));
    if (!server->dedicated || name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL ||
        strstr(name, "..") != NULL) {
        return;
    }
    char path[4600];
    Server__CharacterPath(peer->client_id, name, path, sizeof(path));
    remove(path);
    Server__SendCharacterList(peer);
}

// Dedicated: the game saved its character. The server keeps it, after a sanity check (the same character: name and
// class; Hardcore stays Hardcore; levels don't go down).
static void Server__Save(NetServer *server, Peer *peer, NetReader *reader) {
    char *text = NetReader_StringAlloc(reader);
    if (text == NULL) {
        return;
    }
    Character character;
    if (server->dedicated && peer->stage == STAGE_IN_GAME && Character_ReadText(text, &character) &&
        strcmp(character.name, peer->character.name) == 0 && character.class_id == peer->character.class_id &&
        character.hardcore == peer->character.hardcore && character.level >= peer->character.level) {
        snprintf(character.realm, sizeof(character.realm), "%s", peer->character.realm);
        const bool level_changed = character.level != peer->character.level;
        peer->character          = character;
        Server__WriteCharacter(peer->client_id, &character);
        if (level_changed) {
            NetWriter writer;
            NetWriter_Begin(&writer);
            Server__WritePlayer(&writer, peer);
            Server__Broadcast(server, MSG_PLAYER, &writer, NULL);
        }
    }
    free(text);
}

static void Server__State(Peer *peer, NetReader *reader) {
    if (peer->stage != STAGE_IN_GAME) {
        return;
    }
    const int   place = NetReader_I32(reader);
    const float x     = NetReader_F32(reader);
    const float y     = NetReader_F32(reader);
    const int   turn  = NetReader_U8(reader);
    char        mode[4];
    NetReader_String(reader, mode, sizeof(mode));
    const int life  = NetReader_U8(reader);
    const int level = NetReader_U8(reader);
    if (reader->failed || x != x || y != y || x < 0.0f || y < 0.0f || x > 100000.0f || y > 100000.0f) {
        return; // nonsense (NaN, off any map)
    }
    peer->place     = place;
    peer->x         = x;
    peer->y         = y;
    peer->turn      = turn;
    peer->has_state = true;
    peer->life      = life > 100 ? 100 : life;
    peer->level     = level;
    snprintf(peer->mode, sizeof(peer->mode), "%s", mode);
}

static bool Server__SameName(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

static void Server__ChatFrom(NetServer *server, Peer *peer, NetReader *reader) {
    char text[NET_CHAT_MAX];
    NetReader_String(reader, text, sizeof(text));
    if (peer->stage != STAGE_IN_GAME || text[0] == '\0') {
        return;
    }
    for (char *c = text; *c != '\0'; c++) {
        *c = (unsigned char)*c < 32 ? ' ' : *c; // no control characters
    }
    // A whisper: "/w Name what" (or /m) goes to that player alone
    if ((strncmp(text, "/w ", 3) == 0 || strncmp(text, "/m ", 3) == 0) && text[3] != '\0') {
        char     *name = text + 3;
        char     *what = strchr(name, ' ');
        Peer     *to   = NULL;
        NetWriter writer;
        char      line[NET_CHAT_MAX + CHARACTER_NAME_MAX + 24];
        if (what != NULL) {
            *what++ = '\0';
        }
        for (int p = 0; p < MAX_CONNECTIONS && to == NULL; p++) {
            Peer *other = &server->peers[p];
            if (other->connection != NULL && other->stage == STAGE_IN_GAME &&
                Server__SameName(other->character.name, name)) {
                to = other;
            }
        }
        NetWriter_Begin(&writer);
        if (to == NULL || what == NULL || what[0] == '\0') {
            snprintf(line, sizeof(line), to == NULL ? "%s is not in this game." : "Whisper what to %s?", name);
            NetWriter_U8(&writer, 0);
            NetWriter_String(&writer, line);
            Server__Send(peer, MSG_CHAT_FROM, &writer);
            return;
        }
        snprintf(line, sizeof(line), "%s whispers: %s", peer->character.name, what);
        NetWriter_U8(&writer, 0);
        NetWriter_String(&writer, line);
        Server__Send(to, MSG_CHAT_FROM, &writer);
        snprintf(line, sizeof(line), "You whisper to %s: %s", to->character.name, what);
        NetWriter_Begin(&writer);
        NetWriter_U8(&writer, 0);
        NetWriter_String(&writer, line);
        Server__Send(peer, MSG_CHAT_FROM, &writer);
        return;
    }
    Server__Chat(server, peer->id, text, NULL);
}

// Items on the ground
static void Server__ItemDrop(NetServer *server, Peer *peer, NetReader *reader) {
    if (peer->stage != STAGE_IN_GAME || server->item_count >= NET_GROUND_ITEMS_MAX) {
        return;
    }
    ServerItem item;
    memset(&item, 0, sizeof(item));
    item.map = NetReader_I32(reader);
    item.x   = NetReader_F32(reader);
    item.y   = NetReader_F32(reader);
    NetItem_Read(reader, &item.item);
    if (reader->failed || item.item.code[0] == '\0' || item.x != item.x || item.y != item.y || item.x < 0.0f ||
        item.y < 0.0f || item.x > 100000.0f || item.y > 100000.0f) {
        return;
    }
    item.id                             = ++server->next_item_id;
    server->items[server->item_count++] = item;
    NetWriter writer;
    NetWriter_Begin(&writer);
    Server__WriteItem(&writer, &item, peer->id);
    Server__Broadcast(server, MSG_ITEM_ADD, &writer, NULL);
}

static void Server__ItemPick(NetServer *server, Peer *peer, NetReader *reader) {
    const uint32_t id = NetReader_U32(reader);
    if (peer->stage != STAGE_IN_GAME) {
        return;
    }
    for (int i = 0; i < server->item_count; i++) {
        if (server->items[i].id == id) {
            server->items[i] = server->items[--server->item_count]; // theirs: the first to ask
            NetWriter writer;
            NetWriter_Begin(&writer);
            NetWriter_U32(&writer, id);
            NetWriter_U8(&writer, (uint8_t)peer->id);
            Server__Broadcast(server, MSG_ITEM_GONE, &writer, NULL);
            return;
        }
    }
}

// Opening a chest, door or shrine: the first to ask opens it, and everyone's game shows it open
static void Server__ObjectUse(NetServer *server, Peer *peer, NetReader *reader) {
    const int map   = NetReader_I32(reader);
    const int index = NetReader_U16(reader);
    if (peer->stage != STAGE_IN_GAME || reader->failed || server->used_count >= NET_USED_OBJECTS_MAX) {
        return;
    }
    for (int i = 0; i < server->used_count; i++) {
        if (server->used_maps[i] == map && server->used_objects[i] == index) {
            return; // someone was first
        }
    }
    server->used_maps[server->used_count]      = map;
    server->used_objects[server->used_count++] = index;
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)peer->id);
    NetWriter_I32(&writer, map);
    NetWriter_U16(&writer, (uint16_t)index);
    Server__Broadcast(server, MSG_OBJECT_USED, &writer, NULL);
}

// A kill's experience: the killer, and their party on the same map nearby, share it
static void Server__Killed(NetServer *server, Peer *peer, NetReader *reader) {
    const int map    = NetReader_I32(reader);
    const int index  = NetReader_U16(reader);
    const int killer = NetReader_U8(reader);
    Peer     *by     = Server__PeerById(server, killer);
    if (peer->stage != STAGE_IN_GAME || reader->failed || by == NULL) {
        return;
    }
    Peer *sharing[MAX_CONNECTIONS];
    int   count = 0;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *other = &server->peers[p];
        if (other == by || (by->party != 0 && other->connection != NULL && other->stage == STAGE_IN_GAME &&
                            other->party == by->party && other->has_state && other->place == map &&
                            (other->x - by->x) * (other->x - by->x) + (other->y - by->y) * (other->y - by->y) <=
                                NET_PARTY_RANGE * NET_PARTY_RANGE)) {
            sharing[count++] = other;
        }
    }
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)peer->id);
    NetWriter_I32(&writer, map);
    NetWriter_U16(&writer, (uint16_t)index);
    NetWriter_U8(&writer, (uint8_t)killer);
    NetWriter_U8(&writer, (uint8_t)count);
    for (int s = 0; s < count; s++) {
        Server__Send(sharing[s], MSG_MONSTER_KILLED_FROM, &writer);
    }
}

static void Server__PartyInvite(NetServer *server, Peer *peer, NetReader *reader) {
    Peer *target = Server__PeerById(server, NetReader_U8(reader));
    if (peer->stage != STAGE_IN_GAME || target == NULL || target == peer ||
        (target->party != 0 && target->party == peer->party)) {
        return;
    }
    target->invite_from = peer->id;
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)peer->id);
    Server__Send(target, MSG_PARTY_INVITED, &writer);
}

static void Server__PartyAccept(NetServer *server, Peer *peer, NetReader *reader) {
    const int inviter_id = NetReader_U8(reader);
    Peer     *inviter    = Server__PeerById(server, inviter_id);
    if (peer->stage != STAGE_IN_GAME || inviter == NULL || peer->invite_from != inviter_id) {
        return;
    }
    peer->invite_from = 0;
    Server__LeaveParty(server, peer);
    if (inviter->party == 0) {
        inviter->party = inviter->id;
        Server__TellPlayer(server, inviter);
    }
    peer->party = inviter->party;
    Server__TellPlayer(server, peer);
    char text[96];
    snprintf(text, sizeof(text), "%s joined %s's party.", peer->character.name, inviter->character.name);
    Server__Chat(server, 0, text, NULL);
}

// What a player sent, passed on as it is with their id in front: to everyone on a map, or to one player
// Hostility (PvP games): either one declared it, long enough ago (the game's countdown)
static bool Server__Hostile(NetServer *server, const Peer *a, const int b_id, const uint64_t now) {
    const Peer *b = Server__PeerById(server, b_id);
    if (b == NULL || !server->settings.pvp) {
        return false;
    }
    const uint64_t ab   = a->hostile_at[b_id];
    const uint64_t ba   = b->hostile_at[a->id];
    const uint64_t wait = NET_HOSTILE_MS - 1000; // (a second's leeway for the clocks)
    return (ab != 0 && now >= ab + wait) || (ba != 0 && now >= ba + wait);
}

static void Server__HostileFrom(NetServer *server, Peer *peer, NetReader *reader) {
    const int  target = NetReader_U8(reader);
    const bool on     = NetReader_U8(reader) != 0;
    Peer      *other  = Server__PeerById(server, target);
    if (reader->failed || peer->stage != STAGE_IN_GAME || other == NULL || other == peer || !server->settings.pvp) {
        return;
    }
    const int my_level    = peer->level > peer->character.level ? peer->level : peer->character.level;
    const int their_level = other->level > other->character.level ? other->level : other->character.level;
    if (on && (my_level < NET_HOSTILE_LVL || their_level < NET_HOSTILE_LVL ||
               (peer->party != 0 && peer->party == other->party))) {
        return; // (too low, or in the same party)
    }
    peer->hostile_at[target] = on ? SDL_GetTicks64() : 0;
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_U8(&writer, (uint8_t)peer->id);
    NetWriter_U8(&writer, (uint8_t)target);
    NetWriter_U8(&writer, on ? 1 : 0);
    Server__Broadcast(server, MSG_HOSTILE_FROM, &writer, NULL);
}

static void Server__PassOn(NetServer *server, const Peer *from, const uint8_t type, const NetReader *reader,
                           const int map, const int to_id) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)from->id);
    NetWriter_Bytes(writer, reader->data, reader->size);
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection == NULL || peer->stage != STAGE_IN_GAME || peer == from) {
            continue;
        }
        // Monster positions are sent 15 times a second and each one replaces the last: a player whose game isn't
        // reading (loading an area, which can take seconds) misses some rather than have them pile up until the
        // connection gives up on them
        if (type == MSG_MONSTERS_FROM && peer->connection->out_size > NET_BEHIND_BYTES) {
            continue;
        }
        if (to_id > 0 ? peer->id == to_id : (peer->has_state && peer->place == map)) {
            Server__Send(peer, type, writer);
        }
    }
    free(writer);
}

static void Server__Handle(NetServer *server, Peer *peer, const uint8_t type, NetReader *reader) {
    if (peer->stage == STAGE_HELLO && type != MSG_HELLO) {
        Server__Reject(peer, "Say hello first.");
        return;
    }
    switch (type) {
    case MSG_HELLO:
        if (peer->stage == STAGE_HELLO) {
            Server__Hello(server, peer, reader);
        }
        break;
    case MSG_CHAR_PICK:
        if (peer->stage == STAGE_CHARACTERS) {
            Server__Pick(server, peer, reader);
        }
        break;
    case MSG_CHAR_CREATE:
        if (peer->stage == STAGE_CHARACTERS) {
            Server__Create(server, peer, reader);
        }
        break;
    case MSG_CHAR_DELETE:
        if (peer->stage == STAGE_CHARACTERS) {
            Server__Delete(server, peer, reader);
        }
        break;
    case MSG_CHAR_SAVE:
        Server__Save(server, peer, reader);
        break;
    case MSG_STATE:
        Server__State(peer, reader);
        break;
    case MSG_CHAT:
        Server__ChatFrom(server, peer, reader);
        break;
    case MSG_LEAVE:
        peer->connection->closed = true;
        break;
    case MSG_ITEM_DROP:
        Server__ItemDrop(server, peer, reader);
        break;
    case MSG_ITEM_PICK:
        Server__ItemPick(server, peer, reader);
        break;
    case MSG_CAST:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            Server__PassOn(server, peer, MSG_CAST_FROM, reader, NetReader_I32(&peek), 0);
        }
        break;
    case MSG_PORTAL:
        if (peer->stage == STAGE_IN_GAME && reader->size <= (int)sizeof(peer->portal)) {
            // Kept for whoever joins later (closed: forgotten), and passed on to everyone
            const bool open   = reader->size > 0 && reader->data[0] != 0;
            peer->portal_size = open ? reader->size : 0;
            if (reader->size > 0) {
                memcpy(peer->portal, reader->data, (size_t)reader->size);
            }
            NetWriter *writer = malloc(sizeof(NetWriter));
            if (writer != NULL) {
                NetWriter_Begin(writer);
                NetWriter_U8(writer, (uint8_t)peer->id);
                NetWriter_Bytes(writer, reader->data, reader->size);
                Server__Broadcast(server, MSG_PORTAL_FROM, writer, peer);
                free(writer);
            }
        }
        break;
    case MSG_ALLIES:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            Server__PassOn(server, peer, MSG_ALLIES_FROM, reader, NetReader_I32(&peek), 0);
        }
        break;
    case MSG_MONSTERS:
    case MSG_HIT_MONSTER:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            Server__PassOn(server, peer, type == MSG_MONSTERS ? MSG_MONSTERS_FROM : MSG_HIT_MONSTER_FROM, reader,
                           NetReader_I32(&peek), 0);
        }
        break;
    case MSG_MONSTER_KILLED:
        Server__Killed(server, peer, reader);
        break;
    case MSG_OBJECT_USE:
        Server__ObjectUse(server, peer, reader);
        break;
    case MSG_PARTY_INVITE:
        Server__PartyInvite(server, peer, reader);
        break;
    case MSG_PARTY_ACCEPT:
        Server__PartyAccept(server, peer, reader);
        break;
    case MSG_PARTY_LEAVE:
        if (peer->stage == STAGE_IN_GAME) {
            Server__LeaveParty(server, peer);
        }
        break;
    case MSG_HOSTILE:
        Server__HostileFrom(server, peer, reader);
        break;
    case MSG_CORPSE:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            Server__PassOn(server, peer, MSG_CORPSE_FROM, reader, NetReader_I32(&peek), 0);
        }
        break;
    case MSG_LOOT:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            const int to   = NetReader_U8(&peek);
            Server__PassOn(server, peer, MSG_LOOT_FROM, reader, 0, to > 0 ? to : 255);
        }
        break;
    case MSG_TRADE:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek = *reader;
            const int to   = NetReader_U8(&peek);
            Server__PassOn(server, peer, MSG_TRADE_FROM, reader, 0, to > 0 ? to : 255);
        }
        break;
    case MSG_LOOKS:
        if (peer->stage == STAGE_IN_GAME) {
            char           text[CHARACTER_LOOKS_TEXT];
            CharacterLooks looks;
            NetReader_String(reader, text, sizeof(text));
            if (!reader->failed && Character_LooksFromText(text, &looks) && strcmp(text, peer->looks) != 0) {
                snprintf(peer->looks, sizeof(peer->looks), "%s", text);
                Server__TellPlayer(server, peer);
            }
        }
        break;
    case MSG_AFFLICT:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader peek   = *reader;
            const int target = NetReader_U8(&peek);
            if (target > 0) {
                Server__PassOn(server, peer, MSG_AFFLICTED, reader, 0, target);
            }
        }
        break;
    case MSG_HIT_PLAYER:
        if (peer->stage == STAGE_IN_GAME) {
            NetReader  peek   = *reader;
            const int  target = NetReader_U8(&peek);
            const bool pvp    = NetReader_U8(&peek) != 0;
            if (target > 0 && (!pvp || Server__Hostile(server, peer, target, SDL_GetTicks64()))) {
                // (no fighting each other unless the game allows it and one of them went hostile)
                Server__PassOn(server, peer, MSG_HIT_YOU, reader, 0, target);
            }
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// LAN searches and the lobby

// The server's console (a dedicated server's typed commands): who's in, kick, ban, save everyone, say something
static Peer *Server__PeerByName(NetServer *server, const char *name) {
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection != NULL && peer->stage == STAGE_IN_GAME && Server__SameName(peer->character.name, name)) {
            return peer;
        }
    }
    return NULL;
}

void NetServer_Command(NetServer *server, const char *line, char *reply, const int reply_size) {
    char word[16] = "";
    char rest[NET_CHAT_MAX];
    rest[0] = '\0';
    sscanf(line, " %15s %199[^\r\n]", word, rest);
    reply[0] = '\0';
    if (strcmp(word, "players") == 0 || strcmp(word, "who") == 0) {
        int used = snprintf(reply, (size_t)reply_size,
                            "%d connected (playing or picking a character):", Server__InGame(server));
        for (int p = 0; p < MAX_CONNECTIONS && used < reply_size; p++) {
            const Peer *peer = &server->peers[p];
            if (peer->connection != NULL && peer->stage == STAGE_IN_GAME) {
                used += snprintf(reply + used, (size_t)(reply_size - used), "\n  %s, level %d %s (player id %s)",
                                 peer->character.name, peer->character.level,
                                 Character_ClassName(peer->character.class_id), peer->client_id);
            }
        }
    } else if (strcmp(word, "kick") == 0 || strcmp(word, "ban") == 0) {
        Peer *peer = Server__PeerByName(server, rest);
        if (peer == NULL) {
            snprintf(reply, (size_t)reply_size, "Nobody called \"%s\" is in the game.", rest);
            return;
        }
        const bool ban = strcmp(word, "ban") == 0;
        if (ban) {
            char path[4300];
            snprintf(path, sizeof(path), "%s/bans.txt", NetServer_Folder());
            FILE *file = fopen(path, "a");
            if (file != NULL) {
                fprintf(file, "%s # %s\n", peer->client_id, peer->character.name);
                fclose(file);
            }
        }
        if (server->dedicated) {
            Server__WriteCharacter(peer->client_id, &peer->character); // (what they had, kept)
        }
        char said[96];
        snprintf(said, sizeof(said), "%s was %s by the server.", peer->character.name, ban ? "banned" : "kicked");
        snprintf(reply, (size_t)reply_size, "%s", said);
        Server__Reject(peer, ban ? "You were banned from this server." : "You were kicked by the server.");
        Server__Chat(server, 0, said, peer);
    } else if (strcmp(word, "save") == 0) {
        int saved = 0;
        for (int p = 0; p < MAX_CONNECTIONS && server->dedicated; p++) {
            const Peer *peer = &server->peers[p];
            if (peer->connection != NULL && peer->stage == STAGE_IN_GAME &&
                Server__WriteCharacter(peer->client_id, &peer->character)) {
                saved++;
            }
        }
        snprintf(reply, (size_t)reply_size,
                 server->dedicated ? "Saved %d characters." : "Only a dedicated server keeps characters.", saved);
    } else if (strcmp(word, "say") == 0 && rest[0] != '\0') {
        char said[NET_CHAT_MAX + 16];
        snprintf(said, sizeof(said), "[Server] %s", rest);
        Server__Chat(server, 0, said, NULL);
        snprintf(reply, (size_t)reply_size, "%s", said);
    } else if (word[0] != '\0') {
        snprintf(reply, (size_t)reply_size,
                 "Commands: players, kick <name>, ban <name> (server/bans.txt), save, say <text>, quit");
    }
}

static void Server__Info(const NetServer *server, NetGameInfo *info) {
    memset(info, 0, sizeof(NetGameInfo));
    snprintf(info->name, sizeof(info->name), "%s", server->settings.name);
    info->port                = server->settings.port;
    info->players             = Server__InGame(server);
    info->max_players         = server->settings.max_players;
    info->difficulty          = server->settings.difficulty;
    info->locked              = server->settings.password[0] != '\0';
    info->dedicated           = server->dedicated;
    info->speed               = server->settings.speed;
    info->hardcore            = server->settings.hardcore;
    info->level_min           = server->settings.level_min;
    info->level_max           = server->settings.level_max;
    info->new_characters_only = server->settings.new_characters_only;
    info->made_here_only      = server->settings.made_here_only;
    info->pvp                 = server->settings.pvp;
    info->instance            = server->instance;
    // Who's playing, as many as fit
    size_t used = 0;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        const Peer *peer = &server->peers[p];
        if (peer->connection == NULL || peer->stage != STAGE_IN_GAME) {
            continue;
        }
        const int level = peer->level > peer->character.level ? peer->level : peer->character.level;
        char      entry[48];
        const int n = snprintf(entry, sizeof(entry), "%s%s/%d/%d", used > 0 ? "," : "", peer->character.name, level,
                               (int)peer->character.class_id);
        if (n <= 0 || used + (size_t)n >= sizeof(info->roster)) {
            break;
        }
        memcpy(info->roster + used, entry, (size_t)n + 1);
        used += (size_t)n;
    }
}

static void Server__AnswerSearches(NetServer *server) {
    if (server->discovery == NET_INVALID_SOCKET) {
        return;
    }
    char     packet[512];
    char     from[NET_ADDRESS_MAX];
    uint16_t from_port = 0;
    int      got;
    while ((got = Net_UdpReceive(server->discovery, packet, sizeof(packet) - 1, from, sizeof(from), &from_port)) > 0) {
        packet[got] = '\0';
        if (strncmp(packet, "ABYSS?", 6) != 0 || from_port == 0) {
            continue;
        }
        NetGameInfo info;
        Server__Info(server, &info);
        char      reply[512];
        const int length = NetGameInfo_Format(&info, reply, sizeof(reply));
        Net_UdpSendTo(server->discovery, from, from_port, reply, length);
    }
}

static void Server__TellLobby(NetServer *server, const uint64_t now) {
    if (!server->settings.public_game) {
        return;
    }
    if (server->lobby != NET_INVALID_SOCKET) {
        const int done = Net_ConnectDone(server->lobby);
        if (done > 0) {
            Net_Send(server->lobby, server->lobby_line, (int)strlen(server->lobby_line));
        }
        if (done != 0 || now - server->lobby_started > LOBBY_TIMEOUT_MS) {
            if (done < 0) {
                LOG_WARN("Server: could not reach the lobby server");
            }
            Net_Close(server->lobby);
            server->lobby = NET_INVALID_SOCKET;
        }
        return;
    }
    if (now < server->lobby_next) {
        return;
    }
    server->lobby_next = now + LOBBY_EVERY_MS;
    char     host[256];
    uint16_t port = NET_LOBBY_PORT;
    if (!Net_ParseAddress(AbyssConfiguration_GetLobby(), host, sizeof(host), &port, NET_LOBBY_PORT)) {
        return; // no lobby server set
    }
    NetGameInfo info;
    Server__Info(server, &info);
    char line[440];
    NetGameInfo_Format(&info, line, sizeof(line));
    snprintf(server->lobby_line, sizeof(server->lobby_line), "REGISTER %s\n", line);
    server->lobby         = Net_Connect(host, port);
    server->lobby_started = now;
}

// ---------------------------------------------------------------------------------------------------------------------

NetServer *NetServer_Start(const NetSettings *settings, const bool dedicated, char *error, const int error_size) {
    if (!Net_Init()) {
        snprintf(error, (size_t)error_size, "The network could not be started.");
        return NULL;
    }
    NetServer *server = calloc(1, sizeof(NetServer));
    if (server == NULL) {
        snprintf(error, (size_t)error_size, "Out of memory.");
        return NULL;
    }
    server->settings  = *settings;
    server->dedicated = dedicated;
    if (server->settings.seed == 0) {
        srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)server);
        server->settings.seed = ((uint32_t)rand() << 16) ^ (uint32_t)rand() ^ 1u;
    }
    if (dedicated) {
        Server__DedicatedRealm(server->realm, sizeof(server->realm));
    } else {
        snprintf(server->realm, sizeof(server->realm), "%s", Net_ClientId());
    }
    server->listener = Net_Listen(settings->port);
    if (server->listener == NET_INVALID_SOCKET) {
        snprintf(error, (size_t)error_size, "Port %u is in use (or can't be opened).", (unsigned)settings->port);
        free(server);
        return NULL;
    }
    server->discovery      = settings->lan ? Net_UdpOpen(NET_DISCOVERY_PORT, false) : NET_INVALID_SOCKET;
    server->lobby          = NET_INVALID_SOCKET;
    server->next_player_id = 1;
    server->instance       = ((uint32_t)rand() << 16) ^ (uint32_t)rand() ^ (uint32_t)time(NULL);
    LOG_INFO("Server: \"%s\" on port %u%s%s", server->settings.name, (unsigned)settings->port,
             dedicated ? " (dedicated)" : "",
             server->discovery != NET_INVALID_SOCKET ? ", answering LAN searches" : "");
    return server;
}

void NetServer_Update(NetServer *server, const uint64_t now) {
    // New connections
    for (;;) {
        char            address[NET_ADDRESS_MAX];
        const NetSocket socket = Net_Accept(server->listener, address, sizeof(address));
        if (socket == NET_INVALID_SOCKET) {
            break;
        }
        Peer *free_peer = NULL;
        for (int p = 0; p < MAX_CONNECTIONS && free_peer == NULL; p++) {
            free_peer = server->peers[p].connection == NULL ? &server->peers[p] : NULL;
        }
        if (free_peer == NULL) {
            Net_Close(socket);
            continue;
        }
        memset(free_peer, 0, sizeof(Peer));
        free_peer->connection = NetConnection_Create(socket, address);
        free_peer->stage      = STAGE_HELLO;
        free_peer->since      = now;
        if (free_peer->connection == NULL) {
            Net_Close(socket);
        }
    }
    // Messages
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection == NULL) {
            continue;
        }
        uint8_t   type;
        NetReader reader;
        int       handled = 0;
        while (!peer->connection->closed && handled < 64 && NetConnection_Receive(peer->connection, &type, &reader)) {
            Server__Handle(server, peer, type, &reader);
            handled++;
        }
        if (peer->stage == STAGE_HELLO && now - peer->since > HELLO_TIMEOUT_MS) {
            peer->connection->closed = true;
        }
    }
    // Where everyone is
    if (now - server->last_states >= 1000 / NET_STATE_HZ) {
        server->last_states = now;
        NetWriter writer;
        NetWriter_Begin(&writer);
        int count = 0;
        for (int p = 0; p < MAX_CONNECTIONS; p++) {
            count += server->peers[p].connection != NULL && server->peers[p].stage == STAGE_IN_GAME &&
                             server->peers[p].has_state
                         ? 1
                         : 0;
        }
        NetWriter_U8(&writer, (uint8_t)count);
        for (int p = 0; p < MAX_CONNECTIONS; p++) {
            const Peer *peer = &server->peers[p];
            if (peer->connection != NULL && peer->stage == STAGE_IN_GAME && peer->has_state) {
                NetWriter_U8(&writer, (uint8_t)peer->id);
                NetWriter_I32(&writer, peer->place);
                NetWriter_F32(&writer, peer->x);
                NetWriter_F32(&writer, peer->y);
                NetWriter_U8(&writer, (uint8_t)peer->turn);
                NetWriter_String(&writer, peer->mode);
                NetWriter_U8(&writer, (uint8_t)peer->life);
                NetWriter_U8(&writer, (uint8_t)peer->level);
            }
        }
        if (count > 0) {
            Server__Broadcast(server, MSG_STATES, &writer, NULL);
        }
    }
    // Send, and let go of the ones that left
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection == NULL) {
            continue;
        }
        NetConnection_Flush(peer->connection);
        if (peer->connection->closed) {
            Server__Drop(server, peer);
        }
    }
    Server__AnswerSearches(server);
    Server__TellLobby(server, now);
}

void NetServer_Stop(NetServer **server_ref) {
    if (server_ref == NULL || *server_ref == NULL) {
        return;
    }
    NetServer *server = *server_ref;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        Peer *peer = &server->peers[p];
        if (peer->connection != NULL) {
            NetWriter writer;
            NetWriter_Begin(&writer);
            NetWriter_String(&writer, "The host closed the game.");
            NetConnection_Send(peer->connection, MSG_REJECT, &writer);
            NetConnection_Flush(peer->connection);
            NetConnection_Destroy(&peer->connection);
        }
    }
    if (server->settings.public_game) {
        // Tell the lobby it's gone (best effort: one try, no waiting)
        char     host[256];
        uint16_t port = NET_LOBBY_PORT;
        if (Net_ParseAddress(AbyssConfiguration_GetLobby(), host, sizeof(host), &port, NET_LOBBY_PORT)) {
            NetSocket bye = Net_Connect(host, port);
            if (bye != NET_INVALID_SOCKET) {
                char line[64];
                snprintf(line, sizeof(line), "UNREGISTER %u\n", (unsigned)server->settings.port);
                for (int tries = 0; tries < 50 && Net_ConnectDone(bye) == 0; tries++) {
                    SDL_Delay(10);
                }
                if (Net_ConnectDone(bye) > 0) {
                    Net_Send(bye, line, (int)strlen(line));
                }
                Net_Close(bye);
            }
        }
    }
    Net_Close(server->lobby);
    Net_Close(server->discovery);
    Net_Close(server->listener);
    free(server);
    *server_ref = NULL;
}

const NetSettings *NetServer_Settings(const NetServer *server) { return &server->settings; }

int NetServer_PlayerCount(const NetServer *server) {
    int count = 0;
    for (int p = 0; p < MAX_CONNECTIONS; p++) {
        count += server->peers[p].connection != NULL && server->peers[p].stage == STAGE_IN_GAME ? 1 : 0;
    }
    return count;
}

const char *NetServer_Realm(const NetServer *server) { return server->realm; }
