# Single Player

**Main menu › Single Player** opens the character list. This is the start of the real game: characters are kept
between sessions, gain experience and levels, carry, wear and drop items and learn and cast skills. There are no quests yet (see [Feature Status](Feature-Status.md)).

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
15 Energy, 50 life and 15 mana; a Barbarian 30/20/25/10, 55 life and 10 mana. So do the starting items: the class's
weapon (and a buckler for most), four healing potions in the belt, and a Town Portal and an Identify scroll. The
Sorceress starts with a point in Fire Bolt and the Necromancer in Raise Skeleton (`charstats.txt` StartSkill), ready
on the right mouse button.

## Playing

These are the default keys; every one can be changed (and more added) in **Options › Controls**, for all classes or
for one ([Options](Options.md)). With **Modern** or **Combined** movement (Options › Gameplay) the arrow keys (or a
gamepad's left stick) walk and **E** attacks.

The bottom of the screen is the game's own **control panel**: the red life globe and blue mana globe (point at them for
the numbers), the left and right skills, the run/walk button, the stamina bar (red when you're out of breath) with the
experience bar over it, the belt's four slots, and the arrow in the middle that opens the **mini panel** (Character,
Inventory, Skill Tree, Party, Automap, Message Log, Quest Log, Game Menu; the ones the engine doesn't do yet say so).
When you have stat points to spend, the **New Stats** button lights up on the left; with skill points, **New Skills**
on the right. **Save and Exit Game** is in the game menu (**Esc**).

**Options › Video › Control panel › Modern** lays it out like the remaster: the globes on their holders, and between
them the experience bar along the top, the run button and stamina bar, then the mini panel's buttons (always shown),
the left skill, a spot for dropping items on the ground (between the skills), the right skill and the belt.

| | |
|---|---|
| **Left click** | Walk; click a monster to attack it; click a chest, door, shrine or waypoint to open / use it |
| **C** or **A** / mini panel's Character button | Character screen |
| **I** or **B** / mini panel's Inventory button | Inventory (see [Items](#items)) |
| **T** / mini panel's Skill Tree button / **New Skills** | Skill tree (see [Skills](#skills)) |
| **1**-**4** / right click a belt slot | Drink what's in that belt slot |
| Click the left or right skill on the control panel | Choose what that mouse button uses |
| **R** / the run button on the control panel | Run or walk (running uses stamina); hold **Left Ctrl** to switch for a moment |
| **Shift** + click | Stand still: attack where you stand instead of walking |
| **V** | Travel by waypoint (in town, or near a waypoint; clicking a waypoint does the same): the list has the waypoints you've found (click one to light it up and add it), and you come out next to the one you picked. With **Options › Debug › Free Roam in Single Player and Multiplayer**: from anywhere, and **CapsLock** sprints ([Free Roam](Testing-Grounds-and-Free-Roam.md#in-single-player-and-multiplayer)) |
| **Home** | Back to the act's town |
| **Space** | Close the open screens |
| **Print Screen** | Screenshot (saved in `screenshots` next to `abyss.ini`) |
| **Tab** | Automap: what you've seen of the area, from the game's automap pieces, with the ways out labelled and waypoints, shrines and wells marked |
| Walk out of town / click an entrance | On into the next area, or into a cave, tomb or dungeon (see [Walking between areas](Testing-Grounds-and-Free-Roam.md#walking-between-areas)) |
| **L** | Lighting: day, dusk, night |
| **Esc** | Close the open panel; with none open, the game menu: **Options**, **Save and Exit Game**, **Return to Game** (the world waits while it's open) |
| **Tab** / mini panel's Automap button | Automap |

### Every key, from the engine

<!-- keys:begin (generated: AbyssEngine --export-controls DIR --defaults, then keys-single-player.md) -->
| Action | Mouse | Keyboard | Controller | What it does |
|---|---|---|---|---|
| Move / attack / use | `Mouse Left` | - | - | Classic: walk where you click, attack what you click, open what you click |
| Move up | - | `Up` | `Pad Left Stick Up` | Modern and Combined movement: walk towards the top of the screen |
| Move down | - | `Down` | `Pad Left Stick Down` | Modern and Combined movement |
| Move left | - | `Left` | `Pad Left Stick Left` | Modern and Combined movement |
| Move right | - | `Right` | `Pad Left Stick Right` | Modern and Combined movement |
| Run / walk | - | `R` | - | Switch between running and walking |
| Run (hold) | - | `Left Ctrl` | - | Run (or walk, when running is on) while held |
| Stand still | - | `Left Shift`, `Right Shift` | - | Attack without walking; clicks don't move you |
| Attack | - | `E` | `Pad Button 1 (Left)` | Modern and Combined movement: attack what's under the mouse, or the nearest monster |
| Right skill / drop monster | `Mouse Right` | - | - | Free Roam: drop the selected monster; Single Player: the right skill, cast where the cursor is (held: again and again) |
| Skill tree | - | `T` | - | Single Player: the skill tree (spend skill points) |
| Use belt 1 | - | `1` | - | Drink what's in belt slot 1 |
| Use belt 2 | - | `2` | - | Drink what's in belt slot 2 |
| Use belt 3 | - | `3` | - | Drink what's in belt slot 3 |
| Use belt 4 | - | `4` | - | Drink what's in belt slot 4 |
| Character screen | - | `A`, `C` | - | - |
| Inventory | - | `B`, `I` | - | Single Player: the inventory (items, what you wear) |
| Party screen | - | `P` | - | Multiplayer: who's in the game; invite them to your party, accept, or leave |
| Clear screen | - | `Space` | - | Close the open screens and panels |
| Show items | - | `Left Alt` | - | Show the names of items on the ground while held |
| Waypoint / places | - | `V` | - | Travel by waypoint (in town or next to one); Free Roam: the Places list |
| Back to town | - | `Home` | - | Single Player: back to the act's town |
| Automap | - | `Tab` | - | Show or hide the map of the explored area |
| Zoom in | `Wheel Up` | - | - | Closer (the automap, and the view in Free Roam and the Map Editor) |
| Zoom out | `Wheel Down` | - | - | Further out |
| Day / dusk / night | - | `L` | - | Cycle the light (debug) |
| Screenshot | - | `PrintScreen` | - | Saves a picture in the screenshots folder next to abyss.ini |
| Chat | - | `Return` | - | Multiplayer: type a message (Enter sends it, Esc stops) |
<!-- keys:end -->

- **Combat** is always on. Town folk (Akara, Charsi, Gheed, ...) never fight and can't be attacked. Hits can miss:
  the game's chance to hit uses the attacker's attack rating and level against the defender's defense and level (5% to
  95%), both ways.
- **Your damage** is the weapon you hold (bare hands: 1-2; two-handed weapons such as staves use their two-hand
  damage) plus Strength's bonus (and Dexterity's, for weapons that have one). **Attack rating** comes from Dexterity,
  **defense** is Dexterity / 4 plus what you wear.
- **Experience** for each kill comes from the game's tables (`monstats.txt` / `MonLvl.txt`, normal difficulty): a
  Zombie gives 33. Levels follow `experience.txt` (500 for level 2, 1,500 for level 3, ...).
- **Levelling up** gives 5 stat points and a skill point and fills life, mana and stamina; you hear the level-up
  sound and "New stats!" blinks until you spend them. On the character screen, click the **+** next to a stat to
  spend a point. Vitality adds life and stamina, Energy adds mana, at your class's rates.
- **Mana** refills over two minutes. **Stamina** drains while running and comes back while walking or standing; run
  out and you walk until it's back to a tenth.
- **Life** doesn't come back by itself: drink a healing potion (it heals over a few seconds, like the game's: a
  Lesser Healing Potion gives 30 life over 8 seconds), or go to town, which fills life and mana up.
- **Dying**: you wake up in the act's town after 3 seconds with full life. **Hardcore** characters die for good: they
  stay in the list as "Dead" and can't be played.
- **Waypoints**: you start knowing the Rogue Encampment's. Click a waypoint to light it up ("Waypoint found."); it
  then shows in the waypoint list (V in town or next to a waypoint), and travelling to it puts you next to it. Found
  waypoints are kept with the character.
- **The world**: every area joins up the way the game's do (walking between the outdoor areas, stairs, cave mouths and
  doors, and red portals to Tristram, the Arcane Sanctuary, Duriel's Lair and the hells). Indoors it's dark except for
  the light around you; outdoors it rains (snows in Act V) now and then where the game has rain; each area plays its
  own music.
- The game is **saved** when you travel, level up, die and exit. Saves are text files in the `save` folder next to
  `abyss.ini` (`<name>.abc`); they're not compatible with the original game's `.d2s` files.

## Items

**I** (or **B**, or the mini panel) opens the game's inventory screen on the right, laid out from the game's
`inventory.txt`: what you wear around the outline (helm, amulet, armor, the two hands, rings, belt, gloves, boots) and
the 10 x 4 grid below, with as many pages as **Options › Gameplay › Inventory pages** says (1 to 255; arrows and the
page number under the grid, next to the gold). The square button in its bottom corner closes it (the skill tree has the same one), as do
**I**, **Space** and **Esc**.

- **Click** an item to pick it up; it follows the mouse. **Click** again to put it down: in the grid (a green patch
  shows where), or in a slot it suits. Putting it on another item swaps them. Close the inventory with an item on the
  mouse and it goes back where it came from (or anywhere it fits; with no room at all it stays on the mouse).
- **Picking up what you wear**: **Classic** (the game's way) takes it off at once; **Modern** (Options › Gameplay ›
  Worn gear) leaves it worn, faded in its slot, until you put it in the grid or another slot.
- **Wearing**: an item goes only where it suits (a helm on the head, a shield in a hand, ...) and only if you meet its
  requirements; ones you can't use yet are tinted red, with their unmet requirements in red. A two-handed weapon needs
  both hands; only Barbarians hold a weapon in each hand (Assassins: two claws).
- **Pointing at an item** shows what the game shows: its name, damage or defense, block, belt size, quantity and
  requirements.
- **The belt**: potions and scrolls, its bottom row on the control panel (keys **1**-**4**). Without a belt it holds
  one row of four; a sash two rows, a belt three, a girdle four. **Shift + click** a potion in the grid to put it in
  the belt; **right click** a potion (in the grid or the belt) to drink it.
- **On the character**: what you wear shows on your figure, the game's way: the helm, light, medium or heavy body
  armor (its arms, legs and shoulders from `armor.txt`), the weapon in hand (with that weapon's animations: a bow is
  held in the left hand, a staff in both, a Barbarian's sword in both when there's no shield) and the shield. The
  character list shows it too, and in multiplayer the others see it.
- **Gold**: the coin button left of your gold drops some at your feet (it asks how much: type it, or the mouse wheel and
  the arrow keys; Enter drops it).
- Everything is saved with the character: the grid, what's worn, the belt, the stash and gold. Characters saved
  before items existed get their class's starting items. An item on the mouse is saved where it came from.

### The stash

Each town's stash (the chest by the camp) opens your stash on the left, with the inventory on the right: a 6 x 8 grid
(the expansion's big stash, `inventory.txt`'s "Big Bank Page 1"). Items move between the two with the mouse like in
the inventory. Gold too: at the stash the inventory's coin button puts gold in (up to "Gold Max": 50,000 for each ten
levels), and the stash's own coin button takes it out (no more than you can carry). Walking away, closing the
inventory or opening the character screen closes it. The stash is kept with the character.

