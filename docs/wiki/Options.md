# Options

**Options** on the main menu (or in the game: **Esc** › Options) has four tabs: Video, Audio, Gameplay and Controls. **OK** saves, **Cancel** (or **Esc**)
puts everything back as it was, **Defaults** resets the tab you're on.

Video and audio are saved in `abyss.ini` (only those lines change: the rest of the file, comments too, stays as you
wrote it); gameplay settings and controls in **`options.ini`** next to it. Both are plain text with a comment on every
setting, so they can be edited outside the game too.

## The Options screen

| Tab | |
|---|---|
| **Video** | Window size (800x600 up to 4K, or "Follow InitialScale"), full screen, scaling (sharp pixels / smooth; from the next start). The game keeps its 4:3 shape: wider windows get black bars |
| **Audio** | Master, music, sound effects and interface volume: drag the slider or type a number from 0 to 100; you hear it right away |
| **Gameplay** | Settings for all classes or one class (tick "Own settings" to give a class its own), type of movement, see-through obstructions |
| **Controls** | Every action and what it's bound to (see below) |

**Controls tab**, the ArmA way:

- **Show:** picks a group (Movement, Combat and skills, Belt, Screens, Automap, View, Chat, Free Roam). A coloured bar
  on each row is its group; actions marked `*` aren't in the engine yet.
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

## Gameplay

| Setting | Values | |
|---|---|---|
| `Movement` | `Classic` | Diablo II: click to walk, click a monster to attack it, click a chest or door to open it |
| | `Modern` | Movement keys (arrows by default, or a gamepad's left stick) walk; the mouse attacks and opens what's in reach without walking; the attack key (**E**) hits what's under the mouse or the nearest monster in reach. Like Diablo III / IV |
| | `Combined` | Both: movement keys or clicks walk, clicks or the attack key attack |
| `SeeThroughObstructions` | `true` / `false` | Walls, trees, tents and roofs between you and the camera fade where they really cover you (their pixels, not their box), with the tiles around them so it's a window rather than a hole; walls behind you never fade. Fading eases in and out |
| `ConfirmEntrances` | `true` / `false` | Ask before going through a cave mouth, tomb door or stairs ("To The Den of Evil?": Go in / Stay). Off by default |

**Debug** (the bottom of the Gameplay tab, `[Debug]` in `options.ini`; the same for every class):

| Setting | Values | |
|---|---|---|
| `EditorAutoSaveMinutes` | `0` (off) to `120`; the screen offers 1, 2, 5, 10, 15, 30 | Map Editor: save the map every this many minutes (default 5); a map never saved goes to the recovery file |
| `EditorKeepChanges` | `true` / `false` | Map Editor: write the map to `editor-recovery.abm` after every change, so nothing's lost if the game closes; the editor offers it back (default on) |
| `CollisionInGame` | `true` / `false` | The collision view (**B**) works in Single Player too, not only Free Roam and Testing Grounds (default off). B is also Inventory there, but actions the engine doesn't do yet never take a key from one it does |
| `ConfirmNewSeed` | `true` / `false` | Free Roam: ask "Do you wish to generate a new seed?" before **N** makes a new world (default on) |

## Controls

Every action of the game, with its default keys, and ours. Bindings work the way ArmA's do:

- an action can have **any number of inputs**, and the same input can be on several actions;
- gamepad buttons carry the numbers Windows gives them (its game controller settings) and where they are, the same
  for any pad, whatever is printed on it: the four on the right are a second D-pad, `Pad Button 1 (Left)`,
  `Pad Button 2 (Down)`, `Pad Button 3 (Right)`, `Pad Button 4 (Up)`; then `Pad Button 5 (Left Bumper)`,
  `6 (Right Bumper)`, `7 (Left Trigger)`, `8 (Right Trigger)`, `9 (Select)`, `10 (Start)`, `11 (Left Stick Press)`,
  `12 (Right Stick Press)`, `13 (Home)`, `14 (Touchpad)`, and `Pad D-pad Up` ..., `Pad Left Stick Up` ... (older names
  in an `options.ini`, `Pad A`, `Pad X`, `Pad Left Shoulder`, ..., still work);
- an input is a keyboard key, a mouse button, a wheel step, a gamepad button or stick direction, with modifiers held
  (`Ctrl+X`, `Shift+Mouse Left`) or **tapped twice** (`2x K`);
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
| Right skill / drop monster | Mouse Right | ✅ Free Roam (drop) |
| Swap weapons | W | |
| Skill 1-8 | F1-F8 | |
| Previous / next skill | (the game: mouse wheel; here the wheel zooms) | |
| Skill tree, skill speed bar | T, S | |
| Use belt 1-4, show belt | 1-4, ` | |
| Character screen | A, C | ✅ Single Player |
| Inventory, party, hireling, quests, message log, help | B or I, P, O, Q, M, H | |
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
