# Modding

Abyss Engine reads the game the way the original does, from its own files: the data tables (`.txt`), strings
(`.tbl`), maps (`.ds1`), tiles (`.dt1`), pictures (`.dc6`, `.dcc`) and sounds (`.wav`). To mod it you replace some of
those files with your own. The original MPQs stay as they are.

This works the way the Diablo community engines handle mods (DevilutionX's `mods` folder, Hellfire's data overrides): a
mod is a folder of changed files, mods stack in an order you choose, and nothing is ever written into the MPQs.

## Where a file comes from

For each file the game asks for, it looks in this order and takes the first one it finds:

1. The **mods** that are on, from the top of the list down (**Options › Mods**).
2. The **DataFolder** set in `abyss.ini` (`[General] DataFolder = ...`): one folder of loose files, handy while you
   work on something.
3. The **MPQs**, like the game: `Patch_D2.mpq`, then `d2exp.mpq`, `d2data.mpq` and the rest.

Paths are the MPQs' own, in lower case with `/`: the game's `data\global\excel\Weapons.txt` is
`data/global/excel/weapons.txt` in a mod.

## Making a mod

1. Get the originals to start from:

   ```
   AbyssEngine --extract original
   ```

   This copies every table, string table, map and tile set out of the MPQs into `original/`, keeping their paths. Add
   `--small` to keep only the tiles' walk flags (about 50 MB instead of 190).
2. Make a folder for the mod in `mods/`, next to `abyss.ini` (Options › Mods shows the full path):

   ```
   mods/
     big-axe/
       mod.ini
       data/global/excel/weapons.txt
   ```

3. Copy in only the files you change, and change them. Excel and LibreOffice open the tables. Save them as
   tab-separated text, with no quotes added round the cells.
4. Describe the mod in `mod.ini`:

   ```ini
   [Mod]
   Name = Big Axe
   Author = You
   Version = 1.0
   Description = The hand axe hits for 40-50.
   ```

   Only `Name` matters; a folder without a `mod.ini` is a mod too, named after its folder.
5. Turn it on in **Options › Mods** (select it, **On / Off**, then **OK**) and start the game again. Mods are read
   when the game starts.

**Up** and **Down** set the order: when two mods change the same file, the one higher up wins. The order is kept in
`mods/load_order.txt`, a line per mod: `+folder` for on, `-folder` for off, the first line first. You can edit it by
hand.

## Multiplayer

Everyone in a game has to play with the same mods, in the same order: the monsters, items and maps have to come out
the same in every player's game. A host turns away anyone whose mods differ and tells them which mods the game uses.
A dedicated server (`AbyssEngine --server`) uses the mods turned on next to its own `abyss.ini`.

## What you can change

