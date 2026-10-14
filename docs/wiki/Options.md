# Options

**Options** on the main menu (or in the game: **Esc** › Options) has six tabs: Video, Audio, Gameplay, Controls, Multiplayer and Debug. **OK** saves, **Cancel** (or **Esc**)
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
| **Controls** | Every action and what it's bound to (see below), and the **Mouse** window |
| **Multiplayer** | Names and circles over the other players, your colour, the lobby server ([Multiplayer](Multiplayer.md#options--multiplayer)) |
| **Debug** | The developer tools, the same for every class (below) |

**Video** (`[Graphics]` in `abyss.ini`):

| Setting | Values | |
|---|---|---|
| `FrameRate` | **Classic**: `25` (the game's own) · **Modern**: `60` (the default) · or the slider: Slowest `10`, Slower `15`, Slow `20`, Medium `30`, Fast `45`, Faster `60`, Fastest `unlimited` | Frames drawn a second at most. The game runs at the same speed whatever you pick; only how smooth it looks changes. Multiplayer never goes above 60 |
| `ControlPanel` | `Classic` / `Modern` | Single Player's control panel. **Classic**: the game's. **Modern**, laid out like the remaster's: the globes on their holders; between them the experience bar, the run button and stamina, then the mini panel (always shown), the left skill, a spot to drop items on the ground (once there are items on the ground), the right skill and the belt. Changes straight away; the default comes from the build's `DEFAULT_STYLE` |
| `Brightness`, `Contrast`, `Gamma` | `0` to `100`, `50` = the picture as the game made it | Each frame is drawn to an 800x600 picture first and adjusted on the graphics card as it goes on the window (scaling happens there too), so changes show straight away. Gamma brightens or darkens the dark and middle shades and leaves black and white alone |

**Mouse** (Controls › **Mouse**, `[Input]` in `abyss.ini`):

| Setting | Values | |
|---|---|---|
| `MatchAxes` | `true` / `false` | One pointer speed for both directions (one slider), or across and up/down apart (default on) |
| `MouseSpeedX`, `MouseSpeedY` | `25` to `300` (shown as `0.25x` to `3.00x`) | Pointer speed, `100` (`1.00x`) = the system's own. At any other speed the game moves its own pointer from the mouse's movement, so the mouse stays in the window while it has the focus (Alt+Tab to leave) |
| `WheelSpeed` | `25` to `400` (`0.25x` to `4.00x`) | How far each wheel notch scrolls lists and zooms the Map Editor; fine-stepping wheels and touchpads add up their part steps |

**Controls tab**, the ArmA way:

- **Show:** picks a group (Movement, Combat and skills, Belt, Screens, Automap, View, Chat, Free Roam), each with its
  colour in the list. A coloured bar on each row is its group; with **Everything** each group starts under a heading
  in its colour. Actions marked `*` aren't in the engine yet. Scroll with the wheel, or click or drag the scrollbar.
- Inputs are white, **orange** when the same input is also on an action that's never active at the same time (fine),
  **red** when it clashes with one that is (only the first in the list fires). Point at an action to see what it does.
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
| `Movement` | `Classic` | Diablo II: click to walk, click a monster to attack it, click a chest or door to open it |
| | `Modern` | Movement keys (arrows by default, or a gamepad's left stick) walk; the mouse attacks and opens what's in reach without walking; the attack key (**E**) hits what's under the mouse or the nearest monster in reach. Like Diablo III / IV |
| | `Combined` | Both: movement keys or clicks walk, clicks or the attack key attack |
| `SeeThroughObstructions` | `true` / `false` | Walls, trees, tents and roofs between you and the camera get a round, soft-edged see-through hole around you where they really cover you (their pixels, not their box); walls behind you never fade. It opens and closes smoothly |
| `ConfirmEntrances` | `true` / `false` | Ask before going through a cave mouth, tomb door or stairs ("To The Den of Evil?": Go in / Stay). Off by default |
| `WornGearPickup` | `Classic` / `Modern` | Picking up something you wear in the inventory. **Classic** (the game's): it comes off at once. **Modern**: it stays worn (faded in its slot, still counting) until you put it in the grid or another slot; put back in its own slot or closing the inventory, nothing changes. Default from the build's `DEFAULT_STYLE` |
| `InventoryPages` | `1` to `255` | Pages of the inventory grid in Single Player (a multiplayer game sets its own). Arrows under the grid turn the page; an item carried along can be put down on another page. Never fewer pages than have something on them |
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
| Move / attack / use | Mouse Left | ✅ Classic and Combined |
| Move up / down / left / right | Arrow keys, gamepad left stick | ✅ Modern and Combined |
| Run / walk | R | ✅ |
| Run (hold) | Left Ctrl | ✅ |
| Stand still | Left Shift, Right Shift | ✅ |
| Attack | E, Pad Button 1 (Left) | ✅ Modern and Combined |
| Right skill / drop monster | Mouse Right | ✅ Free Roam (drop); Single Player: no casting yet |
| Swap weapons | W | |
| Skill 1-8 | F1-F8 | |
| Previous / next skill | (the game: mouse wheel; here the wheel zooms) | |
| Skill tree | T | ✅ Single Player |
| Skill speed bar | S | |
| Use belt 1-4 | 1-4 | ✅ Single Player |
| Show belt | ` | |
| Character screen | A, C | ✅ Single Player |
| Inventory | B or I | ✅ Single Player |
| Party, hireling, quests, message log, help | P, O, Q, M, H | |
| Clear screen | Space | ✅ closes panels |
| Show items, show portraits | Left Alt, Z | |
| Waypoint / places | V | ✅ |
| Back to town | Home | ✅ Single Player |
| Automap | Tab | ✅ |
| Center / fade / party / names on automap | F9-F12 | |
| Zoom in / out | Wheel Up / Down | ✅ |
| Day / dusk / night | L | ✅ |
| Screenshot | Print Screen | ✅ saved in `screenshots` next to `abyss.ini` |
| Chat, clear messages | Enter, N | |
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
