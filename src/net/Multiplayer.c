#include "Multiplayer.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Logging.h"
#include <SDL2/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAN_WAIT_MS   1500 // answers to a LAN search come in for this long
#define LAN_ASK_EVERY 500  // asked a few times (UDP can get lost)
#define LOBBY_WAIT_MS 6000
#define BROWSE_MAX    64

static NetServer *hosted = NULL;

bool Multiplayer_Host(const NetSettings *settings, char *error, const int error_size) {
    Multiplayer_Leave();
    hosted = NetServer_Start(settings, false, error, error_size);
    if (hosted == NULL) {
        return false;
    }
    // We join our own game like everyone else (the same checks, the same messages)
    if (!NetClient_Connect("127.0.0.1", settings->port, settings->password)) {
        snprintf(error, (size_t)error_size, "%s", NetClient_Error());
        NetServer_Stop(&hosted);
        return false;
    }
    return true;
}

bool             Multiplayer_IsHosting(void) { return hosted != NULL; }
const NetServer *Multiplayer_Server(void) { return hosted; }

void Multiplayer_Leave(void) {
    NetClient_Disconnect();
    if (hosted != NULL) {
        NetServer_Update(hosted, SDL_GetTicks64()); // let the server hear we left before it closes
        NetServer_Stop(&hosted);
    }
}

void Multiplayer_Update(void) {
    const uint64_t now = SDL_GetTicks64();
    if (hosted != NULL) {
        NetServer_Update(hosted, now);
    }
    NetClient_Update(now);
    if (hosted != NULL) {
        NetServer_Update(hosted, now); // answer what we just sent ourselves straight away
    }
    Browse_Update();
}

bool Multiplayer_Active(void) { return NetClient_Stage() != NETC_OFF || hosted != NULL; }

// ---------------------------------------------------------------------------------------------------------------------
// host.ini: the Host tab's settings as last used

void Multiplayer_LoadHostSettings(NetSettings *settings) {
    NetSettings_Defaults(settings);
    char path[4200];
    snprintf(path, sizeof(path), "%s/host.ini", AbyssConfiguration_GetConfigFolder());
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *equals                = strchr(line, '=');
        if (line[0] == '#' || equals == NULL) {
            continue;
        }
        *equals           = '\0';
        const char *key   = line;
        const char *value = equals + 1;
        const int   n     = atoi(value);
        if (strcmp(key, "Name") == 0 && value[0] != '\0') {
            snprintf(settings->name, sizeof(settings->name), "%.31s", value);
        } else if (strcmp(key, "Password") == 0) {
            snprintf(settings->password, sizeof(settings->password), "%.31s", value);
        } else if (strcmp(key, "MaxPlayers") == 0) {
            settings->max_players = n < 1 ? 1 : (n > NET_MAX_PLAYERS ? NET_MAX_PLAYERS : n);
        } else if (strcmp(key, "Difficulty") == 0) {
            settings->difficulty = n < 0 ? 0 : (n >= DIFFICULTY_COUNT ? DIFFICULTY_COUNT - 1 : n);
        } else if (strcmp(key, "NewCharactersOnly") == 0) {
            settings->new_characters_only = n != 0;
        } else if (strcmp(key, "MadeHereOnly") == 0) {
            settings->made_here_only = n != 0;
        } else if (strcmp(key, "Hardcore") == 0) {
            settings->hardcore = n < 0 ? 0 : (n >= HARDCORE_RULE_COUNT ? HARDCORE_RULE_COUNT - 1 : n);
        } else if (strcmp(key, "LevelMin") == 0) {
            settings->level_min = n < 1 ? 1 : (n > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : n);
        } else if (strcmp(key, "LevelMax") == 0) {
            settings->level_max = n < 1 ? 1 : (n > CHARACTER_MAX_LEVEL ? CHARACTER_MAX_LEVEL : n);
        } else if (strcmp(key, "PvP") == 0) {
            settings->pvp = n != 0;
        } else if (strcmp(key, "Speed") == 0) {
            settings->speed = n < 0 ? 0 : (n >= SPEED_COUNT ? SPEED_COUNT - 1 : n);
        } else if (strcmp(key, "InventoryPages") == 0) {
            settings->inventory_pages = n < 1 ? 1 : (n > 255 ? 255 : n);
        } else if (strcmp(key, "StashPages") == 0) {
            settings->stash_pages = n < 1 ? 1 : (n > 255 ? 255 : n);
        } else if (strcmp(key, "Seed") == 0) {
            settings->seed = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "Public") == 0) {
            settings->public_game = n != 0;
        } else if (strcmp(key, "LAN") == 0) {
            settings->lan = n != 0;
        } else if (strcmp(key, "Port") == 0 && n > 0 && n < 65536) {
            settings->port = (uint16_t)n;
        }
    }
    fclose(file);
    if (settings->level_max < settings->level_min) {
        settings->level_max = settings->level_min;
    }
}

