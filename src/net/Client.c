#include "Client.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include "../common/Mods.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONNECT_TIMEOUT_MS 8000
#define CHAT_QUEUE         32
#define GAME_QUEUE         512 // game messages waiting for the scene
#define MAX_REMOTE         NET_MAX_PLAYERS

static struct {
    NetClientStage stage;
    NetConnection *connection;
    NetSocket      connecting;
    uint64_t       started;
    char           address[NET_ADDRESS_MAX + 8];
    char           password[NET_NAME_MAX];
    char           error[160];

    NetSettings settings;
    bool        server_keeps_characters;
    char        realm[NET_ID_MAX];
    Character  *characters; // dedicated: ours on the server
    int         character_count;
    int         characters_coming; // MSG_CHAR_ENTRY messages still to come
    int         list_version;
    bool        has_result;
    bool        result_ok;
    char        result[160];

    int       my_id;
    int       my_party;
    int       invite_from;
    uint32_t  seed;
    NetPlayer players[MAX_REMOTE];
    int       player_count;
    int       players_version; // goes up when someone joins or leaves
    char      chat[CHAT_QUEUE][NET_CHAT_MAX + CHARACTER_NAME_MAX + 4];
    int       chat_head;
    int       chat_count;
    // The game's own messages (items, monsters, hits), for the game scene to read in order
    uint8_t *game_data[GAME_QUEUE];
    int      game_size[GAME_QUEUE];
    uint8_t  game_type[GAME_QUEUE];
    int      game_head;
    int      game_count;
    uint8_t *game_taken;                       // the last one handed out (freed on the next)
} client = {.connecting = NET_INVALID_SOCKET}; // (not 0: that would close standard input)

static void NetClient__ClearGame(void) {
    while (client.game_count > 0) {
        free(client.game_data[client.game_head]);
        client.game_head = (client.game_head + 1) % GAME_QUEUE;
        client.game_count--;
    }
    free(client.game_taken);
    client.game_taken = NULL;
    client.game_head  = 0;
}

static void NetClient__QueueGame(const uint8_t type, const NetReader *reader) {
    if (client.game_count == GAME_QUEUE) { // nobody's reading: forget the oldest
        free(client.game_data[client.game_head]);
        client.game_head = (client.game_head + 1) % GAME_QUEUE;
        client.game_count--;
    }
    uint8_t *copy = malloc((size_t)(reader->size > 0 ? reader->size : 1));
    if (copy == NULL) {
        return;
    }
    memcpy(copy, reader->data, (size_t)reader->size);
    const int slot          = (client.game_head + client.game_count) % GAME_QUEUE;
    client.game_data[slot]  = copy;
    client.game_size[slot]  = reader->size;
    client.game_type[slot]  = type;
    client.game_count      += 1;
}

bool NetClient_TakeGameMessage(uint8_t *type, NetReader *reader) {
    free(client.game_taken);
    client.game_taken = NULL;
    if (client.game_count == 0) {
        return false;
    }
    const int slot   = client.game_head;
    client.game_head = (client.game_head + 1) % GAME_QUEUE;
    client.game_count--;
    client.game_taken = client.game_data[slot];
    *type             = client.game_type[slot];
    NetReader_Begin(reader, client.game_taken, client.game_size[slot]);
    return true;
}

static void NetClient__Fail(const char *reason) {
    snprintf(client.error, sizeof(client.error), "%s", reason);
    LOG_INFO("Multiplayer: %s", reason);
    NetConnection_Destroy(&client.connection);
    Net_Close(client.connecting);
    client.connecting = NET_INVALID_SOCKET;
    client.stage      = NETC_FAILED;
    Character_SetSaveHook(NULL);
}

static void NetClient__Send(const uint8_t type, const NetWriter *writer) {
    if (client.connection != NULL) {
        NetConnection_Send(client.connection, type, writer);
    }
}

