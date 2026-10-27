#include "Protocol.h"
#include "../common/AbyssConfiguration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void NetSettings_Defaults(NetSettings *settings) {
    memset(settings, 0, sizeof(NetSettings));
    snprintf(settings->name, sizeof(settings->name), "Abyss game");
    settings->max_players     = NET_DEFAULT_PLAYERS;
    settings->difficulty      = DIFFICULTY_NORMAL;
    settings->hardcore        = HARDCORE_ANY;
    settings->level_min       = 1;
    settings->level_max       = CHARACTER_MAX_LEVEL;
    settings->speed           = SPEED_NORMAL;
    settings->inventory_pages = 1;
    settings->lan             = true;
    settings->port            = NET_DEFAULT_PORT;
}

void NetItem_Write(NetWriter *writer, const Item *item) {
    NetWriter_String(writer, item->code);
    NetWriter_I32(writer, item->defense);
    NetWriter_I32(writer, item->quantity);
    NetWriter_U8(writer, item->quality);
    NetWriter_U16(writer, (uint16_t)item->name1);
    NetWriter_U16(writer, (uint16_t)item->name2);
    const int count = item->mod_count < ITEM_MODS_MAX ? item->mod_count : ITEM_MODS_MAX;
    NetWriter_U8(writer, (uint8_t)count);
    for (int m = 0; m < count; m++) {
        NetWriter_U8(writer, item->mods[m].type);
        NetWriter_U16(writer, (uint16_t)item->mods[m].value);
        NetWriter_U16(writer, (uint16_t)item->mods[m].param);
    }
    NetWriter_U8(writer, item->sockets);
    const int in = item->socketed_count < item->sockets ? item->socketed_count : item->sockets;
    NetWriter_U8(writer, (uint8_t)in);
    for (int s = 0; s < in; s++) {
        NetWriter_String(writer, item->socketed[s]);
    }
    NetWriter_U16(writer, (uint16_t)item->durability);
    NetWriter_U16(writer, (uint16_t)item->max_durability);
    NetWriter_U8(writer, item->unidentified ? 1 : 0);
}

void NetItem_Read(NetReader *reader, Item *item) {
    memset(item, 0, sizeof(Item));
    NetReader_String(reader, item->code, sizeof(item->code));
    item->defense   = NetReader_I32(reader);
    item->quantity  = NetReader_I32(reader);
    item->quality   = NetReader_U8(reader);
    item->quality   = item->quality < ITEM_QUALITY_COUNT ? item->quality : ITEM_NORMAL;
    item->name1     = (int16_t)NetReader_U16(reader);
    item->name2     = (int16_t)NetReader_U16(reader);
    const int count = NetReader_U8(reader);
    for (int m = 0; m < count && !reader->failed; m++) {
        const int type  = NetReader_U8(reader);
        const int value = (int16_t)NetReader_U16(reader);
        const int param = (int16_t)NetReader_U16(reader);
        if (type < MOD_TYPE_COUNT && item->mod_count < ITEM_MODS_MAX) {
            item->mods[item->mod_count].type    = (uint8_t)type;
            item->mods[item->mod_count].param   = (int16_t)param;
            item->mods[item->mod_count++].value = (int16_t)value;
        }
    }
    const int sockets = NetReader_U8(reader);
    item->sockets     = (uint8_t)(sockets > ITEM_SOCKETS_MAX ? ITEM_SOCKETS_MAX : sockets);
    const int in      = NetReader_U8(reader);
    for (int s = 0; s < in && !reader->failed; s++) {
        char code[ITEM_CODE_MAX];
        NetReader_String(reader, code, sizeof(code));
        if (item->socketed_count < item->sockets) {
            snprintf(item->socketed[item->socketed_count++], ITEM_CODE_MAX, "%s", code);
        }
    }
    item->durability     = (int16_t)NetReader_U16(reader);
    item->max_durability = (int16_t)NetReader_U16(reader);
    item->unidentified   = NetReader_U8(reader) != 0;
}

