#include "audio/Sounds.h"
#include "common/AbyssConfiguration.h"
#include "common/CrashHandler.h"
#include "common/FileManager.h"
#include "common/Globals.h"
#include "common/Logging.h"
#include "common/Mods.h"
#include "common/Revision.h"
#include "common/Terminal.h"
#include "data/DataTable.h"
#include "data/StringTable.h"
#include "drawing/Cursor.h"
#include "drawing/Label.h"
#include "drawing/LoadingScreen.h"
#include "drawing/Picture.h"
#include "game/Character.h"
#include "game/Controls.h"
#include "game/ControlsExport.h"
#include "game/Settings.h"
#include "managers/AudioManager.h"
#include "managers/InputManager.h"
#include "managers/VideoManager.h"
#include "map/LevelTypes.h"
#include "map/MapSession.h"
#include "net/Multiplayer.h"
#include "scenes/Scene.h"
#include "scenes/SceneGame.h"
#include "scenes/SceneIntroVideos.h"
#include "scenes/SceneMapEditor.h"
#include "scenes/SceneOptions.h"
#include "types/Palette.h"
#include "util/Crypto.h"
#include <ctype.h>
#include <libavutil/log.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

static void Main__Usage(void) {
    printf("Abyss Engine\n\n"
           "  AbyssEngine                          play\n"
           "  AbyssEngine --settings               just the Options screen (video, audio, gameplay, controls)\n"
           "  AbyssEngine --export-controls DIR    write controls.md, the keyboard and gamepad pictures and the\n"
           "                                       Single Player / Free Roam key tables into DIR (your bindings)\n"
           "  AbyssEngine --export-controls DIR --defaults   the same with the default bindings\n"
           "  AbyssEngine --server [--port N]      a dedicated multiplayer server, no window (settings: server.ini\n"
           "                                       in the server folder next to abyss.ini; Ctrl+C stops it)\n"
           "  AbyssEngine --text [NAME] [--class N]   Single Player in the terminal, as text (NAME: a saved\n"
           "                                       character, or a new one of class N: 0 Amazon .. 6 Assassin;\n"
           "                                       no NAME: the last one played). Keys: wasd move, f fight, u use,\n"
           "                                       g pick up, 1-4 potions, t town, Q quit\n"
           "  AbyssEngine --editor [MAP]           the Map Editor on its own (MAP: one of yours, by name or .abm\n"
           "                                       file, or one of the game's, \"act1/town/townN1.ds1\")\n"
           "  AbyssEngine --extract DIR            copy the game's data out of the MPQs into DIR: the tables\n"
           "                                       (.txt), the strings (.tbl), the maps (.ds1) and the tiles (.dt1);\n"
           "                                       --small keeps only the tiles' walk flags (enough for --text)\n"
           "                                       Run from it alone: [General] DataFolder = DIR in abyss.ini\n");
}

// --text: the character to play (a saved one by name, the last one played, or a new one) and into the game
static bool Main__TextCharacter(const char *name, const int class_id) {
    Character *saved = NULL;
    const int  count = Character_ListSaved(&saved);
    Character  character;
    bool       found = false;
    for (int i = 0; i < count && !found; i++) {
        if (name == NULL ? i == 0 : SDL_strcasecmp(saved[i].name, name) == 0) {
            character = saved[i];
            found     = true;
        }
    }
    free(saved);
    if (!found) {
        const char *reason   = NULL;
        const char *new_name = name != NULL ? name : "Wanderer";
        if (!Character_IsValidName(new_name, &reason)) {
            fprintf(stderr, "%s: %s\n", new_name, reason != NULL ? reason : "not a valid name");
            return false;
        }
        const CharacterClass cls =
            class_id >= 0 && class_id < CHARACTER_CLASS_COUNT ? (CharacterClass)class_id : CHARACTER_BARBARIAN;
        Character_Create(&character, new_name, cls, Character_ClassIsExpansion(cls), false);
        if (!Character_Save(&character)) {
            fprintf(stderr, "Could not save the character.\n");
            return false;
        }
    }
    Character_SetDifficulty(0);
    Character_SetCurrent(&character);
    Scene_Set(SCENE_REF(Game));
    return true;
}