| File | What it changes |
|---|---|
| `weapons.txt`, `armor.txt`, `misc.txt` | The base items: damage, defense, requirements, sizes, prices, which merchants sell them, their pictures, sounds, durability, sockets, tiers (normal / exceptional / elite) |
| `itemtypes.txt`, `belts.txt`, `gems.txt`, `runes.txt` | Item kinds and what they're equivalent to, belt sizes, gem and rune properties, runewords |
| `magicprefix.txt`, `magicsuffix.txt`, `rareprefix.txt`, `raresuffix.txt` | Magic affixes (their properties, levels, which items they go on, how often), rare names |
| `uniqueitems.txt`, `setitems.txt`, `sets.txt` | Uniques and sets: names, base items, properties, levels, rarity, colours, pictures |
| `treasureclassex.txt` | What monsters and chests drop and how often (`Picks`, `NoDrop`, the items and their chances). How often a drop is magic, rare, set or unique is the engine's own (Magic Find raises it), not these tables' quality columns yet |
| `cubemain.txt` | Horadric Cube recipes, crafted items included |
| `monstats.txt`, `monstats2.txt`, `monlvl.txt`, `superuniques.txt`, `monsounds.txt` | Monsters: life, damage, speed, experience, resistances, what they drop, their pictures and sounds, bosses |
| `levels.txt`, `lvlprest.txt`, `lvlmaze.txt`, `lvlsub.txt`, `lvltypes.txt`, `automap.txt` | Areas: which monsters, how big, which maps (presets) make them, their tile sets, waypoints, music, lighting |
| `skills.txt`, `skilldesc.txt`, `missiles.txt`, `states.txt`, `overlay.txt` | Skills: damage, mana, ranges, summons, buffs; missiles; what buffs and curses look like |
| `charstats.txt`, `experience.txt`, `hireling.txt`, `npc.txt`, `gamble.txt` | The classes, the experience table, hirelings, merchants' prices, what gamblers offer |
| `sounds.txt`, `soundenviron.txt` | Sound effects, area ambience and music |
| `data/local/lng/eng/*.tbl` | The game's text (names, quest texts, the menus' words) |
| `data/global/tiles/**/*.ds1` | The maps areas are built from: towns, dungeon rooms, set pieces. Make your own in the [Map Editor](Map-Editor.md) |
| `data/global/tiles/**/*.dt1` | Tile sets |
| `data/global/**/*.dc6`, `*.dcc` | Pictures: items, the interface, monsters, characters, missiles |
| `data/global/sfx/**/*.wav`, `data/global/music/*.wav` | Sounds and music |

Columns the engine doesn't use yet are simply ignored. A table with rows added keeps working: the engine reads every
row, by the columns' names.

## Examples

**A stronger hand axe.** In `weapons.txt` find the row whose `code` is `hax` and set `mindam` to `40` and `maxdam` to
`50`. That's the whole mod.

**More drops.** In `treasureclassex.txt`, a row's `Picks` is how many times it rolls and `NoDrop` how often a roll
gives nothing. Raise `Picks` or lower `NoDrop` on the rows monsters use (`monstats.txt`'s `TreasureClass` columns
name them).

**A new unique item.** Add a row to `uniqueitems.txt`: `index` (its name), `enabled` 1, `lvl` (the level it can drop
from), `code` (its base item), `rarity`, and up to twelve `prop` / `par` / `min` / `max` columns, using the property codes the engine knows
([Single Player](Single-Player.md) lists them; a unique keeps the ones it knows and leaves out the rest).

**Your own town.** Open the Rogue Encampment in the [Map Editor](Map-Editor.md) (`AbyssEngine --editor
act1/town/townN1.ds1`), change it, **Save › As DS1** and put the file in your mod as
`data/global/tiles/act1/town/townn1.ds1`. The camp has a few layouts (`townn1`, `towne1`, `townw1`, ...; the world
seed picks one): change each, or only the one you're testing with.

**A new dungeon room.** Save a map from the editor as DS1, put it in the mod next to the area's own rooms, and add its
name as another `File` of that room's row in `lvlprest.txt`. The builder picks among a row's files at random.

## Coming from Diablo 1

| Diablo 1 | Diablo II (here) |
|---|---|
| `.DUN` set pieces and quest maps | `.ds1` presets (`lvlprest.txt` says which areas use them) |
| `.TIL` / `.MIN` megatiles | `.dt1` tile sets (a tile is one 160 × 80 diamond, split into 5 × 5 sub-tiles for walking) |
| `.CEL` / `.CL2` pictures | `.dc6` (interface, items) / `.dcc` (monsters, characters, missiles) |
| `monstdat`, `itemdat`, `spelldat` in the program | Data tables you can edit (`monstats.txt`, `weapons.txt`, `skills.txt`, ...) |
| DevilutionX `mods/` | `mods/` here, the same idea |

## Running from a folder alone

Text mode (`AbyssEngine --text`) can run with no MPQs at all: `AbyssEngine --extract DIR --small`, then
`DataFolder = DIR` in `abyss.ini`. The window version still needs the MPQs for its pictures and sounds.
