# Abyss Engine Wiki

Abyss Engine is a clean-room reimplementation of the **Diablo II: Lord of Destruction** engine, written in C. It uses
the game's original data files (the MPQs from your own copy of the game) and draws, plays and runs them on a modern,
portable engine.

> **Status:** early. The engine boots to the main menu and has a set of developer tools (Catalog, Map Editor, Testing
> Grounds, Free Roam). Single Player is a first playable version: create a character, fight, gain levels, spend skill
> points, wear items, drink potions, save; no casting, drops or quests yet. Multiplayer is a first version: host or
> join a game (LAN, online or by address) and see each other walk around and chat; monsters aren't shared yet.
> See **[Feature Status](Feature-Status.md)** for exactly what works and what doesn't.

## Installation guide

Follow these in order. Each page ends with a link to the next one.

| Step | Page | What you do |
|------|------|-------------|
| 1 | [Requirements](01-Requirements.md) | Install the tools (Visual Studio / compiler, CMake, Git, vcpkg) |
| 2 | [Getting the Source](02-Getting-the-Source.md) | Clone the repository |
| 3 | [Building](03-Building.md) | Configure with CMake and compile (Windows, Linux, macOS) |
| 4 | [Game Files and Configuration](04-Game-Files-and-Configuration.md) | Point the engine at your Diablo II MPQs (`abyss.ini`) |
| 5 | [First Run](05-First-Run.md) | Start the engine and check that it works |

## Reference

- [CMake and Meson Options](CMake-Options.md): the `DEBUG` options (Skip Intro, Show Catalog, Show Map Editor), version text
- [Feature Status](Feature-Status.md): what works, what's debug-only, and what's not done yet
- [Troubleshooting](Troubleshooting.md): known problems and fixes

## Playing

- [Single Player](Single-Player.md): characters, creation, levels and stats, controls
- [Options](Options.md): gameplay (movement type, see-through), controls and key binding, `options.ini`

## Developer tools

- [Catalog](Catalog.md): browse weapons, armor, classes and skill trees, every sprite and the whole bestiary
- [Map Editor](Map-Editor.md): open the game's areas or make your own maps; the `.abm` map format
- [Testing Grounds and Free Roam](Testing-Grounds-and-Free-Roam.md): walk around maps and towns, drop monsters, combat toggle

## Contributing

- Commit messages: a short summary line, a blank line, then what changed and why.
- Run `clang-format` on the files you touch (the repository has a `.clang-format`).
- Keep the [Feature Status](Feature-Status.md) page up to date when you add or finish something.

> The wiki is generated from the [`docs/wiki`](https://github.com/TheSCREWEDSoftware/AbyssEngine/tree/main/docs/wiki)
> folder of the repository. Edit the files there (in a pull request) rather than in the GitHub wiki editor: the wiki is
> overwritten from that folder on every push to `main`.