bool NetClient_Connect(const char *host, const uint16_t port, const char *password) {
    NetClient_Disconnect();
    memset(&client, 0, sizeof(client));
    client.connecting = NET_INVALID_SOCKET;
    snprintf(client.address, sizeof(client.address), strchr(host, ':') != NULL ? "[%s]:%u" : "%s:%u", host,
             (unsigned)port);
    snprintf(client.password, sizeof(client.password), "%s", password != NULL ? password : "");
    if (!Net_Init()) {
        NetClient__Fail("The network could not be started.");
        return false;
    }
    client.connecting = Net_Connect(host, port);
    if (client.connecting == NET_INVALID_SOCKET) {
        NetClient__Fail("That address could not be found.");
        return false;
    }
    client.stage   = NETC_CONNECTING;
    client.started = SDL_GetTicks64();
    return true;
}

void NetClient_Disconnect(void) {
    if (client.connection != NULL) {
        NetWriter writer;
        NetWriter_Begin(&writer);
        NetConnection_Send(client.connection, MSG_LEAVE, &writer);
        NetConnection_Flush(client.connection);
    }
    NetConnection_Destroy(&client.connection);
    Net_Close(client.connecting);
    client.connecting = NET_INVALID_SOCKET;
    free(client.characters);
    client.characters      = NULL;
    client.character_count = 0;
    client.player_count    = 0;
    client.chat_count      = 0;
    client.stage           = NETC_OFF;
    NetClient__ClearGame();
    Character_SetSaveHook(NULL);
}

// Dedicated: the game saving its character goes to the server
static bool NetClient__SaveToServer(Character *character) {
    char *text = malloc(CHARACTER_TEXT_MAX);
    if (text == NULL) {
        return false;
    }
    Character_WriteText(character, text, CHARACTER_TEXT_MAX);
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer != NULL) {
        NetWriter_Begin(writer);
        NetWriter_String(writer, text);
        NetClient__Send(MSG_CHAR_SAVE, writer);
        free(writer);
    }
    free(text);
    return writer != NULL && client.connection != NULL && !client.connection->closed;
}

static void NetClient__AddChat(const char *text) {
    const int slot = (client.chat_head + client.chat_count) % CHAT_QUEUE;
    snprintf(client.chat[slot], sizeof(client.chat[slot]), "%s", text);
    if (client.chat_count == CHAT_QUEUE) {
        client.chat_head = (client.chat_head + 1) % CHAT_QUEUE;
    } else {
        client.chat_count++;
    }
}

NetPlayer *NetClient_Player(const int id) {
    for (int p = 0; p < client.player_count; p++) {
        if (client.players[p].id == id) {
            return &client.players[p];
        }
    }
    return NULL;
}

