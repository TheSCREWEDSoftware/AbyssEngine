#include "SceneMultiplayer.h"
#include "../audio/Sounds.h"
#include "../common/AbyssConfiguration.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Checkbox.h"
#include "../drawing/Cursor.h"
#include "../drawing/Dropdown.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/Slider.h"
#include "../drawing/Sprite.h"
#include "../drawing/TabBar.h"
#include "../drawing/TextBox.h"
#include "../managers/InputManager.h"
#include "../net/Multiplayer.h"
#include "SceneCharacterSelect.h"
#include "SceneMainMenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Layout (the game's 800x600), like the Options screen
#define PANEL_X   40
#define PANEL_Y   30
#define PANEL_W   720
#define PANEL_H   520
#define CONTENT_X 70
#define TABS_Y    82
#define CONTENT_Y 126
#define BUTTONS_Y 560
#define RIGHT_X   430 // the Host tab's second column
#define LIST_ROWS 10

typedef enum MenuTab { TAB_BROWSE, TAB_DIRECT, TAB_HOST, TAB_COUNT } MenuTab;
static const char *tab_names[TAB_COUNT]  = {"Browse", "Direct IP", "Host"};
static const char *source_names[2]       = {"Local network (LAN)", "Online"};
static const char *difficulty_names[]    = {"Normal", "Nightmare", "Hell"};
static const char *speed_names[]         = {"Normal", "Fast", "Faster", "Fastest"};
static const char *hardcore_rule_names[] = {"Any character", "Softcore only", "Hardcore only"};

typedef struct SceneMultiplayer {
    Sprite *background;
    TabBar *tabs;
    Label  *title;
    Label  *text;  // names (font16)
    Label  *small; // details (font8)
    Button *back_button;
    Button *action_button; // Join / Start
    char    status[200];
    bool    connecting;
    Button *cancel_button;
    bool    prev_left;

    // Browse
    TabBar      *sources;
    ListBox     *games;
    Button      *refresh_button;
    TextBox     *browse_password;
    char         game_lines[64][128];
    const char  *game_items[64];
    int          game_count;
    BrowseSource source;

    // Direct IP
    TextBox *address;
    TextBox *direct_password;

    // Host
    NetSettings settings;
    TextBox    *host_name;
    TextBox    *host_password;
    TextBox    *host_port;
    TextBox    *host_seed;
    Slider     *max_players;
    Dropdown   *difficulty;
    Dropdown   *speed;
    Dropdown   *hardcore;
    Checkbox   *new_only;
    Checkbox   *made_here;
    Checkbox   *pvp;
    Checkbox   *public_game;
    Checkbox   *lan;
    Slider     *level_min;
    Slider     *level_max;
    Slider     *pages;
} SceneMultiplayer;

DEFINE_SCENE_CALLBACKS(MultiplayerMenu);

// ---------------------------------------------------------------------------------------------------------------------

static void Menu__Fill(const int x, const int y, const int w, const int h, const uint8_t r, const uint8_t g,
                       const uint8_t b, const uint8_t a) {
    const SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, r, g, b, a);
    SDL_RenderFillRect(sdl_renderer, &rect);
}

static void Menu__Frame(const int x, const int y, const int w, const int h) {
    const SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
    SDL_RenderDrawRect(sdl_renderer, &rect);
}

static void Menu__Text(Label *label, const char *text, const int x, const int y, const uint8_t r, const uint8_t g,
                       const uint8_t b) {
    Label_SetText(label, text);
    Label_SetColor(label, r, g, b, 0xFF);
    Label_Draw(label, x, y);
}

static void Menu__Name(SceneMultiplayer *menu, const char *text, const int x, const int y) {
    Menu__Text(menu->text, text, x, y + 10, 0xC7, 0xB3, 0x77);
}

// Grey text wrapped at max_x
static void Menu__Wrapped(SceneMultiplayer *menu, const char *text, const int x, int y, const int max_x) {
    char        line[256] = "";
    const char *word      = text;
    while (*word != '\0') {
        const char *end = strchr(word, ' ');
        const int   len = end != NULL ? (int)(end - word) : (int)strlen(word);
        char        candidate[512];
        snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] != '\0' ? " " : "", len, word);
        if (line[0] != '\0' && x + Label_MeasureText(menu->small, candidate) > max_x) {
            Menu__Text(menu->small, line, x, y, 0xA0, 0xA0, 0xA0);
            y += 12;
            snprintf(line, sizeof(line), "%.*s", len, word);
        } else {
            snprintf(line, sizeof(line), "%.255s", candidate);
        }
        word += len;
        while (*word == ' ') {
            word++;
        }
    }
    if (line[0] != '\0') {
        Menu__Text(menu->small, line, x, y, 0xA0, 0xA0, 0xA0);
    }
}

