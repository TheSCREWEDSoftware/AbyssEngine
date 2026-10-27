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

Each character stands in their slot, in what they wear, with their name, level and class, and "Expansion
Character", "Hardcore" or "Dead".

**Difficulty**: once a character has finished Normal (Diablo dead, or Baal for expansion characters: Terror's End or
Eve of Destruction done) playing asks **Normal** or **Nightmare**, and after Nightmare **Hell** too (Esc goes back).
Monsters use the game's Nightmare and Hell numbers (`monstats.txt`'s (N) and (H) columns and `MonLvl.txt`), quests
start again for each difficulty, merchants sell better things, and your resistances take the game's penalty (-40 in
Nightmare, -100 in Hell). The difficulty shows at the top of the screen.

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
| **Tab** | Automap: what you've seen of the area, from the game's automap pieces, with the ways out labelled and waypoints, shrines and wells marked; over the whole view or small in the top right corner (Options › Gameplay › Automap) |
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
| Right skill / drop monster | `Mouse Right` | - | - | Free Roam: drop the selected monster; Single Player: the right skill (casting isn't in yet) |
| Swap weapons | - | `W` | - | Expansion: swap weapon sets |
| Skill 1 | - | `F1` | - | Over a skill in a skill picker: bind it to this key; otherwise put it on its button |
| Skill 2 | - | `F2` | - | (like Skill 1) |
| Skill 3 | - | `F3` | - | (like Skill 1) |
| Skill 4 | - | `F4` | - | (like Skill 1) |
| Skill 5 | - | `F5` | - | (like Skill 1) |
| Skill 6 | - | `F6` | - | (like Skill 1) |
| Skill 7 | - | `F7` | - | (like Skill 1) |
| Skill 8 | - | `F8` | - | (like Skill 1) |
| Skill tree | - | `T` | - | Single Player: the skill tree (spend skill points) |
| Skill speed bar | - | `S` | - | The skills bound to F1-F8 in a row above the right skill: click one to use it |
| Use belt 1 | - | `1` | - | Drink what's in belt slot 1 |
| Use belt 2 | - | `2` | - | Drink what's in belt slot 2 |
| Use belt 3 | - | `3` | - | Drink what's in belt slot 3 |
| Use belt 4 | - | `4` | - | Drink what's in belt slot 4 |
| Show belt | - | `` ` `` | - | The belt's rows above the bottom one (they show while the mouse is over the belt too) |
| Character screen | - | `A`, `C` | - | - |
| Inventory | - | `B`, `I` | - | Single Player: the inventory (items, what you wear) |
| Party screen | - | `P` | - | Multiplayer: who's in the game; invite them to your party, accept, or leave |
| Hireling screen | - | `O` | - | Single Player: the hireling's screen (as clicking their portrait) |
| Quest log | - | `Q` | - | Single Player: the quests, act by act, and where each one stands |
| Message log | - | `M` | - | The last things said to you: messages and chat |
| Help | - | `H` | - | The controls and their keys, over the game |
| Clear screen | - | `Space` | - | Close the open screens and panels |
| Show items | - | `Left Alt` | - | Show the names of items on the ground while held |
| Show portraits | - | `Z` | - | Hide or show the hireling's portrait |
| Waypoint / places | - | `V` | - | Travel by waypoint (in town or next to one); Free Roam: the Places list |
| Back to town | - | `Home` | - | Single Player: back to the act's town |
| Automap | - | `Tab` | - | Show or hide the map of the explored area |
| Center automap | - | `F9` | - | Switch the automap between the whole view and the corner |
| Fade automap | - | `F10` | - | The automap see-through, or solid |
| Party on automap | - | `F11` | - | Your party on the automap, or not |
| Names on automap | - | `F12` | - | The names of the ways out and of players on the automap, or not |
| Zoom in | `Wheel Up` | - | - | Closer (the automap, and the view in Free Roam and the Map Editor) |
| Zoom out | `Wheel Down` | - | - | Further out |
| Day / dusk / night | - | `L` | - | Cycle the light (debug) |
| Screenshot | - | `PrintScreen` | - | Saves a picture in the screenshots folder next to abyss.ini |
| Chat | - | `Return` | - | Multiplayer: type a message (Enter sends it, Esc stops) |
| Clear messages | - | `N` | - | Clear the messages and chat on the screen (the log keeps them) |
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
- **Dying**: you wake up in the act's town after 3 seconds with full life, having lost some of the experience towards
  your next level (5% of it on Normal, 10% in Nightmare, 15% in Hell; never a level) and some of your gold (10%, 15%,
  20%). What you wear and what's in your belt stay on your **corpse** where you fell: walk back to it and you take it
  all back (into the inventory or at your feet if you've put on other things since). Saving and exiting before you get
  there gives it back to you next time. **Hardcore** characters die for good: they stay in the list as "Dead" and can't
  be played.

### Monsters

- **Levels**: in a game, monsters are as strong as the area they're in, the expansion's way (`Levels.txt` MonLvl for
  the difficulty, with `MonLvl.txt`'s life, damage, defense, to-hit and experience for that level); bosses and town
  folk keep their own.
- **Champions and uniques**: about one pack in ten is special. Champions (named in blue) have three times the life,
  hit harder and faster and drop better (`TreasureClass2`). A unique (gold, named the game's way from
  `UniquePrefix.txt`, `UniqueSuffix.txt` and `UniqueAppellation.txt`: "Bane Burn the Sharp") has four times the life,
  hits harder, has one of the game's powers (extra strong, extra fast, stone skin, fire, cold or lightning
  enchanted, or aura enchanted: Might, Holy Fire, Holy Freeze or Holy Shock round it, seen on it) and comes with its
  minions; it drops from `TreasureClass3`. The super uniques the area presets place
  (Bishibosh, Rakanishu, Griswold, ...) are named and tough too, with their own treasure (`SuperUniques.txt`).
- **What they do**: archers (`monstats.txt` MissA1: the Dark Wood's corrupt archers, skeleton archers) stand back and
  shoot, stepping away when you get close; casters throw their spells now and then (Skill1/Skill2 with a missile: the
  Fallen Shamans' fire, Andariel's poison, Mephisto's bolts, novas round them), at the spell's own damage; the Fallen
  scatter for a moment when one of them dies near them; and a pack comes all together once one of it has seen you.
  Some curse you (the Succubus witches: Amplify Damage or Weaken; the Summoner: Weaken; Doom Knights: Decrepify or
  another): Amplify Damage lets more of their blows through, Weaken takes from your damage, Decrepify does a bit of
  both and slows you; it shows over your head while it lasts. Zakarum priests and imps teleport next to you, Sand
  Leapers leap at you, frog demons and tentacles go under the ground (out of reach) and come up beside you. A monster's cold slows you for a moment. In multiplayer games curses, auras and cold reach
  every player.
- **Waypoints**: you start knowing the Rogue Encampment's. Click a waypoint to light it up ("Waypoint found."); it
  then shows in the waypoint list (V in town or next to a waypoint), and travelling to it puts you next to it. Found
  waypoints are kept with the character.
- **The world**: every area joins up the way the game's do (walking between the outdoor areas, stairs, cave mouths and
  doors, and red portals to Tristram, the Arcane Sanctuary, Duriel's Lair and the hells). Indoors it's dark except for
  the light around you; outdoors it rains (snows in Act V) now and then where the game has rain; each area plays its
  own music.
- **Superior, low quality and ethereal items**: plain weapons and armor are sometimes *Superior* (one of
  `QualityItems.txt`'s improvements: damage, defense, attack rating or durability) or, below level 40, *Crude*,
  *Cracked*, *Damaged* or *Low Quality* (a quarter less damage or defense, a third less durability). Weapons and armor
  that aren't unique or set are now and then *Ethereal*: half again the damage or defense, half the durability, 10 less
  strength and dexterity needed, and they can't be repaired. Ethereal and socketed plain items are named in grey.
- **Charms** (small, large and grand) give their magic from anywhere in the inventory, like the game (not from the
  stash or the cube).
- **Two weapon sets** (expansion characters): **W** or the **I** / **II** tabs above the hands in the inventory swap
  to the other set, with its own left and right skills, like the game. Both are saved.
- **Town Portal**: right click a scroll or tome of Town Portal (in the inventory or the belt; text mode: **u** on it)
  outside town and a blue portal opens beside you. Through it you're in the act's town, by its other end; back
  through that one you're where you opened it, and it closes. A new portal closes the old one. In multiplayer games
  everyone sees it (named "<you>'s Town Portal") and your party can go through it both ways.
- **Locked chests** ("Locked Chest") take a key from your inventory (one off the stack; the Assassin opens them
  without). Some chests and other containers are **trapped** (objects.txt's TrapProb), with no sign: opening one
  strikes you with lightning, harder in later areas.
- **Shrines** do the game's shrine effects (`shrines.txt`): health and mana shrines fill you up; the others are one
  of Refilling, Armor (+100% defense), Combat (+200% attack rating and damage), the four Resist shrines (+75%), Skill
  (+2 to your skills), Mana Recharge (+400%), Stamina (running doesn't tire you), Experience (+50%), Gem (one of your
  gems a grade up, or a chipped one), Portal (a Town Portal), Exploding and Poison (a pile of exploding or gas
  potions), Fire (everything near it, you too, loses half its life) and Monster (the nearest
  monster becomes a champion), the rarer ones less often. A booster lasts its time from the table (one at a time: a
  new one replaces it) and shows in the message line. **Wells** fill life, mana and stamina. Shrines with a reset time in `shrines.txt` light up again after it (5 minutes for most boosters; Single Player); the rest, and wells, are used once.
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
(the expansion's big stash, `inventory.txt`'s "Big Bank Page 1"; classic characters have the original 6 x 4 bank,
"Bank Page 1", in its own panel). Items move between the two with the mouse like in
the inventory. Gold too: at the stash the inventory's coin button puts gold in (up to "Gold Max": 50,000 for each ten
levels), and the stash's own coin button takes it out (no more than you can carry). Walking away, closing the
inventory or opening the character screen closes it. The stash is kept with the character.

**Pages**: like the inventory, the stash has as many pages as **Options › Gameplay › Stash** says (1 to 255; a
multiplayer game sets its own), turned with the arrows under the grid (`2/5`); an item carried along can be put down on
another page. Never fewer pages than have something on them.

**Shared stash** (expansion characters): the **Personal** / **Shared** tabs under the grid switch to a stash every
character on this computer shares (softcore and hardcore characters each have their own), for passing items and gold
between your characters, with its own number of pages (**Options › Gameplay › Shared**). It's kept in
`save/shared.stash` (`shared_hc.stash`), saved as soon as it changes. Not in multiplayer games.

### Sockets, gems and runes

Weapons, shields, helms and body armor can drop with sockets (plain ones 1 in 8, magic ones 1 in 20; up to the
item's `gemsockets`, fewer below item level 26 and 40). Put a gem, rune or jewel on such an item, in the grid or worn,
and it goes into the next empty socket: a gem or rune adds its magic for that kind of item (`gems.txt`: a ruby is fire
damage in a weapon, life in a helm or armor, fire resistance in a shield), a jewel its own. Point at the item to see
its sockets and what's in them ("Socketed (3)"). Fill a plain item's sockets with the runes of a runeword in their order
(`Runes.txt`, for the item types it allows) and it becomes that runeword: the gold name ("Ancients' Pledge") and its
magic. What's in a socket stays there.

### Durability and identifying

- **Durability**: weapons and armor wear down (`weapons.txt` / `armor.txt` "durability"; stacks like javelins and what
  the table says lasts don't). A weapon loses a point now and then from blows that land, armor from blows taken (a
  random worn piece). The tooltip shows "Durability: 12 of 24" (red when it's low); the game warns when something is
  about to break. At 0 it's **broken**: tinted red, and it does nothing (a broken weapon fights like bare hands, broken
  armor gives no defense or magic) until it's repaired.
- **Repair**: the blacksmiths (Charsi, Fara, Hratli, Halbu, Larzuk) have **Repair everything** in their menu, with
  the price: each item's worth times the part worn away times `npc.txt`'s "rep mult". It covers what you wear and
  what you carry.
- **Unidentified**: magic, rare, set and unique items found on the ground come unidentified: the base's name in the
  quality's colour and "Unidentified" in red, and their magic does nothing yet (the base's damage and defense do).
  Identify them by **right clicking a Scroll of Identify** (or a Tome, which uses a charge) and then clicking the item
  (a "?" by the mouse until you do; any other click puts it away), or at **Deckard Cain** (**Identify Items**: all of
  them, for nothing; he's in the towns from Act 2 on). What merchants sell and what you gamble is identified already.

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
| Set | Green | 1 in 50, when the item is a piece of a set of the monster's level or below | A piece of one of the game's sets (`SetItems.txt`: "Civerb's Ward"), with its own properties; wearing more pieces of the set adds the piece's bonuses and the set's (`Sets.txt`: two worn, three, ..., the whole set). The tooltip says how many you wear |
| Crafted | Orange | Made in the cube (`cubemain.txt`'s crafting recipes: a magic item, a jewel, a rune and a perfect gem) | Named like a rare, the recipe's own magic and 1 to 4 random affixes (more at higher levels) |

Affixes are picked as the game picks them: the item's type must suit them (`itype` / `etype`), their level can't be
above the item's (the monster's level), and the more common ones (`frequency`) come up more. Only affixes whose every
property the engine knows are used, so what an item says is what it does; uniques keep the properties the engine
knows and leave out the rest.

| Works | Shown, but doesn't do anything yet |
|---|---|
| Strength, Dexterity, Vitality, Energy (requirements count them, like the game; the character screen shows them in blue), Life, Mana, Stamina, Attack Rating (and %), Defense, Enhanced Defense (of the item it's on), Enhanced Damage (on a weapon: its damage; elsewhere: added to strength's bonus), minimum / maximum damage, fire / cold / lightning / poison damage (added to each blow, against the monster's resistance to it; poison over two seconds), resistances, Life and Mana Stolen per Hit, Replenish Life, Faster Run/Walk, Damage Reduced, Extra Gold, Magic Find (better drops from what you kill, with the game's diminishing returns for rare, set and unique), Increased Attack Speed and Faster Hit Recovery (quicker swing and get-hit animations, diminishing like the game's), Increased Chance of Blocking, Attacker Takes Damage (and Lightning Damage), **+skills** (All Skills, a class's skills, a skill tree's, one skill: they add to skills you have a point in, the skill tree shows the total in blue; "+N to Guided Arrow" on anyone's item gives it even without a point, but only to the class it belongs to), Maximum Durability, Indestructible, sockets on uniques, Regenerate Mana, Increase Maximum Mana, Defense and Maximum Damage based on character level, Damage and Attack Rating against Undead and Demons (`monstats.txt` lUndead / hUndead / demon), Crushing Blow (a quarter of what's left of a monster's life, an eighth for bosses and uniques), Deadly Strike, Ignore Target's Defense (not on bosses and uniques), Life and Mana after each Kill, Damage Taken Goes To Mana, Damage Reduced by % (at most 50), chance to cast a skill on striking or when struck (missiles, novas, area skills and curses, free, at the item's level), auras when equipped, Requirements -N%, **charges** ("Level 3 Fire Bolt (12/20 Charges)": the skill goes on a mouse button, from the picker beside Attack, at the item's level; each use takes a charge instead of mana; repairing fills them, for a little more gold), Light Radius (the pool of light indoors and at dusk or night), Magic Damage Reduced (off each fire, cold and lightning hit, after resistance), Open Wounds (the monster bleeds for 8 seconds, more as you go up), Freezes Target (it stops, then stays slowed a second; half as long for bosses), Slows Target (at most 50%, 25% for bosses), Knockback, Hit Causes Monster to Flee (not bosses and uniques), -% Target Defense (at most half, a quarter for bosses), Cannot Be Frozen, Half Freeze Duration, Poison Length Reduced (poison on you is all at once here, so it's less of it, at most 75%), Prevent Monster Heal (shown; monsters don't heal in the engine yet) | (properties the engine doesn't know at all are left off uniques; affixes that have them don't roll) |

Uniques, set pieces and some magic affixes colour the item's picture in the inventory and on the ground, like the
game (the item's `InvTrans` colour shift from `data/global/items/Palette`, and the colour from `invtransform` /
`transformcolor`, a `colors.txt` code): Blinkbat's Form is dark red. What you wear is coloured on your figure too
(the base's `Transform` shift), and the others see it in multiplayer.

The name on the ground and the tooltip are in the quality's colour; the tooltip shows damage and defense with the
item's own magic in (blue when it changed them), then the properties in blue. Saved with the character (after an
item's other numbers, so older builds still read the save, as plain items).

### Blocking

With a shield, some blows and missiles are blocked (no damage, the game's block sounds): the shield's block plus the
class's (`charstats.txt` BlockFactor) plus Increased Chance of Blocking and skills, times (dexterity - 15) over twice
your level, at most 75%, like the game. A broken shield doesn't block.

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
    Amazon's and Barbarian's masteries), auras that pulse, curses (Weaken, Amplify Damage, ...), Teleport. Buffs,
    auras and curses show their overlays on whoever has them (`states.txt` / `overlay.txt`: Frozen Armor's shards,
    an aura's rings, the curse over a monster's head).
  - Summons (Raise Skeleton, the Valkyrie, Hydra, ...): allies that follow you, fight what's near and run out when
    their skill says so.
  - The Druid's shapes (Werewolf, Werebear: the wolf's or bear's pictures and the form's bonuses; cast it again to
    change back), Whirlwind (you spin to the spot, striking each monster on the way), Leap Attack, Charge and Dragon
    Flight (there in an instant, and the blow), Static Field (a quarter of each nearby monster's life), Telekinesis,
    Psychic Hammer, Mind Blast, Revive (a corpse fights for you for three minutes), Attract (the monster turns on its
    own kind until the skill's time is up, then it's a monster again), Bone Prison (eight bone walls round the
    monster), Taunt (it comes for you, its damage down), Double Throw (both throwing weapons at once).
  - The Assassin's traps (Charged Bolt, Lightning, Wake of Fire, Inferno and Death Sentry): stay where they're put,
    aren't attacked, and fire their missile at the nearest monster once a second until their shots (the skill's) run
    out.
  Skills that aren't in say so and cost no mana.

## Town folk

Click one of the town folk and you walk up to them; a box lists what they do:

- **Talk**: walking up, they greet you (their speech); **Talk** has them say their introduction the first time in a
  game (the one for your class when they have it), then what they know about this act's quests (the giver's first
  words, "your task is not complete...", and once it's done, their thanks; each once a game), then gossip. The words
  roll up in a box at the top as they're spoken (the game's texts and speech: `AkaraIntroGossip1`, `A1Q1InitAkara`,
  `akara_act1_intro`, ...); a click or walking away ends it. The speech needs `d2speech.mpq` (and `d2xtalk.mpq` for
  Act 5); without it they talk in text only.
- **Healers** (Akara, Fara, Ormus, Jamella, Malah) heal your life, mana and stamina (and cure poison) as you come to
  them, the game's way.
- **Trade** opens their store on the left, with the inventory on the right: tabs for armor, weapons and the rest, what
  they sell from the game's tables (each merchant's own columns in `weapons.txt`, `armor.txt` and `misc.txt`: a
  little of everything they carry, some of it magic, nothing far above your level; potions, scrolls and keys never
  run out). Point at an item to see its cost; click it to buy it (**Shift**: potions straight into the belt). Sell by
  putting an item from the mouse on the store, or right clicking it in the inventory: what they pay and charge is
  `npc.txt`'s (with its most they'll pay at each difficulty), and what you sell joins their wares. Their stock is
  kept while the game lasts. Walking away, closing the inventory or the store's close button ends it.
- **Gamble** (Gheed, Elzix, Alkor, Jamella, Nihlathak until he leaves for the Halls of Vaught once Anya is back, then Anya): the store's screen with plain-looking items from `gamble.txt`
  (the ones up to about your level; rings and amulets always), at their gamble price (`misc.txt`, `armor.txt` and
  `weapons.txt` "gamble cost", times `npc.txt`'s sell mult). What you buy turns out magic, or rare (1 in 10), set (1
  in 500) or unique (1 in 1,000), its item level within five of yours; the message says what you got. New items each
  time you choose Gamble. Once you're past their level, exceptional and elite versions come up too.
- **Repair** (the blacksmiths) and **Identify Items** (Deckard Cain): see [Durability and
  identifying](#durability-and-identifying).
- **Travel**: Warriv takes you between the Rogue Encampment and Lut Gholein, Meshif between Lut Gholein and the Kurast
  Docks, Tyrael from the Pandemonium Fortress to Harrogath. The town's waypoint is yours from then on.
- Hratli (the Kurast Docks) and Larzuk (Harrogath), whom the game places by its own code rather than the town's map,
  stand near Asheara and Malah; Anya joins Harrogath's folk once you've rescued her (Prison of Ice).
- **Hire** (Kashya, Greiz, Asheara, Qual-Kehk): five of the act's hirelings from `hireling.txt` (their kind, a level
  about yours, the price). Your hireling follows you everywhere, fights what's near (Rogue Scouts shoot fire or cold
  arrows from afar), shows top left with their life, gets a level as you get yours (staying a level or so behind),
  and is saved with you. Hiring another sends the first away. When they die, **Resurrect** brings them back for
  gold. Their skills are `hireling.txt`'s: the Desert Mercenaries' auras (Prayer, Defiance, Blessed Aim, Thorns, Holy
  Freeze, Might) work on you while they're alive and near, at their level; the Eastern Sorcerers cast their spells
  (Fire Ball, Inferno, Glacial Spike, Ice Blast, Charged Bolt, Lightning). Click their portrait (top left) for their
  screen (the game's own, with the inventory beside it): put a helm, body armor and a weapon of their kind on them
  (bows for the Rogues, polearms and spears for the Desert Mercenaries, swords for the Eastern Sorcerers and the
  Barbarians; the sorcerers take a shield too), if they meet its requirements (their strength and dexterity grow with
  their level). The weapon's damage, the armor's defense and the magic's life and resistances become theirs; it's
  saved with you, and comes back to you if you hire someone else. Their numbers show below.

## Quests

**Q** (or the mini panel's Quest Log) shows the quests on the left, act by act, with the game's names and steps from
its string tables ("Den of Evil: Kill all the monsters in the Den"): white while open, green when there's a reward
waiting, gold when done. Each difficulty has its own. The game's films come at their moments, once a character (from
`d2video.mpq` / `d2xvideo.mpq`, when you have them; not in multiplayer or text mode): Act II, III and IV's the first
time you reach the act's town, Diablo's end when he dies, and Lord of Destruction's end when Baal does. What's in:

| Quest | Done when | Reward |
|---|---|---|
| Den of Evil | every monster in the Den is dead (it counts down the last five) | Akara: a skill point |
| Sisters' Burial Grounds | Blood Raven is dead (she's put in the Burial Grounds) | Kashya: a hireling, free |
| Tools of the Trade | the Smith in the Barracks is dead | Charsi: imbues the plain weapon in your hand (or your body armor) with rare magic |
| The Search for Cain | you reach Tristram | Akara: a magic ring; Deckard Cain then waits in the Rogue Encampment (and identifies items) |
| The Forgotten Tower | the Countess is dead (put in the Tower Cellar's last level) | her own treasure class (runes) |
| Sisters to the Slaughter, Radament's Lair (Atma: a skill point), Arcane Sanctuary, The Summoner, The Seven Tombs | the boss is dead, or you're there | |
| Lam Esen's Tome | you reach the Ruined Temple | Alkor: 5 stat points |
| The Guardian, Terror's End, Hell's Forge | Mephisto, Diablo, Hephasto dead (Diablo and Hephasto are put in their areas) | |
| The Fallen Angel | Izual is dead (put in the Plains of Despair) | Tyrael: 2 skill points |
| Siege on Harrogath | Shenk the Overseer is dead (put in the Bloody Foothills) | Qual-Kehk: sockets in the plain weapon in your hand (or your body armor) |
| Prison of Ice | you reach the Frozen River | Malah: +10 to all resistances for good |
| Betrayal of Harrogath, Eve of Destruction | Nihlathak, Baal dead | |
| The Horadric Staff | the Horadric Cube (its chest in the Halls of the Dead), the Staff of Kings (its chest in the Maggot Lair) and the Amulet of the Viper put together in the cube, and the staff put in the orifice in Tal Rasha's true tomb | |
| Tainted Sun | you use the altar in the Claw Viper Temple (it gives the Amulet of the Viper) | |
| Khalim's Will | his Eye (Spider Cavern), Brain (Flayer Dungeon) and Heart (the Sewers) from their chests and his Flail from the High Council, put together in the cube; it smashes the Compelling Orb in Travincal | |
| The Golden Bird | a unique monster in Act 3 drops the Jade Figurine; Meshif turns it into the Golden Bird | Alkor: +20 life for good |
| The Blackened Temple | the three of the High Council (super uniques) dead in one game | |
| The Blade of the Old Religion | you use the Gidbinn's altar in the Flayer Jungle (it gives the Gidbinn) | Ormus, for the Gidbinn: a magic ring |
| Rescue on Mount Arreat | the five prison doors in the Frigid Highlands broken (fifteen soldiers freed) | Qual-Kehk: the runes Ral, Ort and Tal, and a hireling free |
| Rite of Passage | the Ancients (called at the Altar of the Heavens on Arreat Summit) beaten | experience: the rest of the way to your next level and half as much again |

Go back to whoever rewards a quest and their menu has the reward ("Quest: Den of Evil"). Objects the engine's
version of an area leaves out (the temple's altar, the Staff of Kings' chest, the Compelling Orb, the prisons) are put
somewhere far from where you come in. Every one of the game's quests is in, though the scripted parts are simpler than
the game's (no cut-scenes, the Ancients are called once a game, the prison doors don't need the soldiers to follow
you).

### The Horadric Cube

Right click the cube in the inventory and it opens on the left: a 3 x 4 inside (items go in and out with the mouse, like
the stash) and **Transmute**, which makes whatever recipe of the game's (`cubemain.txt`) is exactly what's inside: the
quest items, three gems or runes into the next grade (with a gem for the higher runes), potions, three rings into an
amulet (and back), rerolling a magic item with three perfect gems, magic weapons with a socket from three gems, the
prismatic amulet and garnet / cobalt / coral / jade rings, repairing with a rune, unsocketing, unique and rare weapons
and armor made exceptional or elite (their magic kept), **crafted items** (Hit Power, Blood, Caster and Safety: the
magic item can be its exceptional or elite version too). Low quality weapons and armor made normal again (Ral or
Ort and a chipped gem) are in too; the Pandemonium portals aren't yet. **The cow level**: Wirt's body in Tristram gives his leg; transmute it with a Tome of Town Portal
in the Rogue Encampment, once Baal is beaten on that difficulty, and you're taken to the Moo Moo Farm (Hell Bovines and
the Cow King). What's inside is kept with the character; any cube you carry opens it.
In multiplayer games bosses that aren't in the presets aren't put there yet.

## Text mode

`AbyssEngine --text` plays Single Player in a terminal, drawn with letters instead of pictures: no window and no sound.

```
AbyssEngine --text                 the character played last
AbyssEngine --text Texty           a saved character by name, or a new one called Texty
AbyssEngine --text Texty --class 1 a new one of that class: 0 Amazon, 1 Sorceress, 2 Necromancer, 3 Paladin,
                                   4 Barbarian (the default), 5 Druid, 6 Assassin
```

The map is around you, `@` in the middle: `#` walls, `.` ground, letters for monsters (red), town folk (yellow), other
players and items. Under it are your life, mana, gold and where you are, and the last thing that happened.

| Key | |
|-----|---|
| `w` `a` `s` `d` (or the arrow keys), `q` `e` `z` `c` | walk (the last four go diagonally) |
| `f` | fight the nearest monster |
| `r` / `l` | cast the right / left skill at the nearest monster |
| `u` | use what's next to you: talk to someone, open a chest or door, take a waypoint |
| `g` | pick up the nearest item |
| `1`-`4` | drink from the belt; in someone's menu, the choice with that number |
| `t` | go back to town |
| `i` | your items: a letter picks one, then `w` wears it, `u` uses it (potions), `x` drops it, `s` sells it (while a store is open), `p` puts it in the stash (in town), `b` in the Horadric Cube. In town the stash is listed too (`A`-`Z` takes an item out), and the cube's inside when you carry it (`1`-`9` takes one out, `0` transmutes). `i` or Esc goes back to the map |
| `Q` | save and quit |

**Trade** or **Gamble** in someone's menu shows their store as a list: a letter buys that item (a gamble turns out
what it is), `i` shows your items to sell from, Esc leaves.

It's the same game underneath (saves, levels, items and quests are shared with the normal game); the screen is drawn
ten times a second. It still reads the game's MPQs, pictures included, because the engine loads them as it goes.

## Not yet

Some skills (see above). Each game has its own random world seed: the areas keep their layout while you play.