void NetSettings_Write(const NetSettings *settings, NetWriter *writer, const bool with_password) {
    NetWriter_String(writer, settings->name);
    NetWriter_String(writer, with_password ? settings->password : "");
    NetWriter_U8(writer, (uint8_t)settings->max_players);
    NetWriter_U8(writer, (uint8_t)settings->difficulty);
    NetWriter_U8(writer, settings->new_characters_only ? 1 : 0);
    NetWriter_U8(writer, settings->made_here_only ? 1 : 0);
    NetWriter_U8(writer, (uint8_t)settings->hardcore);
    NetWriter_U8(writer, (uint8_t)settings->level_min);
    NetWriter_U8(writer, (uint8_t)settings->level_max);
    NetWriter_U8(writer, settings->pvp ? 1 : 0);
    NetWriter_U8(writer, (uint8_t)settings->speed);
    NetWriter_U8(writer, (uint8_t)settings->inventory_pages);
    NetWriter_U32(writer, settings->seed);
    NetWriter_U8(writer, settings->public_game ? 1 : 0);
    NetWriter_U8(writer, settings->lan ? 1 : 0);
    NetWriter_U16(writer, settings->port);
}

static int NetSettings__Clamp(const int value, const int low, const int high) {
    return value < low ? low : (value > high ? high : value);
}

void NetSettings_Read(NetSettings *settings, NetReader *reader) {
    NetReader_String(reader, settings->name, sizeof(settings->name));
    NetReader_String(reader, settings->password, sizeof(settings->password));
    settings->max_players         = NetSettings__Clamp(NetReader_U8(reader), 1, NET_MAX_PLAYERS);
    settings->difficulty          = NetSettings__Clamp(NetReader_U8(reader), 0, DIFFICULTY_COUNT - 1);
    settings->new_characters_only = NetReader_U8(reader) != 0;
    settings->made_here_only      = NetReader_U8(reader) != 0;
    settings->hardcore            = NetSettings__Clamp(NetReader_U8(reader), 0, HARDCORE_RULE_COUNT - 1);
    settings->level_min           = NetSettings__Clamp(NetReader_U8(reader), 1, CHARACTER_MAX_LEVEL);
    settings->level_max           = NetSettings__Clamp(NetReader_U8(reader), settings->level_min, CHARACTER_MAX_LEVEL);
    settings->pvp                 = NetReader_U8(reader) != 0;
    settings->speed               = NetSettings__Clamp(NetReader_U8(reader), 0, SPEED_COUNT - 1);
    settings->inventory_pages     = NetSettings__Clamp(NetReader_U8(reader), 1, 255);
    settings->seed                = NetReader_U32(reader);
    settings->public_game         = NetReader_U8(reader) != 0;
    settings->lan                 = NetReader_U8(reader) != 0;
    settings->port                = NetReader_U16(reader);
}

const char *NetSettings_DifficultyName(const int difficulty) {
    static const char *names[DIFFICULTY_COUNT] = {"Normal", "Nightmare", "Hell"};
    return difficulty >= 0 && difficulty < DIFFICULTY_COUNT ? names[difficulty] : "?";
}

const char *NetSettings_SpeedName(const int speed) {
    static const char *names[SPEED_COUNT] = {"Normal", "Fast", "Faster", "Fastest"};
    return speed >= 0 && speed < SPEED_COUNT ? names[speed] : "?";
}

const char *NetSettings_HardcoreName(const int hardcore) {
    static const char *names[HARDCORE_RULE_COUNT] = {"Any", "Softcore only", "Hardcore only"};
    return hardcore >= 0 && hardcore < HARDCORE_RULE_COUNT ? names[hardcore] : "?";
}

float NetSettings_SpeedScale(const int speed) {
    static const float scales[SPEED_COUNT] = {1.0f, 1.5f, 2.0f, 2.5f}; // Diablo: 20, 30, 40, 50 frames a second
    return speed >= 0 && speed < SPEED_COUNT ? scales[speed] : 1.0f;
}

bool NetSettings_Allows(const NetSettings *settings, const Character *character, const char *realm, const char **why) {
    static char reason[96];
    const char *no = NULL;
    if (character->dead) {
        no = "That character is dead.";
    } else if (settings->new_characters_only && (character->level > 1 || character->experience > 0)) {
        no = "This game is for new characters only.";
    } else if (settings->made_here_only && (realm == NULL || strcmp(character->realm, realm) != 0)) {
        no = "This game only takes characters made in its own games.";
    } else if (settings->hardcore == HARDCORE_SOFTCORE_ONLY && character->hardcore) {
        no = "This game is for Softcore characters.";
    } else if (settings->hardcore == HARDCORE_ONLY && !character->hardcore) {
        no = "This game is for Hardcore characters.";
    } else if (character->level < settings->level_min || character->level > settings->level_max) {
        snprintf(reason, sizeof(reason), "This game is for levels %d to %d.", settings->level_min, settings->level_max);
        no = reason;
    }
    if (why != NULL) {
        *why = no;
    }
    return no == NULL;
}