static void NetClient__Handle(const uint8_t type, NetReader *reader) {
    switch (type) {
    case MSG_WELCOME:
        NetSettings_Read(&client.settings, reader);
        client.server_keeps_characters = NetReader_U8(reader) != 0;
        NetReader_String(reader, client.realm, sizeof(client.realm));
        client.stage = NETC_CHARACTERS;
        Character_SetSaveHook(client.server_keeps_characters ? NetClient__SaveToServer : NULL);
        break;
    case MSG_REJECT: {
        char reason[160];
        NetReader_String(reader, reason, sizeof(reason));
        NetClient__Fail(reason[0] != '\0' ? reason : "The host turned you away.");
        break;
    }
    case MSG_CHAR_LIST: {
        const int count = NetReader_U8(reader);
        free(client.characters);
        client.characters        = calloc((size_t)(count > 0 ? count : 1), sizeof(Character));
        client.character_count   = 0;
        client.characters_coming = client.characters != NULL ? count : 0;
        if (client.characters_coming == 0) {
            client.list_version++; // none: the list is complete
        }
        break;
    }
    case MSG_CHAR_ENTRY: {
        if (client.characters_coming <= 0) {
            break;
        }
        char *text = NetReader_StringAlloc(reader);
        if (text != NULL && Character_ReadText(text, &client.characters[client.character_count])) {
            client.character_count++;
        }
        free(text);
        if (--client.characters_coming == 0) {
            client.list_version++; // all here
        }
        break;
    }
    case MSG_CHAR_RESULT: {
        client.result_ok = NetReader_U8(reader) != 0;
        NetReader_String(reader, client.result, sizeof(client.result));
        client.has_result = true;
        if (client.result_ok) {
            char     *text = NetReader_StringAlloc(reader);
            Character character;
            if (text != NULL && text[0] != '\0' && Character_ReadText(text, &character) &&
                client.stage == NETC_ENTERING) {
                Character_SetCurrent(&character); // as the server has it
            }
            free(text);
        } else if (client.stage == NETC_ENTERING) {
            client.stage = NETC_CHARACTERS;
        }
        break;
    }
    case MSG_ENTER:
        client.my_id        = NetReader_U8(reader);
        client.seed         = NetReader_U32(reader);
        client.player_count = 0;
        client.stage        = NETC_IN_GAME;
        break;
    case MSG_PLAYER: {
        const int  id     = NetReader_U8(reader);
        NetPlayer *player = NetClient_Player(id);
        if (id == client.my_id) { // us: only our party matters
            char name[CHARACTER_NAME_MAX];
            NetReader_String(reader, name, sizeof(name));
            NetReader_U8(reader); // class
            NetReader_U8(reader); // level
            NetReader_U8(reader); // hardcore
            NetReader_U8(reader); // colour
            client.my_party = NetReader_U8(reader);
            NetReader_String(reader, name, sizeof(name)); // looks: ours
            client.players_version++;
            break;
        }
        if (player == NULL && client.player_count < MAX_REMOTE && id != client.my_id) {
            player = &client.players[client.player_count++];
            memset(player, 0, sizeof(NetPlayer));
            player->id = id;
        }
        if (player != NULL) {
            NetReader_String(reader, player->name, sizeof(player->name));
            const int class_id = NetReader_U8(reader);
            player->class_id   = class_id < CHARACTER_CLASS_COUNT ? (CharacterClass)class_id : CHARACTER_AMAZON;
            player->level      = NetReader_U8(reader);
            player->hardcore   = NetReader_U8(reader) != 0;
            player->color      = NetReader_U8(reader) % NET_COLOR_COUNT;
            player->party      = NetReader_U8(reader);
            char looks[CHARACTER_LOOKS_TEXT];
            NetReader_String(reader, looks, sizeof(looks));
            player->has_looks = Character_LooksFromText(looks, &player->looks);
        }
        client.players_version++;
        break;
    }
    case MSG_PLAYER_GONE: {
        const int id = NetReader_U8(reader);
        for (int p = 0; p < client.player_count; p++) {
            if (client.players[p].id == id) {
                client.players[p] = client.players[--client.player_count];
                break;
            }
        }
        client.players_version++;
        break;
    }
    case MSG_STATES: {
        const uint64_t now   = SDL_GetTicks64();
        const int      count = NetReader_U8(reader);
        for (int i = 0; i < count && !reader->failed; i++) {
            const int   id    = NetReader_U8(reader);
            const int   place = NetReader_I32(reader);
            const float x     = NetReader_F32(reader);
            const float y     = NetReader_F32(reader);
            const int   turn  = NetReader_U8(reader);
            char        mode[4];
            NetReader_String(reader, mode, sizeof(mode));
            const int  life   = NetReader_U8(reader);
            const int  level  = NetReader_U8(reader);
            NetPlayer *player = NetClient_Player(id);
            if (player == NULL || id == client.my_id) {
                continue;
            }
            player->life      = life;
            player->level     = level > 0 ? level : player->level;
            const bool jumped = !player->has_state || place != player->place ||
                                (x - player->x) * (x - player->x) + (y - player->y) * (y - player->y) > 400.0f;
            player->from_x    = jumped ? x : player->x;
            player->from_y    = jumped ? y : player->y;
            player->x         = x;
            player->y         = y;
            player->moved_at  = now;
            player->place     = place;
            player->turn      = turn;
            player->has_state = true;
            snprintf(player->mode, sizeof(player->mode), "%s", mode);
        }
        break;
    }
    case MSG_CHAT_FROM: {
        const int id = NetReader_U8(reader);
        char      text[NET_CHAT_MAX];
        NetReader_String(reader, text, sizeof(text));
        char line[NET_CHAT_MAX + CHARACTER_NAME_MAX + 4];
        if (id == 0) {
            snprintf(line, sizeof(line), "%s", text);
        } else {
            const NetPlayer *player = NetClient_Player(id);
            snprintf(line, sizeof(line), "%s: %s",
                     player != NULL
                         ? player->name
                         : (id == client.my_id && Character_Current() != NULL ? Character_Current()->name : "?"),
                     text);
        }
        NetClient__AddChat(line);
        break;
    }
    case MSG_ITEM_ADD:
    case MSG_ITEM_GONE:
    case MSG_MONSTERS_FROM:
    case MSG_HIT_MONSTER_FROM:
    case MSG_MONSTER_KILLED_FROM:
    case MSG_HIT_YOU:
    case MSG_OBJECT_USED:
    case MSG_CAST_FROM:
    case MSG_TRADE_FROM:
    case MSG_ALLIES_FROM:
    case MSG_PORTAL_FROM:
    case MSG_AFFLICTED:
        NetClient__QueueGame(type, reader);
        break;
    case MSG_PARTY_INVITED: {
        client.invite_from      = NetReader_U8(reader);
        const NetPlayer *player = NetClient_Player(client.invite_from);
        char             line[96];
        snprintf(line, sizeof(line), "%s invites you to their party (P to answer).",
                 player != NULL ? player->name : "Someone");
        NetClient__AddChat(line);
        break;
    }
    default:
        break;
    }
}

