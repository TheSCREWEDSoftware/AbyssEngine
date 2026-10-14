#include "CrashHandler.h"
#include "Logging.h"
#include "Revision.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static char crash_folder[1024] = ".";

static void CrashHandler__Name(char *path, const size_t size, const char *extension) {
    const time_t now = time(NULL);
    struct tm    local;
#ifdef _WIN32
    localtime_s(&local, &now);
    snprintf(path, size, "%s\\crash-%04d-%02d-%02d_%02d-%02d-%02d.%s", crash_folder, local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, extension);
#else
    localtime_r(&now, &local);
    snprintf(path, size, "%s/crash-%04d-%02d-%02d_%02d-%02d-%02d.%s", crash_folder, local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, extension);
#endif
}

static void CrashHandler__RecentLog(FILE *file) {
    fprintf(file, "\nLast log lines:\n");
    for (int i = 0; i < Log_GetRecentCount(); i++) {
        fprintf(file, "  %s\n", Log_GetRecent(i));
    }
}

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif

static LONG WINAPI CrashHandler__Filter(EXCEPTION_POINTERS *info) {
    char report[1200];
    char dump[1200];
    CrashHandler__Name(report, sizeof(report), "txt");
    CrashHandler__Name(dump, sizeof(dump), "dmp");
    const HANDLE process = GetCurrentProcess();

    FILE *file = fopen(report, "w");
    if (file != NULL) {
        fprintf(file, "Abyss Engine crashed\n%s\n\nException 0x%08lX at %p (thread %lu)\n", Revision_GetFullVersion(),
                info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 2) {
            fprintf(file, "Tried to %s address 0x%llx\n",
                    info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "read",
                    (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
        }

        // The call stack
        fprintf(file, "\nCall stack:\n");
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, NULL, TRUE);
        CONTEXT      context = *info->ContextRecord;
        STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));
#if defined(_M_X64)
        const DWORD machine    = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
        const DWORD machine    = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset    = context.Pc;
        frame.AddrFrame.Offset = context.Fp;
        frame.AddrStack.Offset = context.Sp;
#else
        const DWORD machine    = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset    = context.Eip;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrStack.Offset = context.Esp;
#endif
        frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int depth = 0; depth < 64; depth++) {
            if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context, NULL, SymFunctionTableAccess64,
                             SymGetModuleBase64, NULL) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            const DWORD64 address = frame.AddrPC.Offset;
            char          buffer[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO  *symbol = (SYMBOL_INFO *)buffer;
            memset(buffer, 0, sizeof(buffer));
            symbol->SizeOfStruct         = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen           = 255;
            DWORD64         displacement = 0;
            DWORD           line_offset  = 0;
            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            const char *name  = SymFromAddr(process, address, &displacement, symbol) ? symbol->Name : "?";
            if (SymGetLineFromAddr64(process, address, &line_offset, &line)) {
                fprintf(file, "  %s  (%s:%lu)\n", name, line.FileName, line.LineNumber);
            } else {
                fprintf(file, "  %s  (0x%llx)\n", name, (unsigned long long)address);
            }
        }
        CrashHandler__RecentLog(file);
        fprintf(file, "\nA minidump is next to this file: %s (open it in Visual Studio with the .pdb).\n", dump);
        fclose(file);
    }

    // A minidump, for the debugger
    const HANDLE dump_file = CreateFileA(dump, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (dump_file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception;
        exception.ThreadId          = GetCurrentThreadId();
        exception.ExceptionPointers = info;
        exception.ClientPointers    = FALSE;
        MiniDumpWriteDump(process, GetCurrentProcessId(), dump_file,
                          (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory), &exception,
                          NULL, NULL);
        CloseHandle(dump_file);
    }

    char message[1400];
    snprintf(message, sizeof(message), "Abyss Engine crashed. A report was saved to:\n%s", report);
    MessageBoxA(NULL, message, "Abyss Engine", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler_Install(const char *logs_folder) {
    snprintf(crash_folder, sizeof(crash_folder), "%s", logs_folder);
    SetUnhandledExceptionFilter(CrashHandler__Filter);
}

#else // Linux, macOS
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static const int crash_signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};

static void CrashHandler__Signal(const int signal_number, siginfo_t *info, void *context) {
    (void)context;
    char report[1200];
    CrashHandler__Name(report, sizeof(report), "txt");
    FILE *file = fopen(report, "w");
    if (file != NULL) {
        fprintf(file, "Abyss Engine crashed\n%s\n\nSignal %d (%s) at address %p\n", Revision_GetFullVersion(),
                signal_number, strsignal(signal_number), info != NULL ? info->si_addr : NULL);
        fprintf(file,
                "\nCall stack (for a line like AbyssEngine(+0x1234), the file and line: addr2line -f -e AbyssEngine "
                "0x1234):\n");
        fflush(file);
        void     *frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, fileno(file));
        CrashHandler__RecentLog(file);
        fclose(file);
        fprintf(stderr, "Abyss Engine crashed: a report was saved to %s\n", report);
    }
    // Then crash as usual (a core dump, if they're on: ulimit -c unlimited)
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

void CrashHandler_Install(const char *logs_folder) {
    snprintf(crash_folder, sizeof(crash_folder), "%s", logs_folder);
    // Its own stack, so a stack overflow can still be reported
    static char alternate_stack[64 * 1024];
    stack_t     stack;
    stack.ss_sp    = alternate_stack;
    stack.ss_size  = sizeof(alternate_stack);
    stack.ss_flags = 0;
    sigaltstack(&stack, NULL);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = CrashHandler__Signal;
    action.sa_flags     = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (size_t s = 0; s < sizeof(crash_signals) / sizeof(crash_signals[0]); s++) {
        sigaction(crash_signals[s], &action, NULL);
    }
}
#endif
