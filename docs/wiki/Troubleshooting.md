# Troubleshooting

## Building

### vcpkg fails while installing a package (e.g. "python" errors)

vcpkg builds FFmpeg and other libraries from source the first time, and some of them need Python and other tools that
vcpkg downloads itself. If that fails:

- Make sure the build path has no spaces and isn't very long.
- Update vcpkg (`git pull` in the vcpkg folder, then `bootstrap-vcpkg.bat`), or use the vcpkg that comes with Visual
  Studio 2022 (see [Requirements](01-Requirements.md)).
- Delete `build\vcpkg_installed` and configure again.

### "Building Custom Rule ... CMakeLists.txt" then errors after pulling

The CMake files changed. Visual Studio re-runs CMake by itself; if it still fails, close Visual Studio, run
`Open CMake GUI.bat` again (or delete `build\CMakeCache.txt`) and reconfigure.

### CMake configured without vcpkg ("Could not find SDL2" etc.)

Configure with the vcpkg toolchain: `Open CMake GUI.bat` does this, and also removes an old cache that was made without
vcpkg.

## Running

### The window opens and closes immediately

`abyss.ini` wasn't found. Put it next to `AbyssEngine.exe` or in `%APPDATA%\abyss\` (see
[Game Files and Configuration](04-Game-Files-and-Configuration.md)). The log says which file it looked for.

### Black screen / no main menu / no pictures

The MPQs weren't loaded:

- `BasePath` must point at the folder with the MPQs (`C:\Games\Diablo II`; a slash at the end is fine too, and so are
  quotes around it). With no MPQs found at all, the game says so and names the `abyss.ini` it read.
- Check the names in `[MPQs]`. On Windows case doesn't matter; on Linux the engine also finds `D2DATA.MPQ` when you wrote
  `d2data.mpq`.
- The log lists every MPQ it loads.

### Text is missing

`Locale` must be `latin`.

### Linux: the game's files aren't found

Fixed: MPQ names are now matched without caring about upper/lower case
([issue #14](https://github.com/AbyssEngine/AbyssEngine/issues/14)).

### The first time I open Free Roam / the bestiary it takes a few seconds

It loads the graphics of every monster and NPC once. A loading screen shows progress; later visits are quicker (and the
bar is accurate from the second time).

### No sound effects (music plays)

Sound effects come from `d2sfx.mpq`: check it's in the game folder and, if your `abyss.ini` has an `[MPQs]` list, that
it's in it. The volume is set with `SfxVolume` (and `UIVolume` for button clicks) in `[Audio]`, 0.0 to 1.0.

## Reporting a problem

Include:

1. The version text from the top right of the main menu
2. Your platform and how you built it (Visual Studio configuration, or the cmake command)
3. The log output (console, or Visual Studio's *Output* window)

## Logs and crash reports

Every run writes a log to **`logs/abyss.log`** next to `abyss.ini` (the run before is kept as
`logs/abyss.previous.log`). If the engine crashes, it also writes **`logs/crash-<date>_<time>.txt`** with the error,
where it happened (the call stack) and the last lines of the log, in every build (Release too):

- **Windows**: the call stack has function names, files and lines when the `.pdb` is next to `AbyssEngine.exe`
  (Debug and RelWithDebInfo builds). A **`.dmp`** minidump is written next to the report: open it in Visual Studio
  (with the `.pdb`) to see the crash as if it had happened in the debugger. A message box says where the report is.
- **Linux / macOS**: the call stack lists `AbyssEngine(+0x1234)` style addresses; `addr2line -f -e AbyssEngine 0x1234`
  turns one into a function, file and line (for builds with debug information). The engine then crashes as usual,
  so a core dump is written too if they're on (`ulimit -c unlimited`), for `gdb AbyssEngine core`.

Please attach the crash report (and `abyss.log`) when you report a crash.