void NetClient_Update(const uint64_t now) {
    if (client.stage == NETC_CONNECTING) {
        const int done = Net_ConnectDone(client.connecting);
        if (done < 0) {
            NetClient__Fail("Could not connect: nobody is hosting there, or a firewall is in the way.");
            return;
        }
        if (done == 0) {
            if (now - client.started > CONNECT_TIMEOUT_MS) {
                NetClient__Fail("Could not connect: no answer.");
            }
            return;
        }
        client.connection = NetConnection_Create(client.connecting, client.address);
        client.connecting = NET_INVALID_SOCKET;
        client.stage      = NETC_HELLO;
        NetWriter writer;
        NetWriter_Begin(&writer);
        NetWriter_U16(&writer, NET_PROTOCOL_VERSION);
        NetWriter_String(&writer, Net_ClientId());
        NetWriter_String(&writer, client.password);
        NetWriter_U8(&writer, (uint8_t)AbyssConfiguration_GetPlayerColor()); // how the others see us
        char mods[512];
        Mods_ActiveSignature(mods, sizeof(mods)); // the mods we play with: they have to be the game's
        NetWriter_String(&writer, mods);
        NetClient__Send(MSG_HELLO, &writer);
    }
    if (client.connection == NULL) {
        return;
    }
    uint8_t   type;
    NetReader reader;
    int       handled = 0;
    while (client.connection != NULL && handled < 256 && NetConnection_Receive(client.connection, &type, &reader)) {
        NetClient__Handle(type, &reader);
        handled++;
    }
    if (client.connection == NULL) {
        return; // rejected
    }
    NetConnection_Flush(client.connection);
    if (client.connection->closed) {
        NetClient__Fail(client.stage == NETC_IN_GAME ? "Lost the connection to the game."
                                                     : "The host closed the connection.");
    }
}