const char *Net_ClientId(void) {
    static char id[NET_ID_MAX] = "";
    if (id[0] != '\0') {
        return id;
    }
    char path[4200];
    snprintf(path, sizeof(path), "%s/client.id", AbyssConfiguration_GetConfigFolder());
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        if (fgets(id, sizeof(id), file) != NULL) {
            id[strcspn(id, "\r\n ")] = '\0';
        }
        fclose(file);
    }
    if (strlen(id) < 16) {
        // A new one: 32 hex digits from whatever varies (time, the clock, where things are in memory)
        unsigned seed = (unsigned)time(NULL) ^ (unsigned)clock() ^ (unsigned)(uintptr_t)&seed ^ (unsigned)(uintptr_t)id;
        srand(seed);
        for (int i = 0; i < 32; i++) {
            id[i] = "0123456789abcdef"[(rand() ^ (rand() >> 7)) & 15];
        }
        id[32] = '\0';
        file   = fopen(path, "w");
        if (file != NULL) {
            fprintf(file, "%s\n", id);
            fclose(file);
        }
    }
    return id;
}

static const struct {
    const char *name;
    uint8_t     rgb[3];
} colors[NET_COLOR_COUNT] = {
    {"Gold",   {0xFF, 0xD8, 0x60}},
    {"White",  {0xF0, 0xF0, 0xF0}},
    {"Red",    {0xFF, 0x50, 0x40}},
    {"Orange", {0xFF, 0x9A, 0x30}},
    {"Yellow", {0xFF, 0xF2, 0x40}},
    {"Green",  {0x60, 0xE0, 0x60}},
    {"Teal",   {0x40, 0xD8, 0xC0}},
    {"Blue",   {0x60, 0x90, 0xFF}},
    {"Purple", {0xB0, 0x70, 0xFF}},
    {"Pink",   {0xFF, 0x80, 0xC8}},
    {"Grey",   {0xA8, 0xA8, 0xA8}},
    {"Brown",  {0xC0, 0x88, 0x50}},
};

const char *NetColor_Name(const int color) {
    return color >= 0 && color < NET_COLOR_COUNT ? colors[color].name : colors[0].name;
}

void NetColor_Get(const int color, uint8_t *r, uint8_t *g, uint8_t *b) {
    const int c = color >= 0 && color < NET_COLOR_COUNT ? color : 0;
    *r          = colors[c].rgb[0];
    *g          = colors[c].rgb[1];
    *b          = colors[c].rgb[2];
}

// Flags in the one-line description
#define INFO_LOCKED    1
#define INFO_DEDICATED 2
#define INFO_NEW_ONLY  4
#define INFO_MADE_HERE 8
#define INFO_PVP       16

int NetGameInfo_Format(const NetGameInfo *info, char *text, const int size) {
    const int flags = (info->locked ? INFO_LOCKED : 0) | (info->dedicated ? INFO_DEDICATED : 0) |
                      (info->new_characters_only ? INFO_NEW_ONLY : 0) | (info->made_here_only ? INFO_MADE_HERE : 0) |
                      (info->pvp ? INFO_PVP : 0);
    return snprintf(text, (size_t)size, "ABYSS %d %u %d %d %d %d %d %d %d %d %08x %s", NET_PROTOCOL_VERSION,
                    (unsigned)info->port, info->players, info->max_players, info->difficulty, flags, info->speed,
                    info->hardcore, info->level_min, info->level_max, (unsigned)info->instance, info->name);
}

bool NetGameInfo_Parse(const char *text, NetGameInfo *info) {
    int      version  = 0;
    unsigned port     = 0;
    int      flags    = 0;
    int      used     = 0;
    unsigned instance = 0;
    if (sscanf(text, "ABYSS %d %u %d %d %d %d %d %d %d %d %x %n", &version, &port, &info->players, &info->max_players,
               &info->difficulty, &flags, &info->speed, &info->hardcore, &info->level_min, &info->level_max, &instance,
               &used) < 11 ||
        version != NET_PROTOCOL_VERSION || port == 0 || port > 65535) {
        return false;
    }
    info->port                = (uint16_t)port;
    info->instance            = instance;
    info->locked              = (flags & INFO_LOCKED) != 0;
    info->dedicated           = (flags & INFO_DEDICATED) != 0;
    info->new_characters_only = (flags & INFO_NEW_ONLY) != 0;
    info->made_here_only      = (flags & INFO_MADE_HERE) != 0;
    info->pvp                 = (flags & INFO_PVP) != 0;
    snprintf(info->name, sizeof(info->name), "%s", text + used);
    info->name[strcspn(info->name, "\r\n")] = '\0';
    return true;
}