static bool Menu__AnyDropdownOpen(const SceneMultiplayer *menu) {
    return Dropdown_IsOpen(menu->difficulty) || Dropdown_IsOpen(menu->speed) || Dropdown_IsOpen(menu->hardcore);
}

static bool Menu__Typing(const SceneMultiplayer *menu) {
    TextBox *boxes[] = {menu->browse_password, menu->address,   menu->direct_password, menu->host_name,
                        menu->host_password,   menu->host_port, menu->host_seed};
    for (size_t b = 0; b < sizeof(boxes) / sizeof(boxes[0]); b++) {
        if (TextBox_IsFocused(boxes[b])) {
            return true;
        }
    }
    return Slider_IsTyping(menu->max_players) || Slider_IsTyping(menu->level_min) || Slider_IsTyping(menu->level_max) ||
           Slider_IsTyping(menu->pages);
}

// The Browse list's lines: "Name   2/8   Hell   (password)"
static void Menu__RefreshGames(SceneMultiplayer *menu) {
    const NetGameInfo *games = NULL;
    menu->game_count         = Browse_Games(&games);
    menu->game_count         = menu->game_count > 64 ? 64 : menu->game_count;
    for (int g = 0; g < menu->game_count; g++) {
        snprintf(menu->game_lines[g], sizeof(menu->game_lines[g]), "%-24.24s  %d/%d  %s%s%s", games[g].name,
                 games[g].players, games[g].max_players, NetSettings_DifficultyName(games[g].difficulty),
                 games[g].locked ? "  (password)" : "", games[g].dedicated ? "  (server)" : "");
        menu->game_items[g] = menu->game_lines[g];
    }
    const int selected = ListBox_GetSelectedIndex(menu->games);
    ListBox_SetItems(menu->games, menu->game_items, menu->game_count);
    ListBox_SetSelectedIndex(menu->games, selected < menu->game_count ? selected : -1);
}

static void Menu__StartSearch(SceneMultiplayer *menu) {
    menu->source = TabBar_GetSelected(menu->sources) == 0 ? BROWSE_LAN : BROWSE_ONLINE;
    Browse_Start(menu->source);
    menu->game_count = 0;
    ListBox_SetItems(menu->games, menu->game_items, 0);
}

// The Host tab's widgets from settings, and back
static void Menu__LoadHost(SceneMultiplayer *menu) {
    const NetSettings *s = &menu->settings;
    TextBox_SetText(menu->host_name, s->name);
    TextBox_SetText(menu->host_password, s->password);
    char text[24];
    snprintf(text, sizeof(text), "%u", (unsigned)s->port);
    TextBox_SetText(menu->host_port, text);
    if (s->seed != 0) {
        snprintf(text, sizeof(text), "%u", (unsigned)s->seed);
        TextBox_SetText(menu->host_seed, text);
    } else {
        TextBox_Clear(menu->host_seed);
    }
    Slider_SetValue(menu->max_players, s->max_players);
    Dropdown_SetSelected(menu->difficulty, s->difficulty);
    Dropdown_SetSelected(menu->speed, s->speed);
    Dropdown_SetSelected(menu->hardcore, s->hardcore);
    Checkbox_SetChecked(menu->new_only, s->new_characters_only);
    Checkbox_SetChecked(menu->made_here, s->made_here_only);
    Checkbox_SetChecked(menu->pvp, s->pvp);
    Checkbox_SetChecked(menu->public_game, s->public_game);
    Checkbox_SetChecked(menu->lan, s->lan);
    Slider_SetValue(menu->level_min, s->level_min);
    Slider_SetValue(menu->level_max, s->level_max);
    Slider_SetValue(menu->pages, s->inventory_pages);
}

