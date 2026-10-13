# Single Player

**Main menu › Single Player** opens the character list. This is the start of the real game: characters are kept
between sessions, gain experience and levels. There are no items, skills or quests yet (see
[Feature Status](Feature-Status.md)).

## Character list

| | |
|---|---|
| **Click** a character | Select it (its name shows at the top) |
| **Double click** / **OK** / **Enter** | Play it |
| **Create New** | Make a new character |
| **Delete** / **Delete key** | Delete the selected character (asks first) |
| Arrow keys, mouse wheel | Move through the list, scroll when there are more than eight |
| **Exit** / **Esc** | Back to the main menu |

Each character stands in their slot with their name, level and class, and "Expansion Character", "Hardcore" or
"Dead".

## Creating a character

The seven heroes stand round the campfire. Hover one to light them up, click to have them step forward (with their
voice line). Then type a **name** (2-15 letters, at most one `-` or `_`, not at either end; names are unique), choose
**Expansion Character** (always on for the Druid and the Assassin) and **Hardcore**, and click **OK** (or Enter). You
start in the Rogue Encampment. **Exit** goes back to the list.

Starting stats come from the game's tables (`charstats.txt`): an Amazon has 20 Strength, 25 Dexterity, 20 Vitality,
15 Energy, 50 life and 15 mana; a Barbarian 30/20/25/10, 55 life and 10 mana.

## Playing

These are the default keys; every one can be changed (and more added) in **Options › Controls**, for all classes or
for one ([Options](Options.md)). With **Modern** or **Combined** movement (Options › Gameplay) the arrow keys (or a
gamepad's left stick) walk and **E** attacks.

| | |
|---|---|
| **Left click** | Walk; click a monster to attack it; click a chest, door, shrine or waypoint to open / use it |
| **C** or **A** / Character button | Character screen |
| **R** / Run button | Run or walk (running uses stamina); hold **Left Ctrl** to switch for a moment |
| **Shift** + click | Stand still: attack where you stand instead of walking |
| **V** / Waypoint button | Travel (in town, or near a waypoint; clicking a waypoint does the same) |
| **Home** / Town button | Back to the act's town |
| **Space** | Close the open screens |
| **Print Screen** | Screenshot (saved in `screenshots` next to `abyss.ini`) |
| **Tab** | Automap: what you've seen of the area, from the game's automap pieces, with the ways out labelled |
| Walk out of town / click an entrance | On into the next area, or into a cave, tomb or dungeon (see [Walking between areas](Testing-Grounds-and-Free-Roam.md#walking-between-areas)) |
| **L** | Lighting: day, dusk, night |
| **Esc** | Close the open panel; with none open, press twice to save and exit |
| Exit Game button | Save and go back to the character list |

- **Combat** is always on. Town folk (Akara, Charsi, Gheed, ...) never fight and can't be attacked. Hits can miss:
  the game's chance to hit uses the attacker's attack rating and level against the defender's defense and level (5% to
  95%), both ways.
- **Your damage** is your class's starting weapon (Hand Axe 3-6, Javelin 1-5, Short Staff, Wand, Short Sword, Club,
  Katar) plus Strength's bonus. **Attack rating** comes from Dexterity, **defense** is Dexterity / 4.
- **Experience** for each kill comes from the game's tables (`monstats.txt` / `MonLvl.txt`, normal difficulty): a
  Zombie gives 33. Levels follow `experience.txt` (500 for level 2, 1,500 for level 3, ...).
- **Levelling up** gives 5 stat points and a skill point and fills life, mana and stamina; you hear the level-up
  sound and "New stats!" blinks until you spend them. On the character screen, click the **+** next to a stat to
  spend a point. Vitality adds life and stamina, Energy adds mana, at your class's rates.
- **Mana** refills over two minutes. **Stamina** drains while running and comes back while walking or standing; run
  out and you walk until it's back to a tenth.
- **Life** doesn't come back by itself (no potions yet): going to town fills life and mana up.
- **Dying**: you wake up in the act's town after 3 seconds with full life. **Hardcore** characters die for good: they
  stay in the list as "Dead" and can't be played.
- The game is **saved** when you travel, level up, die and exit. Saves are text files in the `save` folder next to
  `abyss.ini` (`<name>.abc`); they're not compatible with the original game's `.d2s` files.

## Not yet

Items (inventory, equipment, drops, gold), skills and the skill tree, potions and the belt, quests, NPC dialogue and
trading, hirelings, the real control panel (life/mana globes), difficulty levels. Each game has its own random world
seed: the areas keep their layout while you play.