NetClientStage     NetClient_Stage(void) { return client.stage; }
const char        *NetClient_Error(void) { return client.error; }
const char        *NetClient_Address(void) { return client.address; }
const NetSettings *NetClient_Settings(void) { return &client.settings; }
bool               NetClient_ServerKeepsCharacters(void) { return client.server_keeps_characters; }
const char        *NetClient_Realm(void) { return client.realm; }
int                NetClient_CharacterListVersion(void) { return client.list_version; }
int                NetClient_MyId(void) { return client.my_id; }
uint32_t           NetClient_Seed(void) { return client.seed; }

int NetClient_Characters(const Character **list) {
    *list = client.characters;
    return client.character_count;
}

const char *NetClient_TakeResult(bool *ok) {
    if (!client.has_result) {
        return NULL;
    }
    client.has_result = false;
    *ok               = client.result_ok;
    return client.result;
}

void NetClient_Pick(const Character *character) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    char      *text   = malloc(CHARACTER_TEXT_MAX);
    if (writer != NULL && text != NULL) {
        NetWriter_Begin(writer);
        if (client.server_keeps_characters) {
            NetWriter_String(writer, character->name);
        } else {
            Character_WriteText(character, text, CHARACTER_TEXT_MAX);
            NetWriter_String(writer, text);
        }
        NetClient__Send(MSG_CHAR_PICK, writer);
        client.stage = NETC_ENTERING;
        Character_SetCurrent(character);
    }
    free(writer);
    free(text);
}

void NetClient_Create(const char *name, const CharacterClass class_id, const bool expansion, const bool hardcore) {
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_String(&writer, name);
    NetWriter_U8(&writer, (uint8_t)class_id);
    NetWriter_U8(&writer, expansion ? 1 : 0);
    NetWriter_U8(&writer, hardcore ? 1 : 0);
    NetClient__Send(MSG_CHAR_CREATE, &writer);
}

void NetClient_Delete(const char *name) {
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_String(&writer, name);
    NetClient__Send(MSG_CHAR_DELETE, &writer);
}

void NetClient_SendState(const int place, const float x, const float y, const int turn, const char *mode,
                         const int life_percent, const int level) {
    if (client.stage != NETC_IN_GAME) {
        return;
    }
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_I32(&writer, place);
    NetWriter_F32(&writer, x);
    NetWriter_F32(&writer, y);
    NetWriter_U8(&writer, (uint8_t)turn);
    NetWriter_String(&writer, mode != NULL ? mode : "nu");
    NetWriter_U8(&writer, (uint8_t)(life_percent < 0 ? 0 : (life_percent > 100 ? 100 : life_percent)));
    NetWriter_U8(&writer, (uint8_t)(level < 1 ? 1 : (level > 255 ? 255 : level)));
    NetClient__Send(MSG_STATE, &writer);
}

void NetClient_SendChat(const char *text) {
    if (client.stage != NETC_IN_GAME || text == NULL || text[0] == '\0') {
        return;
    }
    NetWriter writer;
    NetWriter_Begin(&writer);
    NetWriter_String(&writer, text);
    NetClient__Send(MSG_CHAT, &writer);
}

int NetClient_Players(NetPlayer **players) {
    *players = client.players;
    return client.player_count;
}

void NetClient_Send(const uint8_t type, const NetWriter *writer) {
    if (client.stage == NETC_IN_GAME) {
        NetClient__Send(type, writer);
    }
}

int  NetClient_PlayersVersion(void) { return client.players_version; }
int  NetClient_MyParty(void) { return client.my_party; }
int  NetClient_InviteFrom(void) { return client.invite_from; }
void NetClient_ClearInvite(void) { client.invite_from = 0; }

bool NetClient_NameInGame(const char *name) {
    for (int p = 0; p < client.player_count; p++) {
        if (SDL_strcasecmp(client.players[p].name, name) == 0) {
            return true;
        }
    }
    return false;
}

bool NetClient_TakeChat(char *text, const int size) {
    if (client.chat_count == 0) {
        return false;
    }
    snprintf(text, (size_t)size, "%s", client.chat[client.chat_head]);
    client.chat_head = (client.chat_head + 1) % CHAT_QUEUE;
    client.chat_count--;
    return true;
}
