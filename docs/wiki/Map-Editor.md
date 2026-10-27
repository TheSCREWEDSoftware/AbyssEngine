# Map Editor

**Main menu › Map Editor › Map Editor** (needs the `DEBUG_SHOW_MAP_EDITOR` [option](CMake-Options.md), on by default),
or on its own, without the game's menus:

```
AbyssEngine --editor                         the editor straight away (Back quits)
AbyssEngine --editor mymap                   opening one of your maps (maps/mymap.abm), or any .abm file by its path
AbyssEngine --editor act1/town/townN1.ds1    opening one of the game's areas (its path in the MPQs)
```

Open the game's own areas or build your own maps from the game's tile sets, place monsters, NPCs and objects, and
try them straight away in the [Testing Grounds](Testing-Grounds-and-Free-Roam.md). New to it? The
[Map Editor Guide](Map-Editor-Guide.md) goes from the first room to putting a map in the game with a
[mod](Modding.md).

## Screen

- **Map view** (left): the map as the game draws it.
- **Tool panel** (right): tool tabs, a preview of what you'll paint, the list to pick from, Fill Floor, Back.
- **File buttons** (bottom): New, Open, Save, Play.
- **Status bar** (top): map name, the tile and sub-tile under the mouse, zoom.

## Tools

| Tab | Left click | Right click |
|-----|------------|-------------|
| **Floor** | Paint the selected floor tile (hold and drag to paint many) | Remove the floor |
| **Wall** | Place the selected wall / corner / door / pillar / tree / roof | Remove the top wall of that tile |
| **Shadow** | Paint a shadow | Remove it |
| **Units** | Place the selected monster, NPC, object or player (animated) | Remove the nearest unit |

The monsters, NPCs and players for **Units** are loaded when the editor opens (under its loading screen), so the tab is ready the first time you click it.

**Fill Floor** (Floor tab) covers the whole map with the selected floor.

## Moving around

| | |
|---|---|
| Middle mouse drag, WASD or arrow keys | Move the view |
| Mouse wheel | Zoom (0.25× to 2×) |
| **G** | Grid on/off |
| **H** | Hide/show walls |
| **B** | Show the sub-tiles you can't walk on (red) |
| **R** | Roofs on/off |

## File buttons

- **New**: pick a **tile set** (any of the game's area types, e.g. *Act 1 - Town*, *Act 2 - Desert*, *Act 4 - Lava*)
  and a **size** (16×16 up to 128×128). **Arena** starts from the default Testing Grounds map.
- **Open**:
  - **My Maps**: maps you saved
  - **Game Areas (DS1)**: any of the game's 2,276 area presets (Rogue Encampment = `ACT1\TOWN\townN1`, Lut Gholein =
    `ACT2\Town\lutN`, ...). Type in the search box to find one.
  - **Reset TG** (My Maps): puts `testing_grounds` back to the original arena, after asking. The original is kept in
    the program, so it can't be lost; if that map is open, the editor opens the original.
- **Save**: give the map a name; it's saved as `maps/<name>.abm` next to `abyss.ini`. **As Testing** saves it as
  `testing_grounds.abm`, the map the Testing Grounds play (it's in **My Maps** from the start, made from the built-in
  arena). **As DS1** saves it in the game's own format as `maps/<name>.ds1` (version 18, what the expansion's
  presets use): its layers, tile sets and the monsters and objects that have a preset number in the act's tables
  (`MonPreset.txt`, the objects' presets); others are left out. The game's DS1 tools open it, and so does the engine
  (a saved game area comes back exactly as it was).
- **Play**: try the map in the Testing Grounds right away. **Back** there returns to the editor with your map as you
  left it (its file and its undo history too).
- **Back** (or **Esc**) asks first: with unsaved changes it offers **Save** (into the map's file, or asks for a name),
  **Leave** (lose them) or **Cancel**; otherwise "Return to the main menu?".

## Undo, redo and not losing work

- **Ctrl+Z** undoes the last change, **Ctrl+Y** (or **Ctrl+Shift+Z**) does it again. A change is one stroke of the
  brush (press to release), one **Fill Floor**, one unit placed or removed. The panel shows where you are:
  **Changes: 4 / 11** (four in effect, seven more you can redo). Making a new change after undoing drops the ones you
  could have redone. Up to 1,000 changes are kept; opening or starting a map starts over.
- **Keep every change in a temporary file** (Options › Gameplay › Debug, on by default): after every change the map
  is written to `editor-recovery.abm` next to `abyss.ini`. If the editor closes with unsaved changes (a crash, closing
  the window), it offers them back the next time it opens: **Restore** or **Discard**. Saving, or leaving on
  purpose, removes the file.
- **Auto-save** (Options › Gameplay › Debug: off, or every 1 to 30 minutes; 5 by default): a map with a file is saved
  into it; one never saved goes to `editor-recovery.abm`.

## Coordinates

A map is a grid of **tiles** (160×80 pixel diamonds). Each tile is split into 5×5 **sub-tiles**; units stand on
sub-tiles and walkability is per sub-tile. The status bar shows both for the mouse position.

## The `.abm` format

Plain text, so maps can be compared and merged in git:

```
AbyssMap 1
name My Map
size 24 24
act 0
layers 2 1
dt1 data\global\tiles\Act1\Town\Floor.dt1
dt1 data\global\tiles\Act1\Town\Fence.dt1
floor 0
00000001 00000001 ...          (one line per row of tiles; "height" lines of "width" cells)
wall 0
...
wall 1
...
shadow
...
entity monsters zombie1 60.50 42.50 0 Zombie
end
```

| Line | Meaning |
|------|---------|
| `size W H` | Width and height in tiles |
| `act N` | 0 to 4: chooses the palette |
| `layers W F` | Number of wall and floor layers |
| `dt1 path` | A tile set the map uses (any number) |
| `floor N`, `wall N`, `shadow` | A layer: W×H cells in hex |
| `entity base id x y direction name` | A unit: `base` is `monsters`, `chars` or `objects`; `id` its id in the game tables (`zombie1`, `akara`, a player token like `am`); `x y` in sub-tiles; `direction` 0 to 15 (0 = facing down the screen) |

Cell bits: `prop1` (bits 0-7, 0 = empty), sequence/sub-index (8-13), orientation (14-19), style/main-index (20-25),
hidden (31).

## Not done yet

- Rotating placed units
- The game's objects (chests, waypoints...) in opened areas