### Sockets, gems and runes

Weapons, shields, helms and body armor can drop with sockets (plain ones 1 in 8, magic ones 1 in 20; up to the
item's `gemsockets`, fewer below item level 26 and 40). Put a gem, rune or jewel on such an item, in the grid or worn,
and it goes into the next empty socket: a gem or rune adds its magic for that kind of item (`gems.txt`: a ruby is fire
damage in a weapon, life in a helm or armor, fire resistance in a shield), a jewel its own. Point at the item to see
its sockets and what's in them ("Socketed (3)"). Fill a plain item's sockets with the runes of a runeword in their order
(`Runes.txt`, for the item types it allows) and it becomes that runeword: the gold name ("Ancients' Pledge") and its
magic. What's in a socket stays there.

### On the ground

- **Dropping**: with an item on the mouse, **click anywhere in the world** (or the Modern panel's drop spot between
  the skills). It flips out at your feet, onto the nearest free spot, with the game's picture and sounds. Modern
  pick-up: a worn item comes off as it's dropped.
- **Names**: point at an item to see its name; hold **Show items** (**Left Alt**) to see every name around (they stack
  up when they'd overlap). Click a name or the item itself.
- **Picking up**: click it and you walk over and pick it up: potions into the belt if there's room, the rest into the
  inventory ("There's no room for that" when it's full). With the inventory open it goes onto the mouse instead.
  Modern movement picks up only what's in reach.
- Like the game, what's on the ground stays there while the game lasts (each area keeps its own), and is gone when you
  leave the game. In multiplayer everyone sees what's dropped, and the first to get to an item has it.

### Drops and gold

- **Monsters drop things** when they die, the game's way: each names a treasure class (`monstats.txt`
  TreasureClass1, e.g. "Act 1 H2H A") and `TreasureClassEx.txt` says what it picks, maybe nothing: gold, potions,
  scrolls, keys, arrows, weapons and armor of about the monster's level (the game's own "weap3", "armo6" groups,
  from the items' levels). Up to six things a monster. Weapons, armor, rings, amulets and jewels can come out
  [magic, rare or unique](#magic-rare-and-unique-items), some with [sockets](#sockets-gems-and-runes).
- **Chests, caskets, urns, barrels, crates, corpses and racks** drop the act's chest treasure ("Act 1 Chest A") when
  opened.
- **Gold** shows as "35 Gold" on the ground and goes straight to your gold when picked up; you can carry 10,000 a
  level ("You can't carry any more gold"). Extra Gold on what you wear adds to it.

### Magic, rare and unique items

Drops are rolled the way the game words them, from its own tables:

| Quality | Colour | How often | What it has |
|---|---|---|---|
| Magic | Blue | 1 in 4 weapons and armor; rings, amulets and jewels always (at least) | A prefix and/or a suffix from `MagicPrefix.txt` / `MagicSuffix.txt` ("Jagged Hand Axe of the Fox") |
| Rare | Yellow | 1 in 20 | A made-up name (`RarePrefix.txt` + `RareSuffix.txt`: "Grim Bite") and 3 to 6 affixes (only the ones the game allows on rares, never two of the same kind) |
| Unique | Gold | 1 in 100, when the item has a unique of the monster's level or below | One of `UniqueItems.txt`'s named items ("The Gnasher"), with its own picture and properties |

Affixes are picked as the game picks them: the item's type must suit them (`itype` / `etype`), their level can't be
above the item's (the monster's level), and the more common ones (`frequency`) come up more. Only affixes whose every
property the engine knows are used, so what an item says is what it does; uniques keep the properties the engine
knows and leave out the rest (skills, charges, sockets).

| Works | Shown, but doesn't do anything yet |
|---|---|
| Strength, Dexterity, Vitality, Energy (requirements count them, like the game; the character screen shows them in blue), Life, Mana, Stamina, Attack Rating (and %), Defense, Enhanced Defense (of the item it's on), Enhanced Damage (on a weapon: its damage; elsewhere: added to strength's bonus), minimum / maximum damage, fire / cold / lightning / poison damage (added to each blow, against the monster's resistance to it; poison all at once for now), resistances, Life and Mana Stolen per Hit, Replenish Life, Faster Run/Walk, Damage Reduced, Extra Gold | Magic Find, attack speed, hit recovery, block, light radius, thorns, magic damage reduced |

The name on the ground and the tooltip are in the quality's colour; the tooltip shows damage and defense with the
item's own magic in (blue when it changed them), then the properties in blue. Saved with the character (after an
item's other numbers, so older builds still read the save, as plain items).

### Resistances

The character screen shows Fire, Cold, Lightning and Poison resistance (blue when boosted, red below 0): what's worn
(each resistance, and All Resistances), at most 75%, less the difficulty's penalty in a multiplayer game (Nightmare 40,
Hell 100, like the game). Monsters whose first attack has fire, cold, lightning or poison in it (`monstats.txt` El1,
e.g. Andariel's poison) hit with that too, less your resistance. Monsters resist too (`monstats.txt` ResDm, ResFi,
ResCo, ResLi, ResPo, the difficulty's own in multiplayer): your physical and elemental damage are each cut by theirs,
and an immune monster (100% or more) takes none of that kind. Life and mana stolen come from the physical part.

## Skills

**T** (or the mini panel, or **New Skills**) opens the class's skill tree on the right: its three trees on tabs, with
"Skill Choices Remaining" above them.

- **Click** a skill to put a point in it. The game's rules: your level must be at least the skill's required level
  plus the points already in it, every skill it needs has a point, and 20 is the most. Skills you can't learn yet are
  dimmed; point at one to see why, and its description, level and mana cost.
- **Left and right skill**: click the skill on either side of the control panel to pick from Attack and the skills you
  know (passives can't be picked; the left button takes only the skills the game allows there).
- **Casting**: hold the right mouse button to use the right skill where the cursor is (again and again while you
  hold it); the left skill is used when you click a monster, or anywhere with **Shift**. Skills cost mana
  (`skills.txt`, less for each level) and work from the game's own numbers: damage by skill level, the missiles from
  `Missiles.txt` with their pictures and glow, synergies and the calcs the game uses. What's in:
  - Missiles (Fire Bolt, Fire Ball, Ice Bolt, Charged Bolt with its scatter, Bone Spear, Teeth, Holy Bolt, Blessed
    Hammer, Multiple Shot's fan, Lightning Fury, ...), with bursts over an area, chaining and piercing.
  - Novas (Frost Nova, Nova, Poison Nova), area skills (Blizzard, Meteor, Corpse Explosion), Frozen Orb's shards,
    Glacial Spike and Ice Blast's freeze, chill and poison over time.
  - Weapon skills (Bash, Jab, Critical Strike, ...): the skill's attack rating and damage bonus on the next blow.
  - Buffs and warcries (Shout, Battle Orders, Bone Armor that soaks up damage, Thorns), passives (Dodge, the
    Amazon's and Barbarian's masteries), auras that pulse, curses (Weaken, Amplify Damage, ...), Teleport.
  - Summons (Raise Skeleton, the Valkyrie, Hydra, ...): allies that follow you, fight what's near and run out when
    their skill says so.
  Not yet: the Druid's and Assassin's shapes and traps, Whirlwind and leaps, some rarer skills (they say so).

## Not yet

Some skills (see above), set items, quests, NPC dialogue and trading, hirelings, difficulty levels. Each game has its own random world seed: the
areas keep their layout while you play.