// --text, each frame: keys to the game, and the screen drawn again ten times a second. False once the game is left.
static bool Main__TextFrame(const uint64_t ticks, uint64_t *drawn) {
    static char screen[1 << 18];
    void       *scene_ref = NULL;
    Scene      *scene     = Scene_Current(&scene_ref);
    if (scene != NULL && scene != SCENE_REF(Game)) {
        return false;
    }
    if (scene == NULL || scene_ref == NULL) {
        return true; // still starting
    }
    for (int key = Terminal_ReadKey(); key >= 0; key = Terminal_ReadKey()) {
        Game_TextKey(scene_ref, key);
    }
    if (ticks - *drawn >= 100) {
        *drawn   = ticks;
        int cols = 80;
        int rows = 24;
        Terminal_Size(&cols, &rows);
        const int length = Game_TextRender(scene_ref, screen, (int)sizeof(screen), cols, rows);
        Terminal_Write(screen, length);
    }
    return true;
}

// --extract: the files that describe the game (not its pictures and sounds) out of the MPQs, into a folder, keeping
// their paths (data\global\excel\levels.txt -> DIR/data/global/excel/levels.txt)
static bool Main__ExtractWanted(const char *name) {
    static const char *kinds[] = {".txt", ".tbl", ".ds1", ".dt1"};
    const char        *dot     = strrchr(name, '.');
    for (size_t k = 0; dot != NULL && k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        if (SDL_strcasecmp(dot, kinds[k]) == 0) {
            return true;
        }
    }
    return false;
}

static void Main__MakeFolders(char *path) { // every folder in the path (the last part is the file)
    for (char *c = path + 1; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\0';
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
            *c = '/';
        }
    }
}

// One file out (DIR/<its path, lower case>). small: a DT1 keeps only its tiles' headers (the walk flags, what the
// game needs to be played as text), not their pictures. False if it couldn't be read or written.
static bool Main__ExtractOne(const char *folder, const char *name, const bool small) {
    MpqStream *stream = FileManager_OpenFile(name);
    if (stream == NULL) {
        return false;
    }
    uint32_t size = MpqStream_GetSize(stream);
    char    *data = malloc(size > 0 ? size : 1);
    uint32_t got  = data != NULL ? MpqStream_Read(stream, data, 0, size) : 0;
    MpqStream_Destroy(&stream);
    const char *dot = strrchr(name, '.');
    if (small && data != NULL && got == size && dot != NULL && SDL_strcasecmp(dot, ".dt1") == 0 && size >= 276) {
        // The header and the tile headers, with no blocks
        int32_t count   = 0;
        int32_t headers = 0;
        memcpy(&count, data + 268, 4);
        memcpy(&headers, data + 272, 4);
        if (count >= 0 && headers >= 276 && (uint64_t)headers + ((uint64_t)count * 96) <= size) {
            char *out = malloc((size_t)276 + ((size_t)count * 96));
            if (out != NULL) {
                memcpy(out, data, 276);
                const int32_t at = 276;
                memcpy(out + 272, &at, 4);
                for (int t = 0; t < count; t++) {
                    char *h = out + 276 + (t * 96);
                    memcpy(h, data + headers + (t * 96), 96);
                    memset(h + 72, 0, 12); // block headers' place, data length, block count
                }
                free(data);
                data = out;
                size = got = 276 + ((uint32_t)count * 96);
            }
        }
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", folder, name[0] == '/' || name[0] == '\\' ? name + 1 : name);
    for (char *c = path + strlen(folder) + 1; *c != '\0'; c++) {
        *c = *c == '\\' ? '/' : (char)tolower((unsigned char)*c);
    }
    Main__MakeFolders(path);
    FILE      *out = data != NULL && got == size ? fopen(path, "wb") : NULL;
    const bool ok  = out != NULL && fwrite(data, 1, size, out) == size;
    if (out != NULL) {
        fclose(out);
    }
    free(data);
    return ok;
}

static int Main__Extract(const char *folder, const bool small) {
    const char *const *files = NULL;
    const int          count = FileManager_GetFileList(&files);
    int                saved = 0;
    int                lost  = 0;
    for (int i = 0; i < count; i++) {
        if (!Main__ExtractWanted(files[i]) || !FileManager_FileExists(files[i])) {
            continue;
        }
        Main__ExtractOne(folder, files[i], small) ? saved++ : lost++;
    }
    // What the MPQs' file lists leave out: the tables the engine reads, the presets' DS1s and the act's DT1s
    static const char *const tables[] = {
        "MonLvl",           "MonPreset",    "MonSounds",  "monstats2", "Sets",     "skilldesc",
        "MonType",          "MonUMod",      "MonProp",    "MonEquip",  "PetType",  "LowQualityItems",
        "QualityItems",     "ItemStatCost", "Properties", "Inventory", "BodyLocs", "Overlay",
        "States",           "ObjGroup",     "ObjMode",    "ObjType",   "LvlWarp",  "Experience",
        "DifficultyLevels", "SoundEnviron", "Sounds",     "Colors"};
    for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        char path[96];
        snprintf(path, sizeof(path), "data\\global\\excel\\%s.txt", tables[t]);
        if (FileManager_FileExists(path)) {
            Main__ExtractOne(folder, path, small) ? saved++ : lost++;
        }
    }
    const char *const lists[2][3] = {
        {"/data/global/excel/LvlPrest.txt", "File%d",  "/data/global/tiles/%s"},
        {"/data/global/excel/LvlTypes.txt", "File %d", "/data/global/tiles/%s"},
    };
    for (int l = 0; l < 2; l++) {
        DataTable *table = FileManager_FileExists(lists[l][0]) ? DataTable_Load(lists[l][0]) : NULL;
        for (int r = 0; table != NULL && r < DataTable_GetRowCount(table); r++) {
            for (int f = 1; f <= 32; f++) {
                char column[16];
                snprintf(column, sizeof(column), lists[l][1], f);
                const int c = DataTable_GetColumnIndex(table, column);
                if (c < 0) {
                    break;
                }
                const char *file = DataTable_GetString(table, r, c);
                char        path[256];
                snprintf(path, sizeof(path), lists[l][2], file);
                if (file[0] == '\0' || strcmp(file, "0") == 0 || !FileManager_FileExists(path)) {
                    continue;
                }
                char check[4096];
                snprintf(check, sizeof(check), "%s%s", folder, path);
                for (char *ch = check + strlen(folder); *ch != '\0'; ch++) {
                    *ch = *ch == '\\' ? '/' : (char)tolower((unsigned char)*ch);
                }
                FILE *there = fopen(check, "rb");
                if (there != NULL) {
                    fclose(there); // (the file list had it)
                    continue;
                }
                Main__ExtractOne(folder, path, small) ? saved++ : lost++;
            }
        }
        DataTable_Destroy(&table);
    }
    printf("%d files written into %s", saved, folder);
    printf(lost > 0 ? " (%d could not be read or written)\n" : "\n", lost);
    FileManager_DestroySingleton();
    Log_CloseFile();
    return saved > 0 ? 0 : 1;
}

