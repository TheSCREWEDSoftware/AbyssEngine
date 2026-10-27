#include "Logging.h"
#include "Globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <Windows.h>
#endif // _WIN32

#define MAX_LOG_LINE_LENGTH 4096
enum LogLevel log_level = LOG_LEVEL_ERROR;

static const char *log_level_strings[] = {"", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};

// The log file (logs/abyss.log next to abyss.ini) and the last lines, for crash reports
#define RECENT_LINES     40
#define RECENT_LINE_SIZE 256
static FILE *log_file = NULL;
static char  recent[RECENT_LINES][RECENT_LINE_SIZE];
static int   recent_next  = 0;
static int   recent_count = 0;

void Log_SetLevel(const enum LogLevel level) { log_level = level; }

void Log_OpenFile(const char *path) {
    if (log_file != NULL) {
        fclose(log_file);
    }
    log_file = fopen(path, "w");
}

void Log_CloseFile(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

int Log_GetRecentCount(void) { return recent_count; }

const char *Log_GetRecent(const int index) {
    if (index < 0 || index >= recent_count) {
        return "";
    }
    return recent[(recent_next - recent_count + index + RECENT_LINES) % RECENT_LINES];
}

static void Log__Keep(const char *prefix, const char *text) {
    snprintf(recent[recent_next], RECENT_LINE_SIZE, "%s%s", prefix, text);
    recent_next  = (recent_next + 1) % RECENT_LINES;
    recent_count = recent_count < RECENT_LINES ? recent_count + 1 : RECENT_LINES;
    if (log_file != NULL) {
        fprintf(log_file, "%s%s\n", prefix, text);
        fflush(log_file); // a crash right after still leaves the line in the file
    }
}

static const char *Log__FileName(const char *file) {
    const char *slash     = strrchr(file, '/');
    const char *backslash = strrchr(file, '\\');
    const char *last      = slash == NULL ? backslash : (backslash == NULL || slash > backslash ? slash : backslash);
    return last != NULL ? last + 1 : file;
}

static bool log_console = true;

void Log_SetConsole(const bool on) { log_console = on; }

void Log_Message(enum LogLevel level, const char *file, const int line, const char *format, ...) {
    char    msg[MAX_LOG_LINE_LENGTH];
    va_list args;

    if (level < log_level) {
        return;
    }
    if (!log_console && level < LOG_LEVEL_FATAL) {
        // Text mode: the terminal is the game's; the line goes to the log file only
        char    text[MAX_LOG_LINE_LENGTH];
        char    prefix[160];
        va_list copy;
        va_start(copy, format);
        vsnprintf(text, sizeof(text), format, copy);
        va_end(copy);
        snprintf(prefix, sizeof(prefix), "[%s] %s:%i - ", log_level_strings[level], Log__FileName(file), line);
        Log__Keep(prefix, text);
        return;
    }

#ifdef _WIN32
    printf("[%s] %s:%i - ", log_level_strings[level], Log__FileName(file), line);
    if (IsDebuggerPresent()) {
        memset(msg, 0, sizeof(char) * MAX_LOG_LINE_LENGTH);
        snprintf(msg, MAX_LOG_LINE_LENGTH, "%s(%i): [%s] ", file, line, log_level_strings[level]);
        va_start(args, format);
        // The message goes after the prefix, in what's left of the buffer (keeping room for the newline)
        const size_t used = strlen(msg);
        vsnprintf(msg + used, MAX_LOG_LINE_LENGTH - used - 1, format, args);
        va_end(args);
        strcat(msg, "\n");
        OutputDebugString(msg);
    }
#else
    printf("[%s] %s:%i - ", log_level_strings[level], Log__FileName(file), line);
#endif
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");

    {
        char    text[MAX_LOG_LINE_LENGTH];
        char    prefix[160];
        va_list copy;
        va_start(copy, format);
        vsnprintf(text, sizeof(text), format, copy);
        va_end(copy);
        snprintf(prefix, sizeof(prefix), "[%s] %s:%i - ", log_level_strings[level], Log__FileName(file), line);
        Log__Keep(prefix, text);
    }

    if (level == LOG_LEVEL_FATAL) {
        memset(msg, 0, sizeof(char) * MAX_LOG_LINE_LENGTH);
        va_start(args, format);
        vsnprintf(msg, MAX_LOG_LINE_LENGTH, format, args);
        va_end(args);

        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", msg, sdl_window);
        exit(-1);
    }
}
