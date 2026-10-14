#ifndef ABYSS_CRASH_HANDLER_H
#define ABYSS_CRASH_HANDLER_H

// When the engine crashes, write what's known into the logs folder: crash-<date>-<time>.txt with the error, where it
// happened (the call stack, with function names and lines where the build has them) and the last log lines; on
// Windows also a .dmp minidump to open in Visual Studio. Works in every build (Release, RelWithDebInfo, Debug); with
// a debugger attached, the debugger gets the crash first.
void CrashHandler_Install(const char *logs_folder);

#endif // ABYSS_CRASH_HANDLER_H