static volatile sig_atomic_t server_running = 1;
static void                  Main__StopServer(int signal_number) {
    (void)signal_number;
    server_running = 0;
}

// The dedicated server's console: lines typed on it, read by a thread of their own (reading waits), handed to the
// server's loop
static SDL_mutex   *console_lock = NULL;
static char         console_line[512];
static volatile int console_ready = 0;

static int Main__ConsoleThread(void *data) {
    (void)data;
    char line[512];
    while (server_running && fgets(line, sizeof(line), stdin) != NULL) {
        for (int waited = 0; console_ready && server_running && waited < 2000; waited++) {
            SDL_Delay(5); // (the last one not taken yet)
        }
        SDL_LockMutex(console_lock);
        snprintf(console_line, sizeof(console_line), "%s", line);
        console_ready = 1;
        SDL_UnlockMutex(console_lock);
    }
    return 0;
}

// --server: the game server on its own (characters are kept here), until Ctrl+C
static int Main__DedicatedServer(const int port) {
    NetSettings settings;
    NetServer_LoadSettings(&settings);
    if (port > 0) {
        settings.port = (uint16_t)port;
    }
    char       error[160];
    NetServer *server = NetServer_Start(&settings, true, error, sizeof(error));
    if (server == NULL) {
        LOG_ERROR("Server: %s", error);
        printf("Server: %s\n", error);
        return 1;
    }
    printf("Abyss Engine dedicated server \"%s\" on port %u (settings: %s/server.ini). Ctrl+C or quit to stop;\n"
           "type help for the commands (players, kick, ban, save, say).\n",
           settings.name, (unsigned)settings.port, NetServer_Folder());
    fflush(stdout);
    signal(SIGINT, Main__StopServer);
    signal(SIGTERM, Main__StopServer);
    console_lock        = SDL_CreateMutex();
    SDL_Thread *console = console_lock != NULL ? SDL_CreateThread(Main__ConsoleThread, "console", NULL) : NULL;
    if (console != NULL) {
        SDL_DetachThread(console); // (it may be waiting for a line when the server stops)
    }
    while (server_running) {
        NetServer_Update(server, SDL_GetTicks64());
        if (console_ready) {
            char line[512];
            SDL_LockMutex(console_lock);
            snprintf(line, sizeof(line), "%s", console_line);
            console_ready = 0;
            SDL_UnlockMutex(console_lock);
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
                server_running = 0;
            } else {
                char reply[4096];
                NetServer_Command(server, line, reply, sizeof(reply));
                if (reply[0] != '\0') {
                    printf("%s\n", reply);
                    fflush(stdout);
                }
            }
        }
        SDL_Delay(5);
    }
    NetServer_Stop(&server);
    printf("Server stopped.\n");
    return 0;
}

