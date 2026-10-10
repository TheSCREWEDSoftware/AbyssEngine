# Map Editor

**Main menu › Map Editor › Map Editor** (needs the `DEBUG_SHOW_MAP_EDITOR` [option](CMake-Options.md), on by default).

Open the game's own areas or build your own maps from the game's tile sets, place monsters, NPCs and objects, and
try them straight away in the [Testing Grounds](Testing-Grounds-and-Free-Roam.md).

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
- **Save**: give the map a name; it's saved as `maps/<name>.abm` next to `abyss.ini`. **As Testing** saves it as
  `testing_grounds.abm`, which the Testing Grounds then use instead of the default arena.
- **Play**: try the map in the Testing Grounds right away. **Back** there returns to the editor with your map as you
  left it.

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

- Undo / redo
- Rotating placed units
- Saving back into the game's DS1 format
- The game's objects (chests, waypoints...) in opened areas
