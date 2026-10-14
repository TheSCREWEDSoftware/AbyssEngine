# 1. Requirements

**Installation guide:** **1. Requirements** › [2. Getting the Source](02-Getting-the-Source.md) › [3. Building](03-Building.md) › [4. Game Files](04-Game-Files-and-Configuration.md) › [5. First Run](05-First-Run.md)

## What you need

| | Windows | Linux | macOS |
|---|---|---|---|
| **Compiler** | Visual Studio 2022 (Community is fine), workload *Desktop development with C++* | GCC or Clang | Xcode command line tools |
| **CMake** | 3.20 or newer (comes with Visual Studio, or [cmake.org](https://cmake.org/download/)) | 3.20+ (your package manager) | 3.20+ (`brew install cmake`) |
| **Git** | [git-scm.com](https://git-scm.com/download/win) | your package manager | included with Xcode tools |
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

## Linux

Pick the family your distribution belongs to. Each needs a compiler, CMake, Git, pkg-config and the development
packages for SDL2, FFmpeg, libarchive and zlib.

### Debian based (Debian, Ubuntu, Linux Mint, Pop!_OS, ...)

Example: **Ubuntu**

```bash
sudo apt install build-essential cmake git pkg-config ninja-build \
    libsdl2-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    zlib1g-dev libbz2-dev libarchive-dev
```

### RPM based (Fedora, RHEL, Rocky / Alma Linux, openSUSE, ...)

Example: **Fedora**

```bash
sudo dnf install gcc cmake git pkgconf ninja-build SDL2-devel ffmpeg-free-devel libarchive-devel zlib-devel
```

On RHEL, Rocky or Alma, turn on EPEL and RPM Fusion first for SDL2 and FFmpeg (and use `ffmpeg-devel` from RPM
Fusion). On openSUSE use `zypper install` with `SDL2-devel`, `ffmpeg-devel` (Packman), `libarchive-devel`,
`zlib-devel`.

### Arch based (Arch Linux, Manjaro, EndeavourOS, ...)

Example: **Arch Linux**

```bash
sudo pacman -S base-devel cmake git ninja sdl2 ffmpeg libarchive zlib
```

### Any of them

Optional: `meson` and `ninja-build` if you prefer Meson to CMake, `clang-format` if you'll contribute.

Building from source links against your distribution's own libraries, so there's no minimum glibc version beyond what
your distribution ships (the CI builds on the latest Ubuntu).

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
