// Included by SceneRoam.c: the message log (M), the help screen (H), clearing the messages (N).
//
// The log keeps the last LOG_LINES things said to you (the message line and, in multiplayer, the chat), oldest at
// the top. The help screen lists every control that works with its first binding, like the game's.

// What was said: kept for the log
static void Roam__LogAdd(SceneRoam *roam, const char *text, const bool chat) {
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int at = roam->log_count < LOG_LINES ? roam->log_count : LOG_LINES - 1;
    if (roam->log_count >= LOG_LINES) {
        memmove(roam->log_lines[0], roam->log_lines[1], sizeof(roam->log_lines[0]) * (LOG_LINES - 1));
        memmove(&roam->log_chat[0], &roam->log_chat[1], sizeof(roam->log_chat[0]) * (LOG_LINES - 1));
    } else {
        roam->log_count++;
    }
    snprintf(roam->log_lines[at], sizeof(roam->log_lines[at]), "%s", text);
    roam->log_chat[at] = chat;
}

// The log: a dark box on the left, the newest at the bottom
static void Roam__DrawLog(SceneRoam *roam) {
    if (!roam->log_open || roam->log_label == NULL) {
        return;
    }
    const SDL_Rect box = {8, 60, 420, 330};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(sdl_renderer, &box);
    SDL_SetRenderDrawColor(sdl_renderer, 0x80, 0x70, 0x48, 0xFF);
    SDL_RenderDrawRect(sdl_renderer, &box);
    Label_SetColor(roam->log_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_SetText(roam->log_label, "Message Log");
    Label_Draw(roam->log_label, box.x + 10, box.y + 12);
    const int rows = (box.h - 34) / 15;
    const int from = roam->log_count > rows ? roam->log_count - rows : 0;
    int       y    = box.y + 32;
    if (roam->log_count == 0) {
        Label_SetColor(roam->log_label, 0x90, 0x90, 0x90, 0xFF);
        Label_SetText(roam->log_label, "Nothing yet.");
        Label_Draw(roam->log_label, box.x + 10, y);
    }
    for (int l = from; l < roam->log_count; l++) {
        if (roam->log_chat[l]) {
            Label_SetColor(roam->log_label, 0xFF, 0xFF, 0xFF, 0xFF);
        } else {
            Label_SetColor(roam->log_label, 0xFF, 0xE8, 0x60, 0xFF);
        }
        Label_SetText(roam->log_label, roam->log_lines[l]);
        Label_Draw(roam->log_label, box.x + 10, y);
        y += 15;
    }
}

// The help screen: every control that works and its first key, in two columns
static void Roam__DrawHelp(SceneRoam *roam) {
    if (!roam->help_open || roam->log_label == NULL) {
        return;
    }
    const SDL_Rect box = {40, 30, 720, 460};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 215);
    SDL_RenderFillRect(sdl_renderer, &box);
    SDL_SetRenderDrawColor(sdl_renderer, 0x80, 0x70, 0x48, 0xFF);
    SDL_RenderDrawRect(sdl_renderer, &box);
    Label_SetColor(roam->log_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_SetText(roam->log_label, "Controls (H closes; Options > Controls changes them)");
    Label_Draw(roam->log_label, box.x + 14, box.y + 14);
    const int profile = Roam__Profile(roam);
    const int rows    = (box.h - 40) / 15;
    int       shown   = 0;
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ControlBindings *list = Controls_GetBindings(profile, (ControlAction)a);
        if (!Controls_ActionWorks((ControlAction)a) || list->count == 0) {
            continue;
        }
        const int column = shown / rows;
        if (column > 1) {
            break;
        }
        const int x = box.x + 14 + (column * 355);
        const int y = box.y + 36 + ((shown % rows) * 15);
        char      key[64];
        Controls_InputName(&list->inputs[0], key, sizeof(key));
        Label_SetColor(roam->log_label, 0xFF, 0xE8, 0x60, 0xFF);
        Label_SetText(roam->log_label, key);
        Label_Draw(roam->log_label, x, y);
        Label_SetColor(roam->log_label, 0xE0, 0xE0, 0xE0, 0xFF);
        Label_SetText(roam->log_label, Controls_ActionName((ControlAction)a));
        Label_Draw(roam->log_label, x + 150, y);
        shown++;
    }
}

// The screen keys: message log, help, clear messages, show portraits, the hireling's screen
static void Roam__ScreenKeys(SceneRoam *roam) {
    if (Controls_Pressed(ACTION_MESSAGE_LOG)) {
        roam->log_open  = !roam->log_open;
        roam->help_open = false;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    if (Controls_Pressed(ACTION_HELP)) {
        roam->help_open = !roam->help_open;
        roam->log_open  = false;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    if (Controls_Pressed(ACTION_CLEAR_MESSAGES)) {
        memset(roam->chat_lines, 0, sizeof(roam->chat_lines)); // (the log keeps them)
        roam->message_until = 0;
    }
    if (Controls_Pressed(ACTION_SHOW_PORTRAITS)) {
        roam->hide_portraits = !roam->hide_portraits;
    }
    if (Controls_Pressed(ACTION_CLEAR_SCREEN)) {
        roam->log_open  = false;
        roam->help_open = false;
    }
}
