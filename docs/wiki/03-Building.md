# 3. Building

**Installation guide:** [1. Requirements](01-Requirements.md) › [2. Getting the Source](02-Getting-the-Source.md) › **3. Building** › [4. Game Files](04-Game-Files-and-Configuration.md) › [5. First Run](05-First-Run.md)

Builds go in a `build` folder inside the source folder. Pick your platform:

- [Windows (Visual Studio)](#windows-visual-studio)
- [Linux (command line)](#linux)
- [macOS](#macos)
- [Meson instead of CMake](#meson)

## Windows (Visual Studio)

### The easy way: `Open CMake GUI.bat`

1. Double-click **`Open CMake GUI.bat`** in the source folder. It:
   - finds vcpkg (`VCPKG_ROOT`, `C:\vcpkg`, or the copy inside Visual Studio 2022),
   - configures the project for Visual Studio 2022 x64 in `build\` (the first run downloads and builds SDL2,
     FFmpeg, zlib and libarchive, which is slow, see [Requirements](01-Requirements.md)),
   - then opens **CMake GUI** on that build folder.
2. In CMake GUI, tick **Grouped** to see the **DEBUG** options (Skip Intro, Show Catalog, Show Map Editor; all on by
   default, see [CMake and Meson Options](CMake-Options.md)). Change what you like, then press **Configure** and
   **Generate**.
3. Press **Open Project** (or open `build\AbyssEngine.sln`).
4. In Visual Studio, pick a configuration in the toolbar: **RelWithDebInfo** is a good default (fast, still
   debuggable), **Debug** for stepping through code.
5. **Build > Build Solution** (`Ctrl+Shift+B`). `AbyssEngine` is the start-up project, so **F5** builds and runs it.

The program ends up in `build\<Configuration>\AbyssEngine.exe`, e.g. `build\RelWithDebInfo\AbyssEngine.exe`, next to
the DLLs it needs.

### Build or Rebuild?

- **Build** compiles only what changed since last time. Use this normally.
- **Rebuild** throws everything away and compiles all of it again. Only needed if something seems stuck.

### By hand (Developer PowerShell / command prompt)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config RelWithDebInfo
```

## Linux

With the packages from [Requirements](01-Requirements.md) installed:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # or leave out -G Ninja for Makefiles
cmake --build build
./build/AbyssEngine
```

Change options with `-D`, e.g. `-DDEBUG_SKIP_INTRO=OFF`, or interactively with `ccmake build` / `cmake-gui`.

## macOS

```bash
cmake -S . -B build -G Xcode        # or: -DCMAKE_BUILD_TYPE=RelWithDebInfo for Makefiles/Ninja
cmake --build build --config RelWithDebInfo
```

Run the `AbyssEngine.app` bundle. To see the log output, run the binary inside it from a terminal:

```bash
./build/RelWithDebInfo/AbyssEngine.app/Contents/MacOS/AbyssEngine
```

## Meson

```bash
meson setup build-meson                        # options: -Ddebug_skip_intro=false etc.
meson compile -C build-meson
./build-meson/src/AbyssEngine
```

## Checking the build

The main menu shows the exact build in the top-right corner, for example:

```
AbyssEngine rev. 5e4998c1d2a3 2026-10-10 17:20:03 +0100 (main branch) (Win64, RelWithDebInfo, Dynamic)
```

That's the git commit (a `+` after it means the build has uncommitted changes), its date, the branch, the platform,
the configuration and how the libraries are linked. Quote it when reporting a problem.

## Tests

`ctest --test-dir build -C Release --output-on-failure` (CMake) or `meson test -C <build folder>` runs them; the
CI does too, on each platform. They need no game files:

- `ring_buffer_test`: the audio's ring buffer.
- `readers_test`: the data readers (tables, DC6, DCC, DS1, DT1) and the network's item messages: input made by the
  test read right, then thousands of cut-short, changed and random ones that must never crash. `ABYSS_FUZZ_SEED=n`
  gives other random input; built with `-fsanitize=address,undefined` it also catches reads out of bounds.

---

**Next step:** [4. Game Files and Configuration](04-Game-Files-and-Configuration.md)