void Multiplayer_SaveHostSettings(const NetSettings *s) {
    char path[4200];
    snprintf(path, sizeof(path), "%s/host.ini", AbyssConfiguration_GetConfigFolder());
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    // The game's password isn't kept: it would sit in the file as plain text. It's typed again for each game.
    fprintf(file, "# Multiplayer > Host: the settings last used (the Host tab writes this; not the password)\n");
    fprintf(file, "Name=%s\nMaxPlayers=%d\nDifficulty=%d\nNewCharactersOnly=%d\nMadeHereOnly=%d\n", s->name,
            s->max_players, s->difficulty, s->new_characters_only, s->made_here_only);
    fprintf(file, "Hardcore=%d\nLevelMin=%d\nLevelMax=%d\nPvP=%d\nSpeed=%d\nInventoryPages=%d\nSeed=%u\n", s->hardcore,
            s->level_min, s->level_max, s->pvp, s->speed, s->inventory_pages, (unsigned)s->seed);
    fprintf(file, "StashPages=%d\n", s->stash_pages);
    fprintf(file, "Public=%d\nLAN=%d\nPort=%u\n", s->public_game, s->lan, (unsigned)s->port);
    fclose(file);
}

// ---------------------------------------------------------------------------------------------------------------------
// Browse

static struct {
    BrowseSource source;
    bool         searching;
    uint64_t     started;
    uint64_t     last_ask;
    NetSocket    udp;
    NetSocket    lobby;
    bool         lobby_asked;
    char         lobby_text[16384];
    int          lobby_length;
    NetGameInfo  games[BROWSE_MAX];
    int          count;
    char         status[160];
} browse = {.udp = NET_INVALID_SOCKET, .lobby = NET_INVALID_SOCKET};

static void Browse__Add(const NetGameInfo *info) {
    for (int g = 0; g < browse.count; g++) {
        const bool same = info->instance != 0 ? browse.games[g].instance == info->instance
                                              : strcmp(browse.games[g].address, info->address) == 0 &&
                                                    browse.games[g].port == info->port;
        if (same) {
            // The same game answering again (on this computer it answers by its network address and by 127.0.0.1:
            // keep the network address, which others can use too)
            const bool keep_address = strcmp(info->address, "127.0.0.1") == 0;
            char       address[NET_ADDRESS_MAX];
            snprintf(address, sizeof(address), "%s", browse.games[g].address);
            browse.games[g] = *info;
            if (keep_address) {
                snprintf(browse.games[g].address, sizeof(browse.games[g].address), "%s", address);
            }
            return;
        }
    }
    if (browse.count < BROWSE_MAX) {
        browse.games[browse.count++] = *info;
    }
}

void Browse_Stop(void) {
    Net_Close(browse.udp);
    Net_Close(browse.lobby);
    browse.udp       = NET_INVALID_SOCKET;
    browse.lobby     = NET_INVALID_SOCKET;
    browse.searching = false;
}

static void Browse__Finish(void) {
    Browse_Stop();
    if (browse.count == 0 && browse.status[0] == '\0') {
        snprintf(browse.status, sizeof(browse.status),
                 browse.source == BROWSE_LAN ? "No games found on the local network." : "No games online right now.");
    } else if (browse.count > 0) {
        snprintf(browse.status, sizeof(browse.status), "%d game%s found.", browse.count, browse.count == 1 ? "" : "s");
    }
}

