# Map Editor Guide

A walk-through of making maps for Diablo II with the [Map Editor](Map-Editor.md) and putting them in the game. It
follows the order the Diablo 1 level-making guides use (the tiles first, then a room, then putting it in the game),
with Diablo II's own terms. [Map Editor](Map-Editor.md) is the reference for every key and button.

Start the editor with **Main menu › Map Editor**, or on its own:

```
AbyssEngine --editor
```

## How a Diablo II map is made

**Tiles.** A map is a grid of diamond tiles, each 160 × 80 pixels on screen. The status bar shows the tile under the
mouse. Each tile is split into 5 × 5 **sub-tiles**. Monsters, players and objects stand on sub-tiles, and whether you
can walk somewhere is decided sub-tile by sub-tile (**B** shows the blocked ones in red).

**Layers.** Every tile can hold:

- one or two **floor** pieces (ground, rugs, bridges);
- up to four **walls**: anything standing up (walls, corners, doors, pillars, trees, statues, cliffs) and roofs;
- a **shadow**.

**Tile sets.** The pieces come from the area type's tile sets (`.dt1` files): *Act 1 - Cave* has cave floors and rock
walls, *Act 2 - Desert* sand and dunes. `lvltypes.txt` says which tile sets each area type has. A map picks one area
type when you make it (**New**), and its palette comes from the act.

**Styles and sequences.** Inside a tile set, pieces are grouped by **style** (a family, like "dirt floor" or "stone
wall") and **sequence** (the variants in it). The game picks among a sequence's variants at random, weighted by their
rarity, so a big floor doesn't look tiled. The lists in the editor show them as "Left wall 0:12 (Wall.dt1)": the
kind of piece, style 0, sequence 12, and the tile set it's in.

**Kinds of piece.** A piece's kind (its *orientation* in the tile set) decides how it's drawn and joined to its
neighbours. The editor's lists name them:

| In the list | What it is |
|---|---|
| Floor | Ground |
| Left wall, Right wall | Straight walls along the diamond's two edges |
| NW corner, NE corner, SE corner, SW corner | Where two walls meet |
| Door L, Door R | A wall with a doorway in it |
| Marker | Entrances, warps and the game's marker tiles (where stairs or a cave mouth go) |
| Pillar, Tree | Free-standing |
| Roof | Drawn over the rest; **R** hides roofs |
| Shadow | Painted with the Shadow tool |

**Units.** Monsters, NPCs, objects and player starts. On a map saved as DS1, units keep their preset numbers from
`monpreset.txt` and the objects' tables, so the game places the same thing.

**Diablo 1 terms.** A Diablo 1 `.DUN` set piece is Diablo II's **preset** (`.ds1`); `.TIL` / `.MIN` megatiles are
**tile sets** (`.dt1`); a Diablo 1 level's "theme rooms" are the presets listed under an area in `lvlprest.txt`.

## Your first room

1. **New**: pick an area type, say *Act 1 - Cave*, and a size, 24 × 24. **Size** goes from 16 to 128 tiles a side.
2. **Floor tab**: pick a floor in the list and click **Fill Floor**. Paint other floors over it with the left button
   (hold and drag); the right button removes a floor.
3. **Wall tab**: pick a left wall and paint a line of tiles along the room's left edge, then right walls along the
   other edge. Put a corner piece where they meet. A Door piece leaves a doorway.
   - **H** hides the walls to see the floor under them.
   - **G** turns the grid off to see how it really looks.
4. **Shadow tab** (optional): shadows under the walls.
5. **Units tab**: pick a category (Players, NPCs, Enemies, Bosses, Others: the objects, like chests and torches),
   then a unit (*zombie1*), and click where it should stand. The right button removes the nearest unit; **Q** and
   **E** turn the one under the mouse to face another way (undo takes it back).
6. **B**: check that the room can be walked through: red sub-tiles are blocked.
7. **Play**: walk the room in the Testing Grounds. **Back** comes back to the editor with everything as you left it,
   undo history included.
8. **Save**: give it a name. It's kept as `maps/<name>.abm` next to `abyss.ini`, a plain-text format you can keep in
   git ([Map Editor](Map-Editor.md#the-abm-format)).

**Ctrl+Z** and **Ctrl+Y** undo and redo, a stroke at a time.

## Starting from the game's own maps

**Open › Game Areas (DS1)** lists all 2,276 of the game's presets: towns, dungeon rooms, set pieces, the Arcane
Sanctuary's platforms. Type in the search box to find one, for example `townN1` for the Rogue Encampment or `lutN` for
Lut Gholein. It opens with its tile sets, its layers and its monsters and objects, and you can change it like your own.

From the command line:

```
AbyssEngine --editor act1/town/townN1.ds1
```

## Putting a map in the game

A map gets into the game as a preset in a [mod](Modding.md):

1. **Save › As DS1**: the map is written in the game's own format (version 18, what the expansion's presets use) as
   `maps/<name>.ds1`.
2. Make a mod folder (`mods/my-map/`, with a `mod.ini`) and put the `.ds1` where the game expects it:
   - **To replace one of the game's maps**, use its path: an edited Rogue Encampment goes in
     `data/global/tiles/act1/town/townn1.ds1`.
   - **To add a new room to an area**, put it next to that area's other rooms
     (`data/global/tiles/act1/caves/myroom.ds1`) and add it as another file of the room's row in `lvlprest.txt` (the
     `File1` ... `File6` columns). The builder picks among a row's files at random.
3. Turn the mod on in **Options › Mods** and start the game again.

A room for a dungeon needs its exits where the area's other rooms have them. Open one of them (**Open › Game Areas**)
to see where they are, or start from a copy of one.

## Tips

- Keep the tile sets of one area type in a map. Mixing tile sets from different acts works, but their palettes
  differ.
- A unit goes into a `.ds1` only if it has a preset number in the act's tables (`monpreset.txt`, the objects'
  presets). Others are kept in `.abm` but left out of `.ds1`.
- **Keep every change in a temporary file** (Options › Gameplay › Debug, on by default) writes `editor-recovery.abm`
  after each change, and the editor offers it back if it closed with unsaved work.