static bool Menu__StoreHost(SceneMultiplayer *menu) {
    NetSettings *s = &menu->settings;
    snprintf(s->name, sizeof(s->name), "%s", TextBox_GetText(menu->host_name));
    if (s->name[0] == '\0') {
        snprintf(menu->status, sizeof(menu->status), "Give the game a name.");
        return false;
    }
    snprintf(s->password, sizeof(s->password), "%s", TextBox_GetText(menu->host_password));
    const long port = strtol(TextBox_GetText(menu->host_port), NULL, 10);
    if (port < 1 || port > 65535) {
        snprintf(menu->status, sizeof(menu->status), "The port is a number from 1 to 65535 (%d by default).",
                 NET_DEFAULT_PORT);
        return false;
    }
    s->port                = (uint16_t)port;
    s->seed                = (uint32_t)strtoul(TextBox_GetText(menu->host_seed), NULL, 10);
    s->max_players         = Slider_GetValue(menu->max_players);
    s->difficulty          = Dropdown_GetSelected(menu->difficulty);
    s->speed               = Dropdown_GetSelected(menu->speed);
    s->hardcore            = Dropdown_GetSelected(menu->hardcore);
    s->new_characters_only = Checkbox_IsChecked(menu->new_only);
    s->made_here_only      = Checkbox_IsChecked(menu->made_here);
    s->pvp                 = Checkbox_IsChecked(menu->pvp);
    s->public_game         = Checkbox_IsChecked(menu->public_game);
    s->lan                 = Checkbox_IsChecked(menu->lan);
    s->level_min           = Slider_GetValue(menu->level_min);
    s->level_max           = Slider_GetValue(menu->level_max);
    s->inventory_pages     = Slider_GetValue(menu->pages);
    if (s->level_max < s->level_min) {
        snprintf(menu->status, sizeof(menu->status), "The highest level is below the lowest.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

void *MultiplayerMenu_Create(void) {
    Cursor_SetVisible(true);
    SceneMultiplayer *menu = calloc(1, sizeof(SceneMultiplayer));
    FAIL_IF_NULL(menu);
    menu->background = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    menu->tabs       = TabBar_Create(tab_names, TAB_COUNT, TAB_BROWSE);
    menu->title      = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetText(menu->title, "Multiplayer");
    Label_SetAlignment(menu->title, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(menu->title, 0xC7, 0xB3, 0x77, 0xFF);
    menu->text = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(menu->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    menu->small = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetAlignment(menu->small, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);

    menu->back_button   = Button_Create(BUTTON_TYPE_MEDIUM, "Back");
    menu->action_button = Button_Create(BUTTON_TYPE_MEDIUM, "Join");
    menu->cancel_button = Button_Create(BUTTON_TYPE_MEDIUM, "Cancel");
    Button_SetPosition(menu->back_button, PANEL_X, BUTTONS_Y);
    Button_SetPosition(menu->action_button, PANEL_X + PANEL_W - 128, BUTTONS_Y);
    Button_SetPosition(menu->cancel_button, 400 - 64, 330);

    // Browse
    menu->sources         = TabBar_Create(source_names, 2, 0);
    menu->games           = ListBox_Create(FONTS_FONT16, CONTENT_X, CONTENT_Y + 40, PANEL_W - 60, LIST_ROWS, 20);
    menu->refresh_button  = Button_Create(BUTTON_TYPE_CANCEL, "Refresh");
    menu->browse_password = TextBox_Create(FONTS_FONT16, CONTENT_X + 160, CONTENT_Y + 312, 220, 22, "Password");
    Button_SetPosition(menu->refresh_button, PANEL_X + PANEL_W - 128, CONTENT_Y + 2);

    // Direct IP
    menu->address         = TextBox_Create(FONTS_FONT16, CONTENT_X + 160, CONTENT_Y + 6, 320, 22, "host or host:port");
    menu->direct_password = TextBox_Create(FONTS_FONT16, CONTENT_X + 160, CONTENT_Y + 66, 220, 22, "Password");
    TextBox_SetText(menu->address, AbyssConfiguration_GetLastAddress());

    // Host
    menu->host_name     = TextBox_Create(FONTS_FONT16, CONTENT_X + 150, CONTENT_Y + 6, 190, 22, "Game name");
    menu->host_password = TextBox_Create(FONTS_FONT16, CONTENT_X + 150, CONTENT_Y + 46, 190, 22, "None");
    menu->max_players   = Slider_Create(CONTENT_X + 150, CONTENT_Y + 90, 110, 1, NET_MAX_PLAYERS);
    menu->difficulty    = Dropdown_Create(CONTENT_X + 150, CONTENT_Y + 128, 190, difficulty_names, DIFFICULTY_COUNT);
    menu->speed         = Dropdown_Create(CONTENT_X + 150, CONTENT_Y + 168, 190, speed_names, SPEED_COUNT);
    menu->host_port     = TextBox_Create(FONTS_FONT16, CONTENT_X + 150, CONTENT_Y + 206, 90, 22, "4000");
    menu->host_seed     = TextBox_Create(FONTS_FONT16, CONTENT_X + 150, CONTENT_Y + 246, 190, 22, "Random");
    menu->new_only      = Checkbox_Create(RIGHT_X, CONTENT_Y + 6, "New characters only");
    menu->made_here     = Checkbox_Create(RIGHT_X, CONTENT_Y + 32, "Only characters made in my games");
    menu->pvp           = Checkbox_Create(RIGHT_X, CONTENT_Y + 58, "PvP: players can hurt each other");
    menu->lan           = Checkbox_Create(RIGHT_X, CONTENT_Y + 84, "Show on the local network");
    menu->public_game   = Checkbox_Create(RIGHT_X, CONTENT_Y + 110, "List online (the lobby server)");
    menu->hardcore  = Dropdown_Create(RIGHT_X + 110, CONTENT_Y + 146, 170, hardcore_rule_names, HARDCORE_RULE_COUNT);
    menu->level_min = Slider_Create(RIGHT_X + 140, CONTENT_Y + 190, 80, 1, CHARACTER_MAX_LEVEL);
    menu->level_max = Slider_Create(RIGHT_X + 140, CONTENT_Y + 230, 80, 1, CHARACTER_MAX_LEVEL);
    menu->pages     = Slider_Create(RIGHT_X + 140, CONTENT_Y + 270, 80, 1, 255);
    Multiplayer_LoadHostSettings(&menu->settings);
    Menu__LoadHost(menu);

    // Back from a game that ended (or a host that turned us away): say why
    if (NetClient_Stage() == NETC_FAILED) {
        snprintf(menu->status, sizeof(menu->status), "%s", NetClient_Error());
    }
    Multiplayer_Leave();
    Menu__StartSearch(menu);
    return menu;
}

void MultiplayerMenu_Free(void **scene_ref) {
    SceneMultiplayer *menu = *scene_ref;
    Browse_Stop();
    Sprite_Destroy(&menu->background);
    TabBar_Destroy(&menu->tabs);
    TabBar_Destroy(&menu->sources);
    Label_Destroy(&menu->title);
    Label_Destroy(&menu->text);
    Label_Destroy(&menu->small);
    Button *buttons[] = {menu->back_button, menu->action_button, menu->cancel_button, menu->refresh_button};
    for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
        Button_Destroy(&buttons[b]);
    }
    TextBox *boxes[] = {menu->browse_password, menu->address,   menu->direct_password, menu->host_name,
                        menu->host_password,   menu->host_port, menu->host_seed};
    for (size_t b = 0; b < sizeof(boxes) / sizeof(boxes[0]); b++) {
        TextBox_Destroy(&boxes[b]);
    }
    ListBox_Destroy(&menu->games);
    Slider_Destroy(&menu->max_players);
    Dropdown_Destroy(&menu->difficulty);
    Dropdown_Destroy(&menu->speed);
    Dropdown_Destroy(&menu->hardcore);
    Checkbox *checks[] = {menu->new_only, menu->made_here, menu->pvp, menu->public_game, menu->lan};
    for (size_t c = 0; c < sizeof(checks) / sizeof(checks[0]); c++) {
        Checkbox_Destroy(&checks[c]);
    }
    Slider_Destroy(&menu->level_min);
    Slider_Destroy(&menu->level_max);
    Slider_Destroy(&menu->pages);
    free(menu);
    *scene_ref = NULL;
}

// ---------------------------------------------------------------------------------------------------------------------

static void Menu__Join(SceneMultiplayer *menu, const char *host, const uint16_t port, const char *password) {
    Browse_Stop();
    menu->status[0] = '\0';
    if (NetClient_Connect(host, port, password)) {
        menu->connecting = true;
    } else {
        snprintf(menu->status, sizeof(menu->status), "%s", NetClient_Error());
    }
}

static void Menu__Action(SceneMultiplayer *menu, const MenuTab tab) {
    switch (tab) {
    case TAB_BROWSE: {
        const int          row   = ListBox_GetSelectedIndex(menu->games);
        const NetGameInfo *games = NULL;
        const int          count = Browse_Games(&games);
        if (row < 0 || row >= count) {
            snprintf(menu->status, sizeof(menu->status), "Pick a game from the list.");
            return;
        }
        Menu__Join(menu, games[row].address, games[row].port, TextBox_GetText(menu->browse_password));
        break;
    }
    case TAB_DIRECT: {
        char     host[256];
        uint16_t port = NET_DEFAULT_PORT;
        if (!Net_ParseAddress(TextBox_GetText(menu->address), host, sizeof(host), &port, NET_DEFAULT_PORT)) {
            snprintf(menu->status, sizeof(menu->status), "Type an address: a name or IP, with :port if it isn't %d.",
                     NET_DEFAULT_PORT);
            return;
        }
        AbyssConfiguration_SetLastAddress(TextBox_GetText(menu->address));
        AbyssConfiguration_Save();
        Menu__Join(menu, host, port, TextBox_GetText(menu->direct_password));
        break;
    }
    case TAB_HOST: {
        if (!Menu__StoreHost(menu)) {
            return;
        }
        Multiplayer_SaveHostSettings(&menu->settings);
        Browse_Stop();
        char error[160];
        if (Multiplayer_Host(&menu->settings, error, sizeof(error))) {
            menu->connecting = true;
            menu->status[0]  = '\0';
        } else {
            snprintf(menu->status, sizeof(menu->status), "%s", error);
        }
        break;
    }
    default:
        break;
    }
}

void MultiplayerMenu_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    SceneMultiplayer *menu = scene_ref;

    if (menu->connecting) {
        const NetClientStage stage = NetClient_Stage();
        if (stage == NETC_CHARACTERS) {
            Scene_Set(SCENE_REF(CharacterSelect)); // in: pick a character there
            return;
        }
        if (stage == NETC_FAILED || stage == NETC_OFF) {
            snprintf(menu->status, sizeof(menu->status), "%s", NetClient_Error());
            menu->connecting = false;
            Multiplayer_Leave();
        } else if (Button_Update(menu->cancel_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
            Multiplayer_Leave();
            menu->connecting = false;
        }
        return;
    }

    const MenuTab tab = (MenuTab)TabBar_GetSelected(menu->tabs);
    if (Menu__AnyDropdownOpen(menu)) {
        // An open list takes the clicks
        Dropdown *open[] = {menu->difficulty, menu->speed, menu->hardcore};
        for (size_t d = 0; d < sizeof(open) / sizeof(open[0]); d++) {
            if (Dropdown_IsOpen(open[d])) {
                Dropdown_Update(open[d]);
                break;
            }
        }
        return;
    }
    if (TabBar_Update(menu->tabs, CONTENT_X, TABS_Y, PANEL_W - 60)) {
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        menu->status[0] = '\0';
        Button_SetText(menu->action_button, TabBar_GetSelected(menu->tabs) == TAB_HOST ? "Start" : "Join");
    }

    switch (tab) {
    case TAB_BROWSE: {
        if (TabBar_Update(menu->sources, CONTENT_X, CONTENT_Y, 420) || Button_Update(menu->refresh_button)) {
            Menu__StartSearch(menu);
        }
        Menu__RefreshGames(menu);
        bool left;
        int  mouse_x;
        int  mouse_y;
        InputManager_GetMouseButtons(&left, NULL, NULL);
        InputManager_GetMousePosition(&mouse_x, &mouse_y);
        const bool clicked = left && !menu->prev_left;
        const int  before  = ListBox_GetSelectedIndex(menu->games);
        ListBox_Update(menu->games);
        // Double click a game to join it
        static uint64_t last_click = 0;
        if (clicked && ListBox_GetSelectedIndex(menu->games) >= 0 && mouse_y >= CONTENT_Y + 40 &&
            mouse_y < CONTENT_Y + 40 + (LIST_ROWS * 20)) {
            const uint64_t now = SDL_GetTicks64();
            if (before == ListBox_GetSelectedIndex(menu->games) && now - last_click < 400) {
                Menu__Action(menu, tab);
            }
            last_click = now;
        }
        TextBox_Update(menu->browse_password);
        break;
    }
    case TAB_DIRECT:
        TextBox_Update(menu->address);
        TextBox_Update(menu->direct_password);
        break;
    case TAB_HOST:
        TextBox_Update(menu->host_name);
        TextBox_Update(menu->host_password);
        TextBox_Update(menu->host_port);
        TextBox_Update(menu->host_seed);
        Slider_Update(menu->max_players);
        Dropdown_Update(menu->difficulty);
        Dropdown_Update(menu->speed);
        Dropdown_Update(menu->hardcore);
        Checkbox_Update(menu->new_only);
        Checkbox_Update(menu->made_here);
        Checkbox_Update(menu->pvp);
        Checkbox_Update(menu->lan);
        Checkbox_Update(menu->public_game);
        Slider_Update(menu->level_min);
        Slider_Update(menu->level_max);
        Slider_Update(menu->pages);
        break;
    default:
        break;
    }
    bool left;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    menu->prev_left = left;

    if (Button_Update(menu->action_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN)) {
        Menu__Action(menu, tab);
        return;
    }
    if (Button_Update(menu->back_button) ||
        (!Menu__Typing(menu) && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE))) {
        Scene_Set(SCENE_REF(MainMenu));
    }
}

// ---------------------------------------------------------------------------------------------------------------------

// The selected game's rules, in a line or two
static void Menu__DrawGameDetails(SceneMultiplayer *menu, const NetGameInfo *game, const int y) {
    char rules[400];
    char levels[40] = "";
    if (game->level_min > 1 || game->level_max < CHARACTER_MAX_LEVEL) {
        snprintf(levels, sizeof(levels), "Levels %d-%d. ", game->level_min, game->level_max);
    }
    snprintf(rules, sizeof(rules), "%s at %s:%u. %s, game speed %s. %s%s%s%s%s%s",
             game->dedicated ? "A dedicated server (it keeps your characters)" : "Hosted by a player", game->address,
             (unsigned)game->port, NetSettings_DifficultyName(game->difficulty), NetSettings_SpeedName(game->speed),
             game->hardcore == HARDCORE_ANY ? ""
                                            : (game->hardcore == HARDCORE_ONLY ? "Hardcore only. " : "Softcore only. "),
             levels, game->new_characters_only ? "New characters only. " : "",
             game->made_here_only ? "Only characters made there. " : "", game->pvp ? "PvP. " : "",
             game->locked ? "Needs a password." : "");
    Menu__Wrapped(menu, rules, CONTENT_X, y, PANEL_X + PANEL_W - 16);
}

void MultiplayerMenu_Render(void *scene_ref) {
    SceneMultiplayer *menu = scene_ref;
    Sprite_DrawMulti(menu->background, 0, 0, 0, 4, 3);
    Menu__Fill(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 8, 7, 6, 255);
    Menu__Frame(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    Label_Draw(menu->title, 400, PANEL_Y + 24);
    TabBar_Draw(menu->tabs, CONTENT_X, TABS_Y, PANEL_W - 60);

    const MenuTab tab = (MenuTab)TabBar_GetSelected(menu->tabs);
    switch (tab) {
    case TAB_BROWSE: {
        TabBar_Draw(menu->sources, CONTENT_X, CONTENT_Y, 420);
        Button_Draw(menu->refresh_button);
        ListBox_Draw(menu->games);
        Menu__Frame(CONTENT_X, CONTENT_Y + 40, PANEL_W - 60, LIST_ROWS * 20);
        if (menu->game_count == 0) {
            Menu__Text(menu->small, Browse_Status(), CONTENT_X + 12, CONTENT_Y + 54, 0xA0, 0xA0, 0xA0);
        } else {
            Menu__Text(menu->small, Browse_Status(), CONTENT_X, CONTENT_Y + 254, 0x90, 0x90, 0x90);
        }
        const int          row   = ListBox_GetSelectedIndex(menu->games);
        const NetGameInfo *games = NULL;
        const int          count = Browse_Games(&games);
        if (row >= 0 && row < count) {
            Menu__DrawGameDetails(menu, &games[row], CONTENT_Y + 274);
            if (games[row].locked) {
                Menu__Name(menu, "Password", CONTENT_X, CONTENT_Y + 312);
                TextBox_Draw(menu->browse_password);
            }
        }
        Menu__Wrapped(menu,
                      menu->source == BROWSE_LAN
                          ? "Games on your local network answer here. Double click one (or pick it and Join)."
                          : "Public games the lobby server knows about ([Network] Lobby in abyss.ini).",
                      CONTENT_X, CONTENT_Y + 352, PANEL_X + PANEL_W - 16);
        break;
    }
    case TAB_DIRECT:
        Menu__Name(menu, "Address", CONTENT_X, CONTENT_Y + 6);
        TextBox_Draw(menu->address);
        Menu__Wrapped(menu,
                      "A name or IP address, with :port after it if the game isn't on port 4000 (\"1.2.3.4\", "
                      "\"example.com:4100\", \"[::1]:4000\").",
                      CONTENT_X + 160, CONTENT_Y + 40, PANEL_X + PANEL_W - 16);
        Menu__Name(menu, "Password", CONTENT_X, CONTENT_Y + 66);
        TextBox_Draw(menu->direct_password);
        Menu__Wrapped(menu, "Only if the game has one.", CONTENT_X + 160, CONTENT_Y + 100, PANEL_X + PANEL_W - 16);
        break;
    case TAB_HOST:
        Menu__Name(menu, "Game name", CONTENT_X, CONTENT_Y + 6);
        TextBox_Draw(menu->host_name);
        Menu__Name(menu, "Password", CONTENT_X, CONTENT_Y + 46);
        TextBox_Draw(menu->host_password);
        Menu__Name(menu, "Players", CONTENT_X, CONTENT_Y + 84);
        Slider_Draw(menu->max_players);
        Menu__Name(menu, "Difficulty", CONTENT_X, CONTENT_Y + 124);
        Dropdown_Draw(menu->difficulty);
        Menu__Name(menu, "Game speed", CONTENT_X, CONTENT_Y + 164);
        Dropdown_Draw(menu->speed);
        Menu__Name(menu, "Port", CONTENT_X, CONTENT_Y + 206);
        TextBox_Draw(menu->host_port);
        Menu__Name(menu, "World seed", CONTENT_X, CONTENT_Y + 246);
        TextBox_Draw(menu->host_seed);
        Checkbox_Draw(menu->new_only);
        Checkbox_Draw(menu->made_here);
        Checkbox_Draw(menu->pvp);
        Checkbox_Draw(menu->lan);
        Checkbox_Draw(menu->public_game);
        Menu__Name(menu, "Characters", RIGHT_X, CONTENT_Y + 142);
        Dropdown_Draw(menu->hardcore);
        Menu__Name(menu, "Lowest level", RIGHT_X, CONTENT_Y + 186);
        Slider_Draw(menu->level_min);
        Menu__Name(menu, "Highest level", RIGHT_X, CONTENT_Y + 226);
        Slider_Draw(menu->level_max);
        Menu__Name(menu, "Inventory pages", RIGHT_X, CONTENT_Y + 266);
        Slider_Draw(menu->pages);
        Menu__Wrapped(menu,
                      "Your own game: others join by your address and port (open the port on your router to play "
                      "over the internet). Characters stay on each player's computer; ones made in your games are "
                      "marked as yours. Difficulty and PvP take effect once monsters and combat are shared.",
                      CONTENT_X, CONTENT_Y + 316, PANEL_X + PANEL_W - 16);
        Menu__Wrapped(menu,
                      "A dedicated server (AbyssEngine --server) keeps the characters itself and has the same "
                      "settings in server/server.ini.",
                      CONTENT_X, CONTENT_Y + 358, PANEL_X + PANEL_W - 16);
        break;
    default:
        break;
    }

    Button_Draw(menu->back_button);
    Button_Draw(menu->action_button);
    if (menu->status[0] != '\0') {
        Label_SetAlignment(menu->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Menu__Text(menu->text, menu->status, 400, PANEL_Y + PANEL_H - 14, 0xFF, 0x80, 0x60);
        Label_SetAlignment(menu->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    }
    if (tab == TAB_HOST) {
        Dropdown_DrawList(menu->difficulty);
        Dropdown_DrawList(menu->speed);
        Dropdown_DrawList(menu->hardcore);
    }

    if (menu->connecting) {
        Menu__Fill(0, 0, 800, 600, 0, 0, 0, 160);
        Menu__Fill(200, 240, 400, 140, 18, 16, 14, 245);
        Menu__Frame(200, 240, 400, 140);
        char text[160];
        snprintf(text, sizeof(text), "Connecting to %s...", NetClient_Address());
        Label_SetAlignment(menu->text, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        Menu__Text(menu->text, text, 400, 285, 0xFF, 0xFF, 0xFF);
        Label_SetAlignment(menu->text, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
        Button_Draw(menu->cancel_button);
    }
}