// --export-controls: the controls for reading outside the game, without opening a window
static int Main__ExportControls(const char *folder, const bool defaults) {
    if (defaults) {
        Controls_Reset();
    }
    static const char *pictures[4] = {"controls-keyboard.svg", "controls-keyboard-dark.svg", "controls-gamepad.svg",
                                      "controls-gamepad-dark.svg"};
    char               path[4200];
    bool               ok = true;
    for (int n = 0; n < 4; n++) {
        snprintf(path, sizeof(path), "%s/%s", folder, pictures[n]);
        ok = ok && (n < 2 ? ControlsExport_Svg(0, path, n == 1) : ControlsExport_PadSvg(0, path, n == 3));
    }
    snprintf(path, sizeof(path), "%s/controls.md", folder);
    ok = ok && ControlsExport_Markdown(0, path, true);
    static const struct {
        const char *file;
        unsigned    context;
    } modes[2] = {
        {"keys-single-player.md", CONTROL_CONTEXT_GAME },
        {"keys-free-roam.md",     CONTROL_CONTEXT_DEBUG}
    };
    for (int m = 0; m < 2 && ok; m++) {
        snprintf(path, sizeof(path), "%s/%s", folder, modes[m].file);
        FILE *file = fopen(path, "w");
        ok         = file != NULL;
        if (file != NULL) {
            ControlsExport_ModeTable(file, 0, modes[m].context);
            fclose(file);
        }
    }
    printf(ok ? "Wrote the controls to %s\n" : "Could not write the controls to %s\n", folder);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    bool        settings_only  = false;
    bool        editor_only    = false;
    const char *editor_map     = NULL;
    const char *export_folder  = NULL;
    bool        export_default = false;
    bool        dedicated      = false;
    int         server_port    = 0;
    bool        text_mode      = false;
    const char *extract_folder = NULL;
    bool        extract_small  = false;
    const char *text_name      = NULL;
    int         text_class     = CHARACTER_BARBARIAN;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--settings") == 0) {
            settings_only = true;
        } else if (strcmp(argv[a], "--editor") == 0) {
            editor_only = true;
            if (a + 1 < argc && argv[a + 1][0] != '-') {
                editor_map = argv[++a];
            }
        } else if (strcmp(argv[a], "--export-controls") == 0 && a + 1 < argc) {
            export_folder = argv[++a];
        } else if (strcmp(argv[a], "--server") == 0) {
            dedicated = true;
        } else if (strcmp(argv[a], "--port") == 0 && a + 1 < argc) {
            server_port = atoi(argv[++a]);
        } else if (strcmp(argv[a], "--extract") == 0 && a + 1 < argc) {
            extract_folder = argv[++a];
        } else if (strcmp(argv[a], "--small") == 0) {
            extract_small = true; // (--extract: the DT1s without their pictures)
        } else if (strcmp(argv[a], "--text") == 0) {
            text_mode = true;
            if (a + 1 < argc && argv[a + 1][0] != '-') {
                text_name = argv[++a];
            }
        } else if (strcmp(argv[a], "--class") == 0 && a + 1 < argc) {
            text_class = atoi(argv[++a]);
        } else if (strcmp(argv[a], "--defaults") == 0) {
            export_default = true;
        } else if (strcmp(argv[a], "--help") == 0 || strcmp(argv[a], "-h") == 0) {
            Main__Usage();
            return 0;
        }
    }

    Log_SetLevel(LOG_LEVEL_EVERYTHING);
    if (text_mode) {
        // No window and no sound: the game runs as usual, drawn into nothing, and is shown as text
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        Log_SetConsole(false); // the log file keeps the lines
    }
    if (extract_folder != NULL) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        Log_SetConsole(false); // just the count at the end
    }
    av_log_set_level(AV_LOG_FATAL);
    LOG_INFO("Abyss Engine");

