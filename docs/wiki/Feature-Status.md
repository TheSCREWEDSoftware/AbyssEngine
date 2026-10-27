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
| Music | ✅ | Title music; in Free Roam and Single Player each area plays its own (`Levels.txt` SoundEnv → `SoundEnviron.txt` → `Sounds.txt`). Not yet the ambient sounds (wind, birds, dripping). |
| Sound effects | 🧪 | Button clicks everywhere; in Free Roam / Testing Grounds: footsteps by floor (dirt, stone, wood, sand, snow; walk and run), swings, getting hit and dying for the player and monsters (`sounds.txt`, `MonSounds.txt`). Needs `d2sfx.mpq` (and `d2exp.mpq` for the expansion's). Not yet: positional volume/panning, ambient sounds, spell sounds. Items make the game's sounds when put down, dropped (the flip and the landing) and picked up. |
| Speech (NPC dialogue) | ❌ | `d2speech` isn't played yet. |

## Game

| Feature | Status | Notes |
|---------|--------|-------|
| Intro videos | ✅ | |
| Main menu | ✅ | |
| Options: gameplay, controls | 🧪 | `options.ini` ([Options](Options.md)): Classic / Modern / Combined movement, see-through obstructions; every Diablo II control plus ours, rebindable to any number of keys, mouse buttons, wheel, gamepad buttons and sticks, modifiers and double taps, with clashes found; for all classes or per class. The Options screen on the main menu (Video, Audio, Gameplay, Controls with the ArmA-style configure window), also from the game's Esc menu; Export writes `controls.md` with keyboard and gamepad diagrams, light and dark ([Controls](Controls.md)). Presets, copying controls between classes, mouse movement and analog sticks, speech volume, `--settings` (Options before starting). Frame rate (Classic 25 / Modern 60 / 10 to unlimited, 60 at most in multiplayer), brightness, contrast and gamma, mouse pointer and wheel speed (across and up/down apart, or matched), even walking speed on screen, a Debug tab; everything but the window size changes live, in the game too. |
| Single Player | 🧪 | First playable version ([Single Player](Single-Player.md)): starts in the act's town, travel by waypoint, real combat with hit chances, experience and levels, saving; the game's control panel (life and mana globes, left and right skills, run/walk, stamina and experience bars, belt, mini panel); the inventory and skill tree. Items can be dropped and picked up; monsters and chests drop gold and items (`TreasureClassEx.txt`). Skills can be cast. No quests, hirelings or difficulty levels yet. |
| Character creation / selection | ✅ | The game's screens: the heroes round the campfire (hover, step forward, voice lines), name rules, Expansion / Hardcore; the character list with each character standing in their slot, delete with confirmation. Our own save format (`save/<name>.abc`), not `.d2s`. |
| Multiplayer | 🧪 | Our own ([Multiplayer](Multiplayer.md)), not compatible with Battle.net or the original game. Browse (LAN, and online through **abyss-lobby**), Direct IP (port 4000 if left out), Host with Diablo-style settings (password, 1-255 players, difficulty, game speed, new characters only, characters made here only, Softcore / Hardcore, level range, PvP, inventory pages, world seed), dedicated servers (`AbyssEngine --server`, characters kept on the server). One server holds the game and passes on where everyone is: you see each other walk and run between areas, with names and coloured circles, and chat. Shared monsters (one player's game runs each map's, the others follow), combat with them and between players (PvP), Nightmare and Hell, and items on the ground (the server's: the first to pick one up gets it). Monsters' drops are shared too, chests, doors and shrines open for everyone, and parties (P: invite, accept, leave) share experience. Not yet: trading between players, the party on the automap. |
| Credits, Cinematics menus | ❌ | Buttons shown, disabled. |
| Drawing the game's areas | 🧪 | Fixed areas (towns, Tristram, Monastery Gate, cloisters, every dungeon room) draw correctly in the Map Editor / Free Roam. Like in the game, while you're inside a building (within its walls, not just under the edge of its roof) the roof fades away and the walls between you and the camera turn see-through, easing in and out. |
| Randomly built areas | 🧪 | Built from the world seed (the same seed gives the same layout; **N** in Free Roam for a new world). **Wilderness** (Act 1 Blood Moor to Tamoe Highland, Burial Grounds, Moo Moo Farm; the Act 2 desert; the Act 4 steppes, plains and city; the Act 5 Highlands, Plateau and Tundra): a ring of border pieces with the odd dent, the area's landmarks (cave entrances, Cairn Stones, Inifuss tree, Forgotten Tower, tombs, ...), random filler pieces (camps, cottages, fences, oases, mesas, ...), waypoint and shrines, and small decorations scattered over the ground (`LvlSub.txt`). **Dungeons** (caves, crypts, Tower Cellar, Barracks, Jail, Catacombs, sewers, tombs, Maggot Lair, Ancient Tunnels, Act 3 dungeons and Durance of Hate, River of Flame, ice caves, Worldstone Keep) and the **Act 3 jungle**: rooms joined in a random tree, each room the preset with exactly the exits it needs, entry and exit rooms at either end. **Bloody Foothills**: the siege strips in order, west from Harrogath's gate. **Kurast** (Lower Kurast, Bazaar, Upper Kurast): its border pieces with the sewers, temples and waypoint as landmarks. **Fixed arrangements**: Harem and Palace Cellars, Spider Cave and Cavern (four quarters), Kurast Causeway, Travincal, Chaos Sanctum, Nihlathak's halls, Abaddon / Pit of Acheron / Infernal Pit. Every area is joined up: walking between the outdoor areas (now all of Act 3 from the docks to Travincal, and the Monastery Gate and Outer Cloister), stairs and cave mouths from the presets' own entrance tiles, and **red portals** where the game uses one (Tristram from the Cairn Stones, the Arcane Sanctuary, Duriel's Lair from the true Tal Rasha's Tomb, the hells, Mephisto's and Diablo's ways on) or where a preset has no entrance tile. Monsters spread over the area from its list in `Levels.txt`. Not yet: the Arcane Sanctuary's walkways don't all join up (part of it can't be reached on foot); the game's exact rules (it keeps parts of them in its program), so layouts are in the game's style rather than identical to what it would build. |
| Walking | 🧪 | Free Roam / Testing Grounds. Collision with walls from the tiles; paths around obstacles. Walk, run, and (Free Roam) sprint; the status line shows your speed. Testing Grounds plays `maps/testing_grounds.abm`, editable in the Map Editor and resettable to the built-in arena. |
| World seed, walking between areas | 🧪 | One world seed builds every random area (Free Roam: random or typed in; Single Player: a new one each game). An act's outdoors are one map, like the game's: the town and every area you can walk to from it, each placed so its border gap meets the way out of the one before, so you just walk from the Rogue Encampment through the Blood Moor to the Tamoe Highland (and Acts 2, 4 and 5's outdoors) without loading. Clicking very far away walks the whole way (the search goes on in legs). Click cave mouths, tomb doors and stairs to go in, down and back up, each leading where `Levels.txt` says. Not yet: Act 3, the Monastery, the Tristram portal; the game's own layout rules (which side each area joins on); (monsters further than about two screens from you wait until you come near, like the game's rooms). |
| Automap | 🧪 | Tab in Free Roam and Single Player: the game's automap pieces for each tile you've seen, centred on you, ways out labelled, waypoints, shrines, wells, stairs and other objects marked with their own pieces (`objects.txt` AutoMap). Not yet: the small corner map, party members on it, the game's "room" based reveal. |
| Area captions | ✅ | "Entering The Blood Moor" in red when you come into an area (from `Levels.txt`'s `LevelWarp`). |
| Pathfinding | 🧪 | A* on the sub-tile grid (8 directions, no cutting corners), smoothed into straight legs. The player walks around walls and objects to where you click (or to the closest reachable spot); chasing monsters go straight at you when nothing's in the way and around walls otherwise. Units don't avoid each other yet. |
| NPCs in towns | 🧪 | In their places, and they stroll around them (pause a few seconds, walk to a nearby spot along a path, repeat); guards keep their post. They don't talk or trade yet. |
| Combat | 🧪 | Single Player: always on, town folk never fight; the game's chance to hit (attack rating and level against defense and level) both ways; your damage from the weapon you hold and Strength, your defense from Dexterity and what you wear. Free Roam / Testing Grounds: debug toggle for monsters (town folk never fight), units without an attack animation never attack. Skills and their missiles hurt monsters; monsters are still melee only (no blocking, ranged attacks or AI beyond "walk at the player"). |
| Player stats, leveling, experience | 🧪 | Single Player: Strength, Dexterity, Vitality, Energy from `charstats.txt`; life, mana and stamina per point and per level; experience per kill (`monstats.txt`/`MonLvl.txt`) and per level (`experience.txt`); 5 stat points and a skill point per level, spent on the character screen (the game's panel); mana regeneration, stamina for running. Damage, defense, attack rating, life, mana and stats from what's worn, magic included. |
| Skills | 🧪 | Single Player: the game's skill tree (T), points spent by the game's rules (required level plus points already in, prerequisites, 20 at most), left and right skill chosen on the control panel, saved with the character. Casting (hold the right button; the left skill on monsters or with Shift) with mana from `skills.txt`, damage and calcs by skill level, synergies: missiles from `Missiles.txt` with their pictures (bursts, chains, piercing), novas, area skills, weapon skills, buffs and warcries, passives, auras, curses, Teleport, summons that fight for you; chill, freeze and poison. Multiplayer: the others see your missiles, novas and area skills. Not yet: Druid shapes, Assassin traps, Whirlwind and leaps, some rarer skills. Skill trees can also be browsed in the [Catalog](Catalog.md). |
| Items: inventory, equipment, belt | 🧪 | Single Player: the game's inventory (I) laid out from `inventory.txt`, with 1 to 255 pages (Options, or the multiplayer game's setting); worn gear picked up the Classic way (off at once) or the Modern way (worn until put down); starting items from `charstats.txt`; pick up, put down and swap with the mouse; dropping items on the ground and picking them up (the game's flip pictures and sounds, names on Alt); wearing checks the slot, requirements, two-handed weapons and dual wielding; the belt (1-4, right click) with healing and mana potions over time and rejuvenation at once; saved with the character. Monster and chest drops from the game's treasure classes, gold; magic, rare and unique items (the game's affixes and uniques, with the properties the engine knows working). Resistances (yours from gear, less Nightmare's and Hell's penalty; monsters' from monstats.txt, elemental attacks). What you wear on the character's figure (and the character list; the others see it in multiplayer); dropping gold; the stash in each town (6 x 8, with its gold); sockets with gems, runes and jewels, and runewords (`Runes.txt`). Not yet: set items, durability, identifying. |
| Objects (chests, shrines, waypoints, doors, torches) | 🧪 | Placed from the areas' presets; torches and fires burn. In Free Roam / Testing Grounds click one to walk over and open it (chests open, doors open and stop blocking, shrines and waypoints activate) with its sound; objects block walking where the game says they do. Not yet: what they do (loot, shrine effects, waypoint travel, locked doors, traps). |
| Quests | ❌ | |
| Saving / loading characters | ❌ | |
| Lighting | 🧪 | **L** cycles auto / day / dusk / night. Auto (the default) is dark with a pool of light around you indoors (caves, dungeons, `Levels.txt` IsInside) and daylight outside. Not yet the day/night cycle, light from torches or the palette-based shading. |
| Weather (rain, snow) | 🧪 | Showers come and go (about two minutes in six) in the areas `Levels.txt` marks for rain; snow in Act V. Drawn over the view; no sound or splashes yet. |
| Animated floor tiles | ✅ | Lava and other animated floors play (detected from the tile sets: frames stored as numbered variants). The game's water is mostly still tiles anyway. |

## Developer tools

| Tool | Status | Notes |
|------|--------|-------|
| [Catalog](Catalog.md): weapons, armor | ✅ | Search, filter by type/tier, sort by level/damage/defense/strength. |
| Catalog: classes and skill trees | ✅ | |
| Catalog: sprites | ✅ | Every DC6, by category, palettes, animation. |
| Catalog: bestiary | ✅ | Players, NPCs, monsters, bosses, objects, every animation and direction. |
| [Map Editor](Map-Editor.md) | ✅ | Open game areas, paint tiles, place units, save/load `.abm`. Undo / redo (Ctrl+Z / Ctrl+Y, "Changes: 4 / 11"), a recovery file after every change, auto-save, Back asks about unsaved changes. Can't save back to DS1. |
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
