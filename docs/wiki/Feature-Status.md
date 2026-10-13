# Feature Status

What works in Abyss Engine today, what only exists in the developer tools, and what isn't done.
**Keep this page up to date** when you add or finish something.

Legend: ✅ works · 🧪 debug tools only / partial · ❌ not done yet

## Game data and formats

| Feature | Status | Notes |
|---------|--------|-------|
| MPQ archives (all compression types, listfile) | ✅ | Load order from `abyss.ini`, first match wins. Case-insensitive file names on Linux. |
| Palettes | ✅ | All act, menu and UI palettes. |
| DC6 sprites (UI, items, fonts) | ✅ | All 1,633 DC6 files in the game load. |
| DCC animations | ✅ | All 21,717 DCC files load. |
| COF (layered characters and monsters) | ✅ | Armor/weapon layers, draw order per direction and frame. |
| DT1 tiles | ✅ | All 263 tile sets (floors, walls, roofs, shadows, walk flags). |
| DS1 maps (area presets) | ✅ | All 2,276, with the NPCs, monsters and objects (chests, doors, shrines, waypoints, torches, ...) placed in them. |
| Data tables (`data/global/excel/*.txt`) | ✅ | Items, monsters, skills, levels, presets, ... |
| String tables (`.tbl`) | ✅ | |
| Fonts and text | ✅ | Latin fonts. |
| Intro videos (`.bik`) | ✅ | Through FFmpeg. |
| Music | ✅ | Title music. |
| Sound effects | 🧪 | Button clicks everywhere; in Free Roam / Testing Grounds: footsteps by floor (dirt, stone, wood, sand, snow; walk and run), swings, getting hit and dying for the player and monsters (`sounds.txt`, `MonSounds.txt`). Needs `d2sfx.mpq` (and `d2exp.mpq` for the expansion's). Not yet: positional volume/panning, ambient sounds, item and spell sounds. |
| Speech (NPC dialogue) | ❌ | `d2speech` isn't played yet. |

## Game

| Feature | Status | Notes |
|---------|--------|-------|
| Intro videos | ✅ | |
| Main menu | ✅ | |
| Single Player | 🧪 | First playable version ([Single Player](Single-Player.md)): starts in the act's town, travel by waypoint, real combat with hit chances, experience and levels, saving. No items, skills, quests, potions, hirelings or difficulty levels yet. |
| Character creation / selection | ✅ | The game's screens: the heroes round the campfire (hover, step forward, voice lines), name rules, Expansion / Hardcore; the character list with each character standing in their slot, delete with confirmation. Our own save format (`save/<name>.abc`), not `.d2s`. |
| Multiplayer / Battle.net | ❌ | Won't be compatible with the original game's servers or saves. |
| Credits, Cinematics menus | ❌ | Buttons shown, disabled. |
| Drawing the game's areas | 🧪 | Fixed areas (towns, Tristram, Monastery Gate, cloisters, every dungeon room) draw correctly in the Map Editor / Free Roam. |
| Randomly built areas | 🧪 | Built from the world seed (the same seed gives the same layout; **N** in Free Roam for a new world). **Wilderness** (Act 1 Blood Moor to Tamoe Highland, Burial Grounds, Moo Moo Farm; the Act 2 desert; the Act 4 steppes, plains and city; the Act 5 Highlands, Plateau and Tundra): a ring of border pieces with the odd dent, the area's landmarks (cave entrances, Cairn Stones, Inifuss tree, Forgotten Tower, tombs, ...), random filler pieces (camps, cottages, fences, oases, mesas, ...), waypoint and shrines, and small decorations scattered over the ground (`LvlSub.txt`). **Dungeons** (caves, crypts, Tower Cellar, Barracks, Jail, Catacombs, sewers, tombs, Maggot Lair, Ancient Tunnels, Act 3 dungeons and Durance of Hate, River of Flame, ice caves, Worldstone Keep) and the **Act 3 jungle**: rooms joined in a random tree, each room the preset with exactly the exits it needs, entry and exit rooms at either end. **Bloody Foothills**: the siege strips in order, west from Harrogath's gate. Monsters spread over the area from its list in `Levels.txt`. Not yet: Kurast (Lower Kurast, Bazaar, Upper Kurast), Kurast Causeway, Travincal, Arcane Sanctuary, Chaos Sanctum, Spider Cave/Cavern; the game's exact rules (it keeps parts of them in its program), so layouts are in the game's style rather than identical to what it would build. |
| Walking | 🧪 | Free Roam / Testing Grounds. Collision with walls from the tiles; paths around obstacles. |
| World seed, walking between areas | 🧪 | One world seed builds every random area (Free Roam: random or typed in; Single Player: a new one each game). Areas are joined like in the game: walk from the Rogue Encampment through the Blood Moor to the Tamoe Highland (and Acts 2, 4 and 5's outdoor chains), coming in on the side facing the way you left; click cave mouths, tomb doors and stairs to go in, down and back up, each leading where `Levels.txt` says. Not yet: Act 3, the Monastery, the Tristram portal; the game's own choice of which side each area joins on. |
| Pathfinding | 🧪 | A* on the sub-tile grid (8 directions, no cutting corners), smoothed into straight legs. The player walks around walls and objects to where you click (or to the closest reachable spot); chasing monsters go straight at you when nothing's in the way and around walls otherwise. Units don't avoid each other yet. |
| NPCs in towns | 🧪 | In their places, and they stroll around them (pause a few seconds, walk to a nearby spot along a path, repeat); guards keep their post. They don't talk or trade yet. |
| Combat | 🧪 | Single Player: always on, town folk never fight; the game's chance to hit (attack rating and level against defense and level) both ways; your damage from your starting weapon and Strength. Free Roam / Testing Grounds: debug toggle for monsters (town folk never fight), units without an attack animation never attack. Melee only: no skills, blocking, missiles or AI beyond "walk at the player". |
| Player stats, leveling, experience | 🧪 | Single Player: Strength, Dexterity, Vitality, Energy from `charstats.txt`; life, mana and stamina per point and per level; experience per kill (`monstats.txt`/`MonLvl.txt`) and per level (`experience.txt`); 5 stat points and a skill point per level, spent on the character screen (the game's panel); mana regeneration, stamina for running. No items, so no bonuses from gear; skill points can't be spent yet. |
| Skills | ❌ | Skill trees can be browsed in the [Catalog](Catalog.md). |
| Items: inventory, equipment, drops, stash | ❌ | Item data and pictures can be browsed in the Catalog. |
| Objects (chests, shrines, waypoints, doors, torches) | 🧪 | Placed from the areas' presets; torches and fires burn. In Free Roam / Testing Grounds click one to walk over and open it (chests open, doors open and stop blocking, shrines and waypoints activate) with its sound; objects block walking where the game says they do. Not yet: what they do (loot, shrine effects, waypoint travel, locked doors, traps). |
| Quests | ❌ | |
| Saving / loading characters | ❌ | |
| Lighting | 🧪 | Free Roam / Testing Grounds: **L** cycles day / dusk / night (a soft pool of light around the player, darkness elsewhere). Not yet the game's own per-area light levels, light from torches or the palette-based shading. |
| Weather (rain, snow) | ❌ | |
| Animated floor tiles | ✅ | Lava and other animated floors play (detected from the tile sets: frames stored as numbered variants). The game's water is mostly still tiles anyway. |

## Developer tools

| Tool | Status | Notes |
|------|--------|-------|
| [Catalog](Catalog.md): weapons, armor | ✅ | Search, filter by type/tier, sort by level/damage/defense/strength. |
| Catalog: classes and skill trees | ✅ | |
| Catalog: sprites | ✅ | Every DC6, by category, palettes, animation. |
| Catalog: bestiary | ✅ | Players, NPCs, monsters, bosses, objects, every animation and direction. |
| [Map Editor](Map-Editor.md) | ✅ | Open game areas, paint tiles, place units, save/load `.abm`. No undo yet. Can't save back to DS1. |
| [Testing Grounds](Testing-Grounds-and-Free-Roam.md) | ✅ | Default arena or your own map; drop monsters; combat toggle; placement preview. |
| [Free Roam](Testing-Grounds-and-Free-Roam.md) | ✅ | Towns, fixed areas and randomly built wilderness and dungeons; teleport list; combat toggle. |
| Loading screen | ✅ | Progress bar; the picture plays with the progress. |

## Platforms

| Platform | Status |
|----------|--------|
| Windows 10/11 x64 (Visual Studio 2022 + vcpkg) | ✅ |
| Linux x64 (GCC/Clang, CMake or Meson) | ✅ |
| macOS (Apple Silicon, Homebrew) | 🧪 Built by CI on every push; not played on recently. |

Every push to `main` is built on all three by GitHub Actions (`.github/workflows/ci.yml`): Windows with vcpkg, Linux
with the distribution's packages, macOS with Homebrew. The builds of `main` can be downloaded from the run's
*Artifacts* (`unstable-Windows`, `unstable-Linux`, `unstable-macOS`).
