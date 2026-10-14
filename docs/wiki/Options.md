# Options

**Options** on the main menu has four tabs: Video, Audio, Gameplay and Controls. **OK** saves, **Cancel** (or **Esc**)
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
  go of it; the same key twice quickly makes it a double tap, "2x K"), or pick mouse buttons, the wheel, mouse
  combinations and gamepad inputs from the list on the right. Select one and **Delete** it, **Default** puts the
  game's back, **Undo** what you did in the window, **Previous** / **Next** go through the actions, **OK** / **Cancel**
  (or **Esc**).
- **Keyboard** shows a keyboard coloured by group, with the action on each key; point at a key to see everything on
  it; a red border is a clash. **List** goes back.
- **Export** writes the whole table to `controls.md` next to `abyss.ini` (input, action, group, whether it works yet,
  clashes), to print or share.

## Gameplay

| Setting | Values | |
|---|---|---|
| `Movement` | `Classic` | Diablo II: click to walk, click a monster to attack it, click a chest or door to open it |
| | `Modern` | Movement keys (arrows by default, or a gamepad's left stick) walk; the mouse attacks and opens what's in reach without walking; the attack key (**E**) hits what's under the mouse or the nearest monster in reach. Like Diablo III / IV |
| | `Combined` | Both: movement keys or clicks walk, clicks or the attack key attack |
| `SeeThroughObstructions` | `true` / `false` | Walls, trees, tents and roofs between you and the camera fade, so you can see where you're walking |

## Controls

Every action of the game, with its default keys, and ours. Bindings work the way ArmA's do:

- an action can have **any number of inputs**, and the same input can be on several actions;
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
| Attack | E, Pad X | ✅ Modern and Combined |
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

**Esc** isn't bindable: it always closes what's open and leaves.

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
attack = E, Mouse 4, Pad X
character = A, C
screenshot = PrintScreen, 2x F12

[Controls.Sorceress]
...
```

Each action is a line, `id = inputs` separated by commas; an empty value means not bound. The file is written with
every action and a comment saying what it does the first time something's changed.
