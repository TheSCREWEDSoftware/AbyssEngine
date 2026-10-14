#include "audio/Sounds.h"
#include "common/AbyssConfiguration.h"
#include "common/CrashHandler.h"
#include "common/FileManager.h"
#include "common/Globals.h"
#include "common/Logging.h"
#include "common/Revision.h"
#include "data/StringTable.h"
#include "drawing/Cursor.h"
#include "drawing/Label.h"
#include "drawing/LoadingScreen.h"
#include "drawing/Picture.h"
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
#include "scenes/SceneIntroVideos.h"
#include "scenes/SceneOptions.h"
#include "types/Palette.h"
#include "util/Crypto.h"
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
           "                                       in the server folder next to abyss.ini; Ctrl+C stops it)\n");
}

static volatile sig_atomic_t server_running = 1;
static void                  Main__StopServer(int signal_number) {
    (void)signal_number;
    server_running = 0;
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
    printf("Abyss Engine dedicated server \"%s\" on port %u (settings: %s/server.ini). Ctrl+C to stop.\n",
           settings.name, (unsigned)settings.port, NetServer_Folder());
    fflush(stdout);
    signal(SIGINT, Main__StopServer);
    signal(SIGTERM, Main__StopServer);
    while (server_running) {
        NetServer_Update(server, SDL_GetTicks64());
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
    const char *export_folder  = NULL;
    bool        export_default = false;
    bool        dedicated      = false;
    int         server_port    = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--settings") == 0) {
            settings_only = true;
        } else if (strcmp(argv[a], "--export-controls") == 0 && a + 1 < argc) {
            export_folder = argv[++a];
        } else if (strcmp(argv[a], "--server") == 0) {
            dedicated = true;
        } else if (strcmp(argv[a], "--port") == 0 && a + 1 < argc) {
            server_port = atoi(argv[++a]);
        } else if (strcmp(argv[a], "--defaults") == 0) {
            export_default = true;
        } else if (strcmp(argv[a], "--help") == 0 || strcmp(argv[a], "-h") == 0) {
            Main__Usage();
            return 0;
        }
    }

    Log_SetLevel(LOG_LEVEL_EVERYTHING);
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
    FileManager_CreateSingleton();

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
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

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
    if (settings_only) {
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
