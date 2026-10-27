# Abyss Engine

[![Continuous Integration](https://github.com/TheSCREWEDSoftware/AbyssEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/TheSCREWEDSoftware/AbyssEngine/actions/workflows/ci.yml)

Abyss Engine is a clean-room reimplementation of the **Diablo II: Lord of Destruction** engine, written in C. It runs on
the game's own data files (the MPQs from your copy of the game) on a modern, portable engine that is easy to mod and
extend.

This is **not** a reverse-engineering project: no original code from the game is used. It is not compatible with the
original game's save files or with Battle.net.

## Status

Early, but playable in parts:

- **Menus**: intro videos, the main menu, loading screens, the game's fonts, buttons, cursors and sounds.
- **Single Player** (first version): create a character on the game's "Select Hero Class" screen, pick it from the
  character list, walk out of town, fight, gain experience and levels, spend stat and skill points, wear and swap
  items in the game's inventory, drink potions from the belt, travel by waypoint, save. No casting, drops, quests,
  trading or hirelings yet.
- **Multiplayer** (first version): browse games on the local network or online, join by address, host your own with
  Diablo-style settings, or run a dedicated server (`AbyssEngine --server`). Players see each other walk around and
  chat; monsters and combat aren't shared yet.
- **The world**: the towns and fixed areas from the game's maps with their NPCs and objects; the wilderness, dungeons,
  jungle and Bloody Foothills built at random from the game's pieces. One world seed builds every area, and areas are
  joined like in the game: walk out of the Rogue Encampment into the Blood Moor, go down into caves and back up.
- **Developer tools**: a Catalog (items, classes and skill trees, every sprite, the whole bestiary), a Map Editor,
  the Testing Grounds and Free Roam.

The wiki's **[Feature Status](https://github.com/TheSCREWEDSoftware/AbyssEngine/wiki/Feature-Status)** page says
exactly what works and what doesn't, and [`TODO.md`](TODO.md) lists what's being worked on next.

## Documentation

The **[wiki](https://github.com/TheSCREWEDSoftware/AbyssEngine/wiki)** has a step-by-step installation guide
(requirements, getting the source, building, game files, first run), the build options, the developer tools and
troubleshooting. Its pages live in [`docs/wiki`](docs/wiki) and are published to the wiki on every push.

## Quick start

### 1. Install the tools and libraries

The engine needs a C99 compiler, CMake 3.20+, Git, and four libraries: **SDL2**, **FFmpeg** (libavcodec, libavformat,
libavutil, libswscale, libswresample), **libarchive** and **zlib**.

**Windows**: Visual Studio 2022 with *Desktop development with C++*, the *C++ CMake tools* and the *vcpkg package
manager*. vcpkg downloads and builds the libraries the first time you configure (this takes a while).

**Linux**, by family (the [Requirements](docs/wiki/01-Requirements.md) page has more distributions):

- **Debian based** (e.g. Ubuntu):

  ```bash
  sudo apt install build-essential cmake git pkg-config ninja-build \
      libsdl2-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
      libarchive-dev zlib1g-dev
  ```

- **RPM based** (e.g. Fedora):
  `sudo dnf install gcc cmake git pkgconf ninja-build SDL2-devel ffmpeg-free-devel libarchive-devel zlib-devel`
- **Arch based** (e.g. Arch Linux): `sudo pacman -S base-devel cmake git ninja sdl2 ffmpeg libarchive zlib`

Building from source links against your system's own libraries, so it works on whatever glibc your distribution has.

**macOS**: `xcode-select --install`, then `brew install cmake ninja pkg-config sdl2 ffmpeg libarchive`.

### 2. Get the source and build

```bash
git clone --recursive https://github.com/TheSCREWEDSoftware/AbyssEngine.git
cd AbyssEngine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On macOS, if CMake can't find libarchive, add `-DCMAKE_PREFIX_PATH=$(brew --prefix libarchive)`.

On Windows, open the folder in Visual Studio (or run `Open CMake GUI.bat`), which configures with vcpkg for you; see
[Building](https://github.com/TheSCREWEDSoftware/AbyssEngine/wiki/03-Building) for the details.

Meson works too: `meson setup build && ninja -C build`.

Every push is built on Windows, Linux and macOS by GitHub Actions. The latest builds of `main` can be downloaded from
the [Actions](https://github.com/TheSCREWEDSoftware/AbyssEngine/actions/workflows/ci.yml) page (the run's
*Artifacts*). They are development builds.

Releases are built by `.github/workflows/release.yml`: pushing a tag like `v0.2.0` builds downloads for all three
(a Windows `.zip` with its DLLs, a Linux `.tar.gz`, a macOS `.zip` of the app with its libraries in it) and makes a
draft release with them on the [Releases](https://github.com/TheSCREWEDSoftware/AbyssEngine/releases) page. Each has the engine,
`abyss-lobby`, `abyss.ini.dist` (copy it to `abyss.ini`), this README and the licence. The Linux one uses the
distribution's SDL2, FFmpeg and libarchive (the packages above).

### 3. Point it at your game files

You need the MPQ files from a legitimate copy of **Diablo II** and **Lord of Destruction** (the classic game, not
*Resurrected*): at least `d2data.mpq`, `d2exp.mpq`, `d2char.mpq` and `Patch_D2.mpq`; add `d2sfx.mpq`, the music and the
video MPQs for sound, music and the intro videos. They are not included, and modded or repacked copies are not
supported.

Copy [`content/abyss.ini`](content/abyss.ini) to one of these places (the first one found is used) and set `BasePath`
to the folder with your MPQs (with or without a slash at the end):

1. next to the program (`AbyssEngine` / `AbyssEngine.exe`), for a portable setup;
2. your user settings folder:
   - Windows: `%APPDATA%\abyss\abyss.ini`
   - Linux: `~/.config/abyss/abyss.ini`
   - macOS: `~/Library/Application Support/abyss/abyss.ini`

```ini
[General]
BasePath = C:\Program Files (x86)\Diablo II\
```

### 4. Run

Start `AbyssEngine` (`AbyssEngine.exe` on Windows, the `AbyssEngine.app` bundle on macOS) from the build folder. Logs go
to the console; on macOS run `./AbyssEngine.app/Contents/MacOS/AbyssEngine` from a terminal to see them. If something
doesn't start, see [Troubleshooting](https://github.com/TheSCREWEDSoftware/AbyssEngine/wiki/Troubleshooting).

## Contributing

- Format C code with the repository's `.clang-format` (`clang-format -i` on the files you changed).
- Keep the wiki in step: a change to what works goes into [`docs/wiki`](docs/wiki) (especially Feature Status) in the
  same commit.
- Plain C99/gnu99 and the standard library, SDL2, FFmpeg and libarchive only; it has to build with MSVC, GCC and Clang.

## Related projects

- [AbyssEngine/AbyssEngine](https://github.com/AbyssEngine/AbyssEngine): the original C engine this repository
  continues from.
- [AbyssEngine/AbyssEngineOld](https://github.com/AbyssEngine/AbyssEngineOld) (C++) and
  [OpenDiablo2](https://github.com/OpenDiablo2/OpenDiablo2) (Go, archived; GPL-3.0): earlier attempts by the same
  community, useful as a reference for the file formats and how the game behaves. Their code is under different
  licenses, so it isn't copied here.
- [DevilutionX](https://github.com/diasurgical/devilutionX): what this aims to be for Diablo II, for the first Diablo.

## License

MIT, see [LICENSE](LICENSE). Diablo II and Diablo II: Lord of Destruction are trademarks of Blizzard Entertainment. This
project is not made, supported or endorsed by Blizzard.