#ifdef _WIN32
    if (dedicated) {
        // A window program on Windows: show the server's output in the terminal it was started from
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    LOG_DEBUG("Initializing SDL...");
    if (SDL_Init(dedicated ? SDL_INIT_TIMER : SDL_INIT_EVERYTHING) != 0) { // a server needs no screen or sound
        LOG_FATAL(SDL_GetError());
    }

    LOG_DEBUG("Initializing crypto...");
    crypto_init();

    LOG_DEBUG("Loading configuration...");
    char config_path[4096];
    memset(config_path, 0, 4096);

    // Prefer an abyss.ini next to the executable (portable setup)
    char *exe_dir = SDL_GetBasePath();
    if (exe_dir != NULL) {
        snprintf(config_path, 4096, "%sabyss.ini", exe_dir);
        SDL_free(exe_dir);
        FILE *local_config = fopen(config_path, "r");
        if (local_config != NULL) {
            fclose(local_config);
        } else {
            memset(config_path, 0, 4096);
        }
    }

    // Otherwise fall back to the per-user settings folder
    if (config_path[0] == '\0') {
#ifdef _WIN32
        snprintf(config_path, 4096, "%s\\abyss\\abyss.ini", getenv("APPDATA"));
#elif __APPLE__
        snprintf(config_path, 4096, "%s/Library/Application Support/abyss/abyss.ini", getenv("HOME"));
#else
        snprintf(config_path, 4096, "%s/.config/abyss/abyss.ini", getenv("HOME"));
#endif
    }
    LOG_INFO("Using configuration file '%s'", config_path);
    AbyssConfiguration_LoadSingleton(config_path);

    // Logs next to abyss.ini: logs/abyss.log (the run before: abyss.previous.log), and crash reports
    {
        char logs[4096];
        char log_path[4200];
        char previous[4200];
        snprintf(logs, sizeof(logs), "%s/logs", AbyssConfiguration_GetConfigFolder());
#ifdef _WIN32
        _mkdir(logs);
#else
        mkdir(logs, 0755);
#endif
        snprintf(log_path, sizeof(log_path), "%s/abyss.log", logs);
        snprintf(previous, sizeof(previous), "%s/abyss.previous.log", logs);
        remove(previous);
        rename(log_path, previous);
        Log_OpenFile(log_path);
        CrashHandler_Install(logs);
        LOG_INFO("%s", Revision_GetFullVersion());
        LOG_INFO("Configuration: %s", config_path);
    }
    Settings_Load(); // gameplay and controls (options.ini)
    if (export_folder != NULL) {
        return Main__ExportControls(export_folder, export_default);
    }

    LOG_DEBUG("Initializing file manager...");
    if (extract_folder == NULL) {
        Mods_Load(); // (before the files: a mod's come first; --extract copies the game's own)
    }
    FileManager_CreateSingleton();
    FileManager_SetLenient(text_mode); // (text mode can run from an extracted folder: no pictures needed)

    if (extract_folder != NULL) {
        return Main__Extract(extract_folder, extract_small);
    }

    LOG_DEBUG("Loading game strings...");
    StringTable_InitializeSingleton("eng");

    if (dedicated) {
        return Main__DedicatedServer(server_port); // the game's data tables, no window
    }

    LOG_DEBUG("Creating window...");
    // The game is drawn at 800x600 and scaled to the window, keeping its 4:3 shape (black bars on wide screens).
    int      window_width;
    int      window_height;
    uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    AbyssConfiguration_GetWindowSize(&window_width, &window_height);

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && (window_width >= usable.w || window_height >= usable.h)) {
        // As big as (or bigger than) the screen: open maximized instead of off-screen
        window_width   = window_width < usable.w ? window_width : usable.w;
        window_height  = window_height < usable.h ? window_height : usable.h;
        window_flags  |= SDL_WINDOW_MAXIMIZED;
    }

    sdl_window = SDL_CreateWindow("Abyss Engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width,
                                  window_height, window_flags);

    if (sdl_window == NULL) {
        LOG_FATAL(SDL_GetError());
    }

    LOG_DEBUG("Creating renderer...");
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
                                      text_mode ? SDL_RENDERER_SOFTWARE // no screen: drawn into memory
                                                : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (sdl_renderer == NULL) {
        LOG_FATAL(SDL_GetError());
    }

    SDL_RenderSetLogicalSize(sdl_renderer, 800, 600);
    SDL_ShowCursor(false);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, AbyssConfiguration_GetScaleQuality());

    if (AbyssConfiguration_GetFullScreen()) {
        SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    Palette_Initialize();
    Cursor_CreateSingleton();
    Scene_InitializeManager();
    Cursor_SetType(CURSOR_STANDARD);
    if (text_mode) {
        if (!Main__TextCharacter(text_name, text_class)) {
            return 1;
        }
    } else if (editor_only) {
        MapEditor_SetStandalone(editor_map); // AbyssEngine --editor [map]
        Scene_Set(SCENE_REF(MapEditor));
    } else if (settings_only) {
        options_standalone = true;
        Scene_Set(SCENE_REF(Options)); // AbyssEngine --settings
    } else {
        Scene_Set(SCENE_REF(IntroVideos));
    }
    Label_InitializeCaches();
    VideoManager_InitializeSingleton(); // before the audio starts: its thread asks whether a video is playing
    InputManager_InitializeSingleton();
    AudioManager_InitSingleton();

    Picture_Initialize();

    SDL_Event sdl_event;
    running             = true;
    uint64_t last_ticks = SDL_GetTicks64();
    // The frame limiter: each frame waits out the rest of its share of a second (the game itself always ran at 25)
    const uint64_t counter_rate = SDL_GetPerformanceFrequency();
    uint64_t       frame_start  = SDL_GetPerformanceCounter();
    int            vsync_rate   = -1;
    uint64_t       text_drawn   = 0;
    if (text_mode && !Terminal_Begin()) {
        fprintf(stderr, "Text mode needs a terminal.\n");
        return 1;
    }

    while (running) {
        while (SDL_PollEvent(&sdl_event)) {
            if (InputManager_ProcessSdlEvent(&sdl_event)) {
                continue;
            }

            switch (sdl_event.type) {
            case SDL_QUIT:
                running = false;
                break;
            default:
                break;
            }
        }

        if (!running) {
            break;
        }

        const uint64_t current_ticks = SDL_GetTicks64();
        const uint64_t tick_delta    = current_ticks - last_ticks;

        if (tick_delta == 0) {
            SDL_Delay(1);
            continue;
        }

        last_ticks = current_ticks;

        if (text_mode && !Main__TextFrame(current_ticks, &text_drawn)) {
            break; // the game was left
        }

        Multiplayer_Update(); // the hosted server and our connection, before the scene reads them
        VideoManager_IsPlayingVideo() ? VideoManager_Update(tick_delta) : Scene_UpdateCurrentScene(tick_delta);
        AudioManager_Update();

        Picture_BeginFrame();
        SDL_RenderClear(sdl_renderer);
        if (VideoManager_IsPlayingVideo()) {
            VideoManager_Render();
        } else {
            Scene_RenderCurrentScene();
            Cursor_Draw();
        }
        Picture_EndFrame();
        SDL_RenderPresent(sdl_renderer);

        const int rate = AbyssConfiguration_GetFrameRate(AbyssConfiguration_IsOnline());
        if (rate != vsync_rate) { // the screen's own refresh would cap "Unlimited", so it only syncs with a limit
            SDL_RenderSetVSync(sdl_renderer, rate == 0 ? 0 : 1);
            vsync_rate = rate;
        }
        if (rate > 0) {
            const uint64_t frame_length = counter_rate / (uint64_t)rate;
            const uint64_t spent        = SDL_GetPerformanceCounter() - frame_start;
            if (spent < frame_length) {
                const uint32_t wait_ms = (uint32_t)((frame_length - spent) * 1000 / counter_rate);
                if (wait_ms > 0) {
                    SDL_Delay(wait_ms);
                }
                while (SDL_GetPerformanceCounter() - frame_start < frame_length) {
                    // the last part of a millisecond
                }
            }
        }
        frame_start = SDL_GetPerformanceCounter();
    }

    if (text_mode) {
        Terminal_End();
    }
    Multiplayer_Leave();
    Picture_Finalize();
    AudioManager_DestroySingleton(); // Destroy this first as it reaches into things via threads
    VideoManager_DestroySingleton();
    InputManager_DestroySingleton();
    Scene_DestroyManager();
    LoadingScreen_Release();
    Label_FinalizeCaches();
    MapSession_Release();
    LevelTypes_Release();
    Sounds_Release();
    Cursor_Destroy();
    Palette_Finalize();
    AbyssConfiguration_DestroySingleton();
    StringTable_DestroySingleton();
    FileManager_DestroySingleton();
    Log_CloseFile();

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}
