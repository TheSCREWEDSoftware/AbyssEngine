# 1. Requirements

**Installation guide:** **1. Requirements** › [2. Getting the Source](02-Getting-the-Source.md) › [3. Building](03-Building.md) › [4. Game Files](04-Game-Files-and-Configuration.md) › [5. First Run](05-First-Run.md)

## What you need

| | Windows | Linux | macOS |
|---|---|---|---|
| **Compiler** | Visual Studio 2022 (Community is fine), workload *Desktop development with C++* | GCC or Clang | Xcode command line tools |
| **CMake** | 3.20 or newer (comes with Visual Studio, or [cmake.org](https://cmake.org/download/)) | 3.20+ (`apt install cmake`) | 3.20+ (`brew install cmake`) |
| **Git** | [git-scm.com](https://git-scm.com/download/win) | `apt install git` | included with Xcode tools |
| **Libraries** | vcpkg (installs them for you) | SDL2, FFmpeg, zlib, libarchive from your package manager | vcpkg or Homebrew |
| **Game files** | Your own Diablo II + Lord of Destruction MPQs | same | same |

The engine needs: **SDL2** (window, input, audio), **FFmpeg** (the intro videos and music), **zlib** and **libarchive**.

## Windows

1. Install **Visual Studio 2022**. In the installer, tick:
   - *Desktop development with C++*
   - under *Individual components*: **vcpkg package manager** and **C++ CMake tools for Windows**
2. Install **Git for Windows**.
3. (Optional) Install CMake separately if you want `cmake-gui`; Visual Studio's own copy works for building.

> vcpkg downloads and builds SDL2, FFmpeg and the rest the first time you configure. This takes a while (FFmpeg
> especially): 15 to 30 minutes is normal. It only happens once.

If you don't use the vcpkg that comes with Visual Studio:

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

## Linux (Debian / Ubuntu)

```bash
sudo apt install build-essential cmake git pkg-config \
    libsdl2-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    zlib1g-dev libbz2-dev libarchive-dev
```

Optional: `meson` and `ninja-build` if you prefer Meson to CMake, `clang-format` if you'll contribute.

## macOS

```bash
xcode-select --install
brew install cmake sdl2 ffmpeg libarchive
```

## Game files

You need the MPQ files from a legitimate copy of **Diablo II** and **Lord of Destruction** (the classic game, not
*Resurrected*). They are not included in the repository. See [Game Files and Configuration](04-Game-Files-and-Configuration.md).

---

**Next step:** [2. Getting the Source](02-Getting-the-Source.md)
