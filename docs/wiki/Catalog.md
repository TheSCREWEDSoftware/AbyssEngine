# Catalog

**Main menu › Catalog** (needs the `DEBUG_SHOW_CATALOG` [option](CMake-Options.md), on by default).

A browser for the game's own data and graphics, read straight from the MPQs.

## Tabs

| Tab | What it shows |
|-----|---------------|
| **Weapons** | Every weapon with its picture, damage, requirements, sockets, quality level, size and code. |
| **Armor** | Every armor piece: defense, requirements, sockets and so on. |
| **Classes** | The seven classes, their starting stats, and their three **skill trees** (click a skill for its description). |
| **Sprites** | Every DC6 image in the game, grouped into Items / Skills / Menus / UI / Fonts / Other. Step through frames and directions, play animations, try other palettes, change the speed. |
| **Bestiary** | Players, NPCs, Enemies, Bosses and Others (objects), animated, in every direction and animation mode (walk, attack, death, ...). |
| **Uniques** | Every unique (`UniqueItems.txt`), set item (`SetItems.txt`) and runeword (`Runes.txt`): its base item and picture (or its runes), required level and properties with their ranges ("+60-70% Enhanced Damage"), as the engine words them. **All Types** picks Unique, Set or Runeword. Properties the engine doesn't know yet aren't listed. |

## Controls

- **Search box**: type to filter the list (on Weapons and Armor it also matches the item type, e.g. "axe").
- **Weapons and Armor filters** (buttons under the detail panel; click to cycle):
  - **Type**: Axe, Wand, Claw, Amazon Spear, Helm, ...
  - **Tier**: Normal / Exceptional / Elite
  - **Sort**: Name, Level, Required Level, Damage (Defense for armor), Strength
- **Scrollbar**: drag it or click the track; the mouse wheel works too.
- **Bottom / Top** (next to Back): jump to the end or the start of the list.
- **Backdrop** (top right): change the panels' background colour if something is hard to see.
- **Speed** (Sprites, Bestiary): slow animations down or speed them up.
