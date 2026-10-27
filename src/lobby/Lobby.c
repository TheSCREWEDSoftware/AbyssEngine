// abyss-lobby: the list of online Abyss Engine games (Multiplayer › Browse › Online).
//
// Hosts of public games tell it about their game every 30 seconds; players ask it for the list. It keeps nothing
// on disk, and forgets a game it hasn't heard from in 90 seconds. Everything is one line of text over TCP:
//
//   REGISTER <description>   a host: "ABYSS <version> <port> <players> ..." (the address is the one it calls from)
//   UNREGISTER <port>        a host closing its game
//   LIST                     a player: answered with "GAME <address> <description>" lines and "END"
//
// Usage: abyss-lobby [port]   (4002 by default). Run it anywhere players and hosts can reach, and put its address
// in abyss.ini: [Network] Lobby = host:port.

#include "../net/Socket.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define Lobby__Sleep(ms) Sleep(ms)
#else
#include <time.h>
static void Lobby__Sleep(const int ms) {
    const struct timespec wait = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&wait, NULL);
}
#endif

#define LOBBY_DEFAULT_PORT 4002
#define MAX_GAMES          512
#define MAX_CALLERS        64
#define GAME_EXPIRES_S     90
#define CALLER_TIMEOUT_S   5
#define LINE_MAX           480 // (a game's description, with who's playing)

typedef struct Game {
    bool   used;
    char   address[NET_ADDRESS_MAX];
    int    port;
    char   description[LINE_MAX];
    time_t heard;
} Game;

typedef struct Caller {
    NetSocket socket;
    char      address[NET_ADDRESS_MAX];
    char      line[LINE_MAX];
    int       length;
    time_t    since;
} Caller;

static Game         games[MAX_GAMES];
static Caller       callers[MAX_CALLERS];
static volatile int running = 1;

static void Lobby__Stop(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void Lobby__SendAll(const NetSocket socket, const char *text) {
    int       sent   = 0;
    const int length = (int)strlen(text);
    for (int tries = 0; sent < length && tries < 200; tries++) {
        const int now = Net_Send(socket, text + sent, length - sent);
        if (now < 0) {
            return;
        }
        if (now == 0) {
            Lobby__Sleep(5);
        }
        sent += now;
    }
}

static void Lobby__Register(const Caller *caller, const char *description) {
    int version = 0;
    int port    = 0;
    if (sscanf(description, "ABYSS %d %d", &version, &port) != 2 || port < 1 || port > 65535) {
        return;
    }
    Game *slot = NULL;
    for (int g = 0; g < MAX_GAMES && slot == NULL; g++) {
        if (games[g].used && games[g].port == port && strcmp(games[g].address, caller->address) == 0) {
            slot = &games[g];
        }
    }
    for (int g = 0; g < MAX_GAMES && slot == NULL; g++) {
        slot = games[g].used ? NULL : &games[g];
    }
    if (slot == NULL) {
        return; // full
    }
    const bool fresh = !slot->used;
    slot->used       = true;
    slot->port       = port;
    slot->heard      = time(NULL);
    snprintf(slot->address, sizeof(slot->address), "%s", caller->address);
    snprintf(slot->description, sizeof(slot->description), "%s", description);
    if (fresh) {
        printf("+ %s:%d %s\n", slot->address, port, description);
        fflush(stdout);
    }
}

static void Lobby__Unregister(const Caller *caller, const int port) {
    for (int g = 0; g < MAX_GAMES; g++) {
        if (games[g].used && games[g].port == port && strcmp(games[g].address, caller->address) == 0) {
            games[g].used = false;
            printf("- %s:%d\n", caller->address, port);
            fflush(stdout);
        }
    }
}

static void Lobby__List(const Caller *caller) {
    char line[LINE_MAX + NET_ADDRESS_MAX + 16];
    for (int g = 0; g < MAX_GAMES; g++) {
        if (games[g].used) {
            snprintf(line, sizeof(line), "GAME %s %s\n", games[g].address, games[g].description);
            Lobby__SendAll(caller->socket, line);
        }
    }
    Lobby__SendAll(caller->socket, "END\n");
}

// One whole line from a caller: does it, and the call is over
static void Lobby__Handle(Caller *caller) {
    caller->line[strcspn(caller->line, "\r\n")] = '\0';
    if (strncmp(caller->line, "REGISTER ", 9) == 0) {
        Lobby__Register(caller, caller->line + 9);
    } else if (strncmp(caller->line, "UNREGISTER ", 11) == 0) {
        Lobby__Unregister(caller, atoi(caller->line + 11));
    } else if (strcmp(caller->line, "LIST") == 0) {
        Lobby__List(caller);
    }
}

int main(int argc, char **argv) {
    const int port = argc > 1 ? atoi(argv[1]) : LOBBY_DEFAULT_PORT;
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Usage: abyss-lobby [port]   (default %d)\n", LOBBY_DEFAULT_PORT);
        return 1;
    }
    if (!Net_Init()) {
        fprintf(stderr, "The network could not be started.\n");
        return 1;
    }
    const NetSocket listener = Net_Listen((uint16_t)port);
    if (listener == NET_INVALID_SOCKET) {
        fprintf(stderr, "Port %d is in use (or can't be opened).\n", port);
        return 1;
    }
    signal(SIGINT, Lobby__Stop);
    signal(SIGTERM, Lobby__Stop);
    for (int c = 0; c < MAX_CALLERS; c++) {
        callers[c].socket = NET_INVALID_SOCKET;
    }
    printf("abyss-lobby on port %d (Ctrl+C to stop)\n", port);
    fflush(stdout);

    while (running) {
        const time_t now = time(NULL);
        // New callers
        for (;;) {
            char            address[NET_ADDRESS_MAX];
            const NetSocket socket = Net_Accept(listener, address, sizeof(address));
            if (socket == NET_INVALID_SOCKET) {
                break;
            }
            Caller *slot = NULL;
            for (int c = 0; c < MAX_CALLERS && slot == NULL; c++) {
                slot = callers[c].socket == NET_INVALID_SOCKET ? &callers[c] : NULL;
            }
            if (slot == NULL) {
                Net_Close(socket); // busy: they'll try again
                continue;
            }
            memset(slot, 0, sizeof(Caller));
            slot->socket = socket;
            slot->since  = now;
            snprintf(slot->address, sizeof(slot->address), "%s", address);
        }
        // Their lines
        for (int c = 0; c < MAX_CALLERS; c++) {
            Caller *caller = &callers[c];
            if (caller->socket == NET_INVALID_SOCKET) {
                continue;
            }
            const int got = Net_Receive(caller->socket, caller->line + caller->length, LINE_MAX - 1 - caller->length);
            if (got > 0) {
                caller->length               += got;
                caller->line[caller->length]  = '\0';
            }
            const bool whole = strchr(caller->line, '\n') != NULL;
            if (whole) {
                Lobby__Handle(caller);
            }
            if (whole || got < 0 || caller->length >= LINE_MAX - 1 || now - caller->since > CALLER_TIMEOUT_S) {
                Net_Close(caller->socket);
                caller->socket = NET_INVALID_SOCKET;
            }
        }
        // Games nobody has heard from
        for (int g = 0; g < MAX_GAMES; g++) {
            if (games[g].used && now - games[g].heard > GAME_EXPIRES_S) {
                games[g].used = false;
                printf("~ %s:%d (gone quiet)\n", games[g].address, games[g].port);
                fflush(stdout);
            }
        }
        Lobby__Sleep(20);
    }
    Net_Close(listener);
    Net_Quit();
    printf("abyss-lobby stopped\n");
    return 0;
}
