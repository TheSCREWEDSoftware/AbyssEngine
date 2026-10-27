#ifndef ABYSS_LOG_H
#define ABYSS_LOG_H

#include <stdbool.h>
#include <stdarg.h>

#define LOG_DEBUG(...) Log_Message(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Log_Message(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Log_Message(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Log_Message(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)                                                                                                 \
    Log_Message(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__);                                                     \
    exit(-1)

#define FAIL_IF_NULL(ptr)                                                                                              \
    if ((ptr) == NULL) {                                                                                               \
        LOG_FATAL("Failed to allocate memory.");                                                                       \
    }

enum LogLevel {
    LOG_LEVEL_EVERYTHING,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
};

extern enum LogLevel log_level;

void Log_SetLevel(enum LogLevel level);
void Log_SetConsole(bool on); // off: lines go only to the log file (text mode keeps the terminal for the game)
// Also write every line to a file (and keep the last few for crash reports)
void        Log_OpenFile(const char *path);
void        Log_CloseFile(void);
int         Log_GetRecentCount(void);
const char *Log_GetRecent(int index); // oldest first
void        Log_Message(enum LogLevel level, const char *file, int line, const char *format, ...);

#endif // ABYSS_LOG_H
