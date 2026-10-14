# 5. First Run

**Installation guide:** [1. Requirements](01-Requirements.md) › [2. Getting the Source](02-Getting-the-Source.md) › [3. Building](03-Building.md) › [4. Game Files](04-Game-Files-and-Configuration.md) › **5. First Run**

Start `AbyssEngine` (Windows: `build\RelWithDebInfo\AbyssEngine.exe`, or **F5** in Visual Studio).

## What you should see

1. The Blizzard and Diablo II intro videos (skipped with the default `DEBUG_SKIP_INTRO` build option, or
   `SkipIntroMovies = true`).
2. The **main menu**, with the title music and the animated Diablo II logo:
   - **Single Player** ([Single Player](Single-Player.md)) and **Multiplayer** ([Multiplayer](Multiplayer.md))
   - **Credits**, **Cinematics**: shown but greyed out (not implemented yet)
   - **Catalog**: the item / class / sprite / bestiary browser ([Catalog](Catalog.md))
   - **Map Editor**: opens a small menu with **Map Editor**, **Testing Grounds** and **Free Roam**
     ([Map Editor](Map-Editor.md), [Testing Grounds and Free Roam](Testing-Grounds-and-Free-Roam.md))
   - **Exit Diablo II**
3. Top right: the build's version text (see [Building](03-Building.md#checking-the-build)).

Catalog and Map Editor only appear when their [CMake options](CMake-Options.md) are on (the default).

## Something's wrong?

| You see | Likely cause |
|---------|--------------|
| A window closes right away / "Could not find file abyss.ini" | `abyss.ini` isn't in one of the places listed in [Game Files and Configuration](04-Game-Files-and-Configuration.md) |
| Black screen, no menu, or missing pictures | `BasePath` is wrong or doesn't end with a slash, so the MPQs weren't loaded |
| Text missing | `Locale` isn't `latin` |

More in [Troubleshooting](Troubleshooting.md). The console (or the Visual Studio *Output* window) shows a log; quote it
together with the version text from the main menu when asking for help.

---

**Done!** Where next: [Feature Status](Feature-Status.md) · [Catalog](Catalog.md) · [Map Editor](Map-Editor.md) · [Free Roam](Testing-Grounds-and-Free-Roam.md)
