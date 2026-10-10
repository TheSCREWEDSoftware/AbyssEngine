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
| Single Player | ❌ | Button shown, disabled. |
| Character creation / selection | ❌ | |
| Multiplayer / Battle.net | ❌ | Won't be compatible with the original game's servers or saves. |
| Credits, Cinematics menus | ❌ | Buttons shown, disabled. |
| Drawing the game's areas | 🧪 | Fixed areas (towns, Tristram, Monastery Gate, cloisters, every dungeon room) draw correctly in the Map Editor / Free Roam. |
| Randomly built areas (Blood Moor, Cold Plains, dungeons' random layouts) | ❌ | The game assembles these from many small pieces (LvlMaze/LvlSub); not implemented. |
| Walking | 🧪 | Free Roam / Testing Grounds. Collision with walls from the tiles; paths around obstacles. |
| Pathfinding | 🧪 | A* on the sub-tile grid (8 directions, no cutting corners), smoothed into straight legs. The player walks around walls and objects to where you click (or to the closest reachable spot); chasing monsters go straight at you when nothing's in the way and around walls otherwise. Units don't avoid each other yet. |
| NPCs in towns | 🧪 | In their places, and they stroll around them (pause a few seconds, walk to a nearby spot along a path, repeat); guards keep their post. They don't talk or trade yet. |
| Combat | 🧪 | Debug toggle only: units without an attack animation (most town NPCs) never attack; melee, life and damage from the game tables (normal difficulty), get-hit/death animations. No skills, levels, experience, blocking, missiles or AI beyond "walk at the player". |
| Player stats, leveling, experience | ❌ | Life comes from the class's starting value. |
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
| [Free Roam](Testing-Grounds-and-Free-Roam.md) | ✅ | Towns and fixed outdoor areas with their NPCs; teleport list; combat toggle. |
| Loading screen | ✅ | Progress bar; the picture plays with the progress. |

## Platforms

| Platform | Status |
|----------|--------|
| Windows 10/11 x64 (Visual Studio 2022 + vcpkg) | ✅ |
| Linux x64 (GCC/Clang, CMake or Meson) | ✅ |
| macOS | 🧪 Supported by the build files; not tested recently. |