void Browse_Start(const BrowseSource source) {
    Browse_Stop();
    browse.source    = source;
    browse.count     = 0;
    browse.status[0] = '\0';
    browse.started   = SDL_GetTicks64();
    browse.last_ask  = 0;
    if (!Net_Init()) {
        snprintf(browse.status, sizeof(browse.status), "The network could not be started.");
        return;
    }
    if (source == BROWSE_LAN) {
        browse.udp = Net_UdpOpen(0, true);
        if (browse.udp == NET_INVALID_SOCKET) {
            snprintf(browse.status, sizeof(browse.status), "Could not search the local network.");
            return;
        }
    } else {
        char     host[256];
        uint16_t port = NET_LOBBY_PORT;
        if (!Net_ParseAddress(AbyssConfiguration_GetLobby(), host, sizeof(host), &port, NET_LOBBY_PORT)) {
            snprintf(browse.status, sizeof(browse.status),
                     "No lobby server set: put its address in abyss.ini, [Network] Lobby = host:port.");
            return;
        }
        browse.lobby        = Net_Connect(host, port);
        browse.lobby_asked  = false;
        browse.lobby_length = 0;
        if (browse.lobby == NET_INVALID_SOCKET) {
            snprintf(browse.status, sizeof(browse.status), "The lobby server's address could not be found.");
            return;
        }
    }
    browse.searching = true;
    snprintf(browse.status, sizeof(browse.status), "Searching...");
}

static void Browse__Lan(const uint64_t now) {
    if (now - browse.last_ask >= LAN_ASK_EVERY) {
        browse.last_ask = now;
        Net_UdpSendTo(browse.udp, "255.255.255.255", NET_DISCOVERY_PORT, "ABYSS?", 6);
        Net_UdpSendTo(browse.udp, "127.0.0.1", NET_DISCOVERY_PORT, "ABYSS?", 6); // games on this computer
    }
    char     packet[512];
    char     from[NET_ADDRESS_MAX];
    uint16_t from_port = 0;
    int      got;
    while ((got = Net_UdpReceive(browse.udp, packet, sizeof(packet) - 1, from, sizeof(from), &from_port)) > 0) {
        packet[got] = '\0';
        NetGameInfo info;
        memset(&info, 0, sizeof(info));
        if (NetGameInfo_Parse(packet, &info)) {
            snprintf(info.address, sizeof(info.address), "%s", from);
            Browse__Add(&info);
        }
    }
    if (now - browse.started >= LAN_WAIT_MS) {
        Browse__Finish();
    }
}

// The lobby answers "LIST" with "GAME <address> <one-line description>" lines and "END"
static void Browse__Online(const uint64_t now) {
    if (!browse.lobby_asked) {
        const int done = Net_ConnectDone(browse.lobby);
        if (done < 0) {
            snprintf(browse.status, sizeof(browse.status), "The lobby server didn't answer.");
            Browse_Stop();
            return;
        }
        if (done > 0) {
            Net_Send(browse.lobby, "LIST\n", 5);
            browse.lobby_asked = true;
        }
    } else {
        int got;
        while ((got = Net_Receive(browse.lobby, browse.lobby_text + browse.lobby_length,
                                  (int)sizeof(browse.lobby_text) - 1 - browse.lobby_length)) > 0) {
            browse.lobby_length += got;
        }
        browse.lobby_text[browse.lobby_length] = '\0';
        if (strstr(browse.lobby_text, "END\n") != NULL || got < 0) {
            char *line = browse.lobby_text;
            while (line != NULL && *line != '\0') {
                char *next = strchr(line, '\n');
                if (next != NULL) {
                    *next++ = '\0';
                }
                char address[NET_ADDRESS_MAX];
                int  used = 0;
                if (sscanf(line, "GAME %63s %n", address, &used) == 1) {
                    NetGameInfo info;
                    memset(&info, 0, sizeof(info));
                    if (NetGameInfo_Parse(line + used, &info)) {
                        snprintf(info.address, sizeof(info.address), "%s", address);
                        Browse__Add(&info);
                    }
                }
                line = next;
            }
            Browse__Finish();
            return;
        }
    }
    if (now - browse.started >= LOBBY_WAIT_MS) {
        snprintf(browse.status, sizeof(browse.status), "The lobby server didn't answer in time.");
        Browse_Stop();
    }
}

void Browse_Update(void) {
    if (!browse.searching) {
        return;
    }
    const uint64_t now = SDL_GetTicks64();
    if (browse.source == BROWSE_LAN) {
        Browse__Lan(now);
    } else {
        Browse__Online(now);
    }
}

bool Browse_Searching(void) { return browse.searching; }

int Browse_Games(const NetGameInfo **games) {
    *games = browse.games;
    return browse.count;
}

const char *Browse_Status(void) { return browse.status; }
