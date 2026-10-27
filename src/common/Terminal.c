#include "Terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

static DWORD saved_mode = 0;
static bool  started    = false;

bool Terminal_Begin(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || !GetConsoleMode(out, &saved_mode)) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
            return false;
        }
        freopen("CONOUT$", "w", stdout);
        freopen("CONIN$", "r", stdin);
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(out, &saved_mode);
    }
    SetConsoleMode(out, saved_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING); // the colour codes
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
    fflush(stdout);
    started = true;
    return true;
}

void Terminal_End(void) {
    if (!started) {
        return;
    }
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), saved_mode);
    started = false;
}

int Terminal_ReadKey(void) {
    if (!_kbhit()) {
        return -1;
    }
    const int key = _getch();
    if (key == 0 || key == 0xE0) { // an arrow
        switch (_getch()) {
        case 72:
            return 'w';
        case 80:
            return 's';
        case 75:
            return 'a';
        case 77:
            return 'd';
        default:
            return -1;
        }
    }
    return key;
}

void Terminal_Size(int *cols, int *rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    *cols = 80;
    *rows = 25;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        *cols = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
}

#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;
static bool           started = false;

static void Terminal__AtExit(void) { Terminal_End(); }

bool Terminal_Begin(void) {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved) != 0) {
        return false;
    }
    struct termios raw  = saved;
    raw.c_lflag        &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_cc[VMIN]      = 0;
    raw.c_cc[VTIME]     = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout); // its own screen, no cursor
    fflush(stdout);
    started = true;
    atexit(Terminal__AtExit);
    return true;
}

void Terminal_End(void) {
    if (!started) {
        return;
    }
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    started = false;
}

int Terminal_ReadKey(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }
    if (c == 0x1B) { // an arrow: ESC [ A..D
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[') {
            switch (seq[1]) {
            case 'A':
                return 'w';
            case 'B':
                return 's';
            case 'C':
                return 'd';
            case 'D':
                return 'a';
            default:
                return -1;
            }
        }
        return 0x1B;
    }
    return c;
}

void Terminal_Size(int *cols, int *rows) {
    struct winsize size;
    *cols = 80;
    *rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        *cols = size.ws_col;
        *rows = size.ws_row;
    }
}
#endif

void Terminal_Write(const char *text, const int length) {
    fwrite(text, 1, (size_t)length, stdout);
    fflush(stdout);
}
