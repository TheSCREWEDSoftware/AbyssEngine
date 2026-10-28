# CMake and Meson Options

## Debug options

In **CMake GUI**, tick **Grouped**: these appear under **DEBUG**. All are on by default.

| CMake | Meson | Default | What it does |
|-------|-------|---------|--------------|
| `DEBUG_FEATURES` | | `ON` | Master switch. Turning it off turns all the options below off. |
| `DEBUG_SKIP_INTRO` | `debug_skip_intro` | `ON` | Start at the main menu, whatever `SkipIntroMovies` says in `abyss.ini`. |
| `DEBUG_SHOW_CATALOG` | `debug_show_catalog` | `ON` | Show the **Catalog** button on the main menu. |
| `DEBUG_SHOW_MAP_EDITOR` | `debug_show_map_editor` | `ON` | Show the **Map Editor** button (Map Editor, Testing Grounds, Free Roam). |
| `DEBUG_SHOW_OPTIONS_DEBUG` | `debug_show_options_debug` | `ON` | Show the **Debug** tab in Options (Map Editor auto-save, collision view in Single Player, ...). Off, those settings keep their defaults or what `options.ini` says. |

## Defaults

| CMake | Meson | Default | What it does |
|-------|-------|---------|--------------|
| `DEFAULT_STYLE` | `default_style` | `Original` | The starting style of every control and setting that has an **Original** (Diablo II's; `Classic`, the old name, still works) and a **Modern** way: the type of movement (click to walk, or keys to walk), picking up worn gear (it comes off at once, or stays worn until put down), and the control panel. Players can still change each one in Options. The frame rate's default is Modern (60) either way. |

```bash
cmake -S . -B build -DDEFAULT_STYLE=Modern
meson setup build-meson -Ddefault_style=Modern
```

Command line examples:

```bash
cmake -S . -B build -DDEBUG_FEATURES=OFF            # a "release" build without the developer tools
cmake -S . -B build -DDEBUG_SKIP_INTRO=OFF          # keep the tools, but play the intro videos
meson setup build-meson -Ddebug_show_catalog=false
```

## Programs

| CMake | Meson | Default | What it does |
|-------|-------|---------|--------------|
| `BUILD_LOBBY_SERVER` | `build_lobby_server` | `ON` | Also build **abyss-lobby**, the server that lists online games ([Multiplayer](Multiplayer.md#online-games-the-lobby-server)). A small program of its own, without SDL. |

## Version text

The main menu shows the git commit the build was made from, in the same format as AzerothCore:

```
AbyssEngine rev. <commit> <commit date> (<branch> branch) (<platform>, <configuration>, <Static|Dynamic>)
```

It's refreshed on every build (the `revision_h` target writes `revision_data.h` into the build folder).

| CMake | Default | What it does |
|-------|---------|--------------|
| `ABYSS_VERSION_TEXT` | empty | Use this instead of the git hash (the CI passes the GitHub commit). |
| `WITHOUT_GIT` | `OFF` | Don't ask git at all; the text says `unknown (local build ...)`. |

Meson reads the commit when the build is configured (re-run `meson setup --reconfigure` to refresh it).

## Build type

Single-configuration generators (Ninja, Makefiles) use `CMAKE_BUILD_TYPE` (`Debug`, `Release`, `RelWithDebInfo`,
`MinSizeRel`; default `Release`). Visual Studio and Xcode pick the configuration in the IDE instead.
