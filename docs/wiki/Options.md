# Options

**Options** on the main menu (or in the game: **Esc** › Options) has seven tabs: Video, Audio, Gameplay, Controls, Multiplayer, Mods and Debug. **OK** saves, **Cancel** (or **Esc**)
puts everything back as it was, **Defaults** resets the tab you're on. Everything except the window size shows
straight away, in the game too (Esc › Options while playing), so you can see or feel a change before keeping it.

Every slider shows its lowest and highest value under its ends and has a box beside it to type the value in (Esc in
the box clears it rather than closing Options).

Video, audio and the mouse are saved in `abyss.ini` (only those lines change: the rest of the file, comments too, stays as you
wrote it); gameplay settings and controls in **`options.ini`** next to it. Both are plain text with a comment on every
setting, so they can be edited outside the game too.

**Before starting the game:** `AbyssEngine --settings` opens just the Options screen (OK or Cancel closes the
program), for setting the resolution, volumes and controls first. `AbyssEngine --export-controls DIR` writes the
controls files below into a folder without opening a window (add `--defaults` for the default bindings).

## The Options screen

| Tab | |
|---|---|
| **Video** | Window size (800x600 up to 4K, or "Follow InitialScale"), full screen, scaling (sharp pixels / smooth), frame rate, brightness, contrast and gamma (below). The game keeps its 4:3 shape: wider windows get black bars |
| **Audio** | Master, music, sound effects, interface and speech volume, 0 to 100; you hear it right away (speech is kept for NPCs and quests, once the engine plays them; `SpeechVolume` in `abyss.ini`) |
| **Gameplay** | Settings for all classes or one class (the highlighted **Settings for** band at the top; tick "Own settings" to give a class its own), type of movement, entrances, see-through obstructions, even walking speed on screen |
| **Controls** | Every action and what it's bound to (see below), and the **Pointer** window (mouse and controller) |
| **Multiplayer** | Names and circles over the other players, your colour, the lobby server ([Multiplayer](Multiplayer.md#options--multiplayer)) |
| **Mods** | The [mods](Modding.md) in the `mods` folder: **On / Off** turns the selected one on or off, **Up** and **Down** move it (higher up wins when two change the same file). Kept in `mods/load_order.txt`; they're read when the game starts, so start it again after a change |
| **Debug** | The developer tools, the same for every class (below) |

**Video** (`[Graphics]` in `abyss.ini`):

| Setting | Values | |
|---|---|---|
| `FrameRate` | **Original**: `25` (the game's own) · **Modern**: `60` (the default) · or the slider: Slowest `10`, Slower `15`, Slow `20`, Medium `30`, Fast `45`, Faster `60`, Fastest `unlimited` | Frames drawn a second at most. The game runs at the same speed whatever you pick; only how smooth it looks changes. Multiplayer never goes above 60 |
| `ControlPanel` | `Original` / `Modern` | Single Player's control panel. **Original**: the game's. **Modern**, laid out like the remaster's: the globes on their holders; between them the experience bar, the run button and stamina, then the mini panel (always shown), the left skill, a spot to drop items on the ground (once there are items on the ground), the right skill and the belt. Changes straight away; the default comes from the build's `DEFAULT_STYLE` |
| `Brightness`, `Contrast`, `Gamma` | `0` to `100`, `50` = the picture as the game made it | Each frame is drawn to an 800x600 picture first and adjusted on the graphics card as it goes on the window (scaling happens there too), so changes show straight away. Gamma brightens or darkens the dark and middle shades and leaves black and white alone |

**Pointer** (Controls › **Pointer**, the *Mouse and controller* window, `[Input]` in `abyss.ini`):

| Setting | Values | |
|---|---|---|
| `MatchAxes` | `true` / `false` | One pointer speed for both directions (one slider), or across and up/down apart (default on) |
| `MouseSpeedX`, `MouseSpeedY` | `25` to `300` (shown as `0.25x` to `3.00x`) | Pointer speed, `100` (`1.00x`) = the system's own. At any other speed the game moves its own pointer from the mouse's movement, so the mouse stays in the window while it has the focus (Alt+Tab to leave) |
| `WheelSpeed` | `25` to `400` (`0.25x` to `4.00x`) | How far each wheel notch scrolls lists and zooms the Map Editor; fine-stepping wheels and touchpads add up their part steps |
| `PadMenus` | `Console` (default) / `Cursor` | How a controller works the menus and panels (see **Controller** below). **Console**, like Diablo III and the remaster on a controller: the D-pad or left stick jumps from one button, box, tab or row to the next, a cell at a time in the inventory and stash, framed in gold. **Cursor**: the left stick moves the pointer |
| `PadPointerSpeed` | `25` to `300` (`0.25x` to `3.00x`) | How fast a stick moves the pointer (the right stick, and the left one with `Cursor`) |

**Pad Layout** in the same window adds the controller's default buttons (below) to every action that hasn't got them,
for an `options.ini` saved before they were there; **Default** puts the pointer, wheel and controller settings back.

### Controller

Any controller SDL knows (Xbox, PlayStation, Switch Pro and the many that copy them) works the whole game, menus
included, without a mouse. Moving the mouse or pressing a key hands control back to them; touching the controller takes
it again.

**In the menus and panels** (the main menu, Options, character screens, the inventory, stash, cube, stores, trade,
skill tree, quest log, the game menu, ...) the controller moves the pointer and clicks for you, so every screen works
as it does with the mouse:

| Button | Does |
|---|---|
| D-pad, left stick | **Console**: the next button, box, tab or list row that way (held, it repeats); in an item grid one cell; on a slider, left / right change its value; a list scrolls at its top and bottom. **Cursor**: the left stick moves the pointer |
| Right stick | Moves the pointer freely (either style) |
| A | Click (held: drag); picks up and puts down items |
| X | Right click (use an item, buy, sell) |
| B, Start | Back (Esc): closes the open panel or window; Start in play opens the game menu |
| LB / RB | The previous / next tab: Options' tabs, the store's tabs, the skill trees, the quest log's acts |

When the pointer comes off the game onto a panel it lands on the panel's grid (the inventory or stash), or the nearest
thing on it. A box to type in (a name, chat, a lobby address) brings up a **keyboard on the screen**: the D-pad moves on
it, A types, X deletes, Y switches capitals, B or Start (or **Done**) finishes; A on the box again brings it back.

**In play** the controller goes to the controls' bindings (the defaults, like Diablo III and the remaster):

| Button | Default action |
|---|---|
| Left stick | Walk (whatever the movement type) |
| A | Move / attack / use: what's under the aim; with nothing near, a swing where you stand |
| X | Attack |
| B, Right trigger | Right skill |
| Y | Inventory |
| Back (Select) | Character screen |
| LB / RB | Previous / next skill: the right skill steps through the skills on the skill keys (F1-F8) |
| Left trigger | Automap |
| D-pad | Belt 1-4 (left, up, right, down) |
| Left stick press | Run / walk |
| Right stick press | Skill tree |
| Start | The game menu |

The **aim** follows the left stick: the pointer goes onto the nearest monster, chest, door, person to talk to or item
on the ground the way you're facing (close by), or a little ahead of you when there's nothing. Moving the right stick
aims by hand for a moment and a half.

In **Configure action** a controller's buttons are bound, not used to move about: hold **Start** a second for **OK**,
or **B** to cancel (the held button isn't kept).

**Input colours** (in the list and in the Configure window):

| Colour | Means | |
|---|---|---|
| White | Only this action uses the input | Fine |
| **Orange** | Another action uses it too, but the two never work at the same time: each action works in Single Player, in Free Roam / the Testing Grounds, or in both, and these two don't share one (Single Player's **C**, character screen, and Free Roam's **C**, combat) | Fine: whichever is active fires |
| **Red** | Another action uses it and both work at the same time | A clash: only the first in the list fires; change one of them |

The coloured stripe at the start of each row is something else: the action's group (Movement blue, Combat and skills
red, Belt yellow, Screens green, Automap teal, View purple, Chat grey, Free Roam orange), the same colours as the
**Show** list.

**Controls tab**, the ArmA way:

- **Show:** picks a group (Movement, Combat and skills, Belt, Screens, Automap, View, Chat, Free Roam), each with its
  colour in the list. A coloured bar on each row is its group; with **Everything** each group starts under a heading
  in its colour. Actions marked `*` aren't in the engine yet. Scroll with the wheel, or click or drag the scrollbar.
- Inputs are coloured by whether something else uses them too (see **Input colours** above). Point at an action to
  see what it does.
- **Double click** an action (or click its inputs) to change them, in the **Configure action** window: press any key
  or gamepad button to add it (Ctrl / Shift / Alt held make a combination; a modifier on its own is added when you let
  go of it; the same key twice quickly makes it a double tap, "2x K"; gamepad buttons and stick directions are added
  the same way). The list on the right is only for what you can't press there: mouse buttons, the wheel, double
  clicks and mouse / wheel with Ctrl, Shift or Alt. Select one and **Delete** it, **Default** puts the
  game's back, **Undo** what you did in the window, **Previous** / **Next** go through the actions, **OK** / **Cancel**
  (or **Esc**).
- **Export** writes the controls for reading outside the game, next to `abyss.ini`: `controls.md` (a table per group:
  action, mouse, keyboard and controller inputs, clashes, and a note under it naming what isn't implemented yet,
  with the pictures folded away above it), a keyboard coloured by group
  with the actions on each key (`controls-keyboard.svg`), and a gamepad (a generic pad) with what
  each trigger, bumper, stick direction, D-pad direction and button does (`controls-gamepad.svg`), each also in a dark version (`-dark.svg`). The
  defaults are on the [Controls](Controls.md) page.
- **Presets** saves the controls you're setting up under a name, loads one back, or deletes one. Each preset is a file
  in the `presets` folder next to `abyss.ini`, in the same `action = inputs` lines as `options.ini`, so they can be
  shared or edited by hand.
- **Copy to** gives a class a copy of the controls you're looking at (all classes', or another class's), as its own.
- The help line under the game view is built from your bindings, so it always shows your keys.

## Gameplay

| Setting | Values | |
|---|---|---|
| `Movement` | `Original` | Diablo II: click to walk, click a monster to attack it, click a chest or door to open it |
| | `Modern` | Movement keys (arrows by default, or a gamepad's left stick) walk; the mouse attacks and opens what's in reach without walking; the attack key (**E**) hits what's under the mouse or the nearest monster in reach. Like Diablo III / IV |
| | `Combined` | Both: movement keys or clicks walk, clicks or the attack key attack |
| `SeeThroughObstructions` | `true` / `false` | Walls, trees, tents and roofs between you and the camera get a round, soft-edged see-through hole around you where they really cover you (their pixels, not their box); walls behind you never fade. It opens and closes smoothly |
| `ConfirmEntrances` | `true` / `false` | Ask before going through a cave mouth, tomb door or stairs ("To The Den of Evil?": Go in / Stay). Off by default |
| `Automap` | `Full` / `Corner` | The automap (Tab): **Full** over the whole view, like the game; **Corner** half size in a box at the top right, without the exits' names (Automap, beside Worn gear) |
| `WornGearPickup` | `Original` / `Modern` | Picking up something you wear in the inventory. **Original** (the game's): it comes off at once. **Modern**: it stays worn (faded in its slot, still counting) until you put it in the grid or another slot; put back in its own slot or closing the inventory, nothing changes. Default from the build's `DEFAULT_STYLE` |
| `InventoryPages` | `1` to `255` | Pages of the inventory grid in Single Player (a multiplayer game sets its own). Arrows under the grid turn the page; an item carried along can be put down on another page. Never fewer pages than have something on them |
| `StashPages` | `1` to `255` | Pages of the stash, the same way (Stash, beside it on the Gameplay tab; a multiplayer game sets its own) |
| `SharedStashPages` | `1` to `255` | Pages of the shared stash (Shared; Single Player only) |
| `EvenScreenSpeed` | `true` / `false` | The game covers the same ground in every direction, which on its slanted view looks twice as fast across the screen as up and down (running: about 210 pixels a second across, 110 up and down). On, you move the same number of pixels a second whichever way you go. Off by default, like the game |

**Debug** (the Debug tab, `[Debug]` in `options.ini`; the same for every class; builds can hide the tab, CMake option
`DEBUG_SHOW_OPTIONS_DEBUG`):

| Setting | Values | |
|---|---|---|
| `EditorAutoSaveMinutes` | `0` (off) to `120`; the screen offers 1, 2, 5, 10, 15, 30 | Map Editor: save the map every this many minutes (default 5); a map never saved goes to the recovery file |
| `EditorKeepChanges` | `true` / `false` | Map Editor: write the map to `editor-recovery.abm` after every change, so nothing's lost if the game closes; the editor offers it back (default on) |
| `CollisionInGame` | `true` / `false` | The collision view (**B**) works in Single Player too, not only Free Roam and Testing Grounds (default off). B is also Inventory there, which comes first: give Collision view another key in Options › Controls to use it in Single Player |
| `ConfirmNewSeed` | `true` / `false` | Free Roam: ask "Do you wish to generate a new seed?" before **N** makes a new world (default on) |
| `FreeRoamInGame` | `true` / `false` | Free Roam in Single Player and Multiplayer: the Waypoint button and **V** open the **Places** list from anywhere (not only in town or at a waypoint) and take you to any area, and **CapsLock** sprints (twice as fast as running, no stamina). Can be turned on and off while playing (default off) |

## Controls

Every action of the game, with its default keys, and ours. Bindings work the way ArmA's do:

- an action can have **any number of inputs**, and the same input can be on several actions;
- gamepad buttons carry the numbers Windows gives them (its game controller settings) and where they are, the same
  for any pad, whatever is printed on it: the four on the right are a second D-pad, `Pad Button 1 (Left)`,
  `Pad Button 2 (Down)`, `Pad Button 3 (Right)`, `Pad Button 4 (Up)`; then `Pad Button 5 (Left Bumper)`,
  `6 (Right Bumper)`, `7 (Left Trigger)`, `8 (Right Trigger)`, `9 (Select)`, `10 (Start)`, `11 (Left Stick Press)`,
  `12 (Right Stick Press)`, `13 (Home)`, `14 (Touchpad)`, and `Pad D-pad Up` ..., `Pad Left Stick Up` ... (older names
  in an `options.ini`, `Pad A`, `Pad X`, `Pad Left Shoulder`, ..., still work);
- an input is a keyboard key, a mouse button, a wheel step, **moving the mouse** (`Mouse Move Left` / `Right` / `Up` /
  `Down`), a gamepad button or stick direction, with modifiers held
  (`Ctrl+X`, `Shift+Mouse Left`) or **tapped twice** (`2x K`);
- sticks are analog: pushing the left stick part way walks slower (down to about a third of the speed); moving the
  mouse counts as held while it moves, and a quick flick as a press;
- an input on two actions that are active at the same time **clashes**: only the first in the list fires (shown
  **red**). On two actions that are never active together, like Single Player's **C** (character screen) and Free
  Roam's **C** (combat), it's fine (shown **orange**).

The Diablo II controls the engine doesn't do anything with yet (belt, skills, chat, ...) are there with their keys so
they can be set up already; they're marked "not in the engine yet".

| Action | Default | Works |
|---|---|---|
| Move / attack / use | Mouse Left, Pad Button 2 (Down) | ✅ Original and Combined (a controller: any movement) |
| Move up / down / left / right | Arrow keys, gamepad left stick | ✅ Modern and Combined |
| Run / walk | R, Pad Button 11 (Left Stick Press) | ✅ |
| Run (hold) | Left Ctrl | ✅ |
| Stand still | Left Shift, Right Shift | ✅ |
| Attack | E, Pad Button 1 (Left) | ✅ Modern and Combined |
| Right skill / drop monster | Mouse Right, Pad Button 3 (Right), Pad Button 8 (Right Trigger) | ✅ Single Player (casts the right skill); Free Roam (drop) |
| Swap weapons | W | ✅ the expansion's second weapon set (also the I / II tabs in the inventory) |
| Skill 1-8 | F1-F8 | ✅ Single Player: over a skill in a picker, binds it; otherwise puts it on its button |
| Previous / next skill | Pad Button 5 / 6 (Left / Right Bumper) (the game: mouse wheel; here the wheel zooms) | ✅ the right skill steps through the skills on the skill keys |
| Skill tree | T, Pad Button 12 (Right Stick Press) | ✅ Single Player |
| Skill speed bar | S | ✅ the bound skills above the right skill |
| Use belt 1-4 | 1-4, Pad D-pad Left / Up / Right / Down | ✅ Single Player |
| Show belt | ` | ✅ the belt's upper rows (also while the mouse is over the belt) |
| Character screen | A, C, Pad Button 9 (Select) | ✅ Single Player |
| Inventory | B or I, Pad Button 4 (Up) | ✅ Single Player |
| Party, hireling, quests, message log, help | P, O, Q, M, H | ✅ |
| Clear screen | Space | ✅ closes panels |
| Show items, show portraits | Left Alt, Z | ✅ |
| Waypoint / places | V | ✅ |
| Back to town | Home | ✅ Single Player |
| Automap | Tab, Pad Button 7 (Left Trigger) | ✅ |
| Center / fade / party / names on automap | F9-F12 | ✅ Center switches between the whole view and the corner |
| Zoom in / out | Wheel Up / Down | ✅ |
| Day / dusk / night | L | ✅ |
| Screenshot | Print Screen | ✅ saved in `screenshots` next to `abyss.ini` |
| Chat, clear messages | Enter, N | ✅ Chat (multiplayer) |
| Free Roam: combat, collision view, placement preview, new world | C, B, P, N | ✅ |

**Esc** isn't bindable: it closes what's open, or opens the game menu.

The defaults as a keyboard picture and a table per group: [Controls](Controls.md).

### Per class

Gameplay settings and controls are for **all classes** by default. A class can have its own: a `[Gameplay.Barbarian]`
or `[Controls.Barbarian]` section in `options.ini` (it starts as a copy of the all-classes one). Free Roam and the
Testing Grounds use the settings of the class you're playing.

### options.ini

```ini
[Gameplay]
Movement = Combined
SeeThroughObstructions = true

[Controls]
move_attack = Mouse Left
move_up = Up, Keypad 8, Pad Left Stick Up
attack = E, Mouse 4, Pad Button 1 (Left)
character = A, C
screenshot = PrintScreen, 2x F12

[Controls.Sorceress]
...
```

Each action is a line, `id = inputs` separated by commas; an empty value means not bound. The file is written with
every action and a comment saying what it does the first time something's changed.
