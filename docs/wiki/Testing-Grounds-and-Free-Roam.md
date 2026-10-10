# Testing Grounds and Free Roam

Both are in **Main menu › Map Editor**. They share the same engine.

| | Testing Grounds | Free Roam |
|---|---|---|
| **Map** | Your `testing_grounds` map, or the built-in arena (grass with a dirt square and four training monsters), or the map from the editor's **Play** button | Starts in the **Rogue Encampment**; teleport between the game's fixed outdoor areas |
| **Bottom buttons** | Back · Class · Walk · Monsters · Clear · Combat | Back · Class · Walk · Places · Monsters · Combat |

## Controls

| | |
|---|---|
| **Left click** | Walk there, around walls and objects (hold to keep following the mouse). If the spot can't be reached you walk to the closest point you can. With combat on, clicking a monster or NPC walks up and attacks it. |
| **Right click** | Drop the monster selected in the **Monsters** list |
| **Shift + right click** | Remove the unit under the mouse |
| **R** / Walk button | Walk or run |
| **C** / Combat button | Combat on or off |
| **P** / Preview button | Placement preview on or off |
| **L** | Lighting: day (full brightness), dusk, night (a pool of light around you) |
| **B** | Show the sub-tiles you can't walk on |
| Mouse wheel | Zoom |
| **Esc** / Back | Leave (back to the editor if you came from **Play**) |
| Class button | Switch character on the spot (Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin) |

## Places (Free Roam)

Act 1: Rogue Encampment, Monastery Gate, Outer Cloister, Inner Cloister, Tristram ·
Act 2: Lut Gholein · Act 3: Kurast Docktown · Act 4: The Pandemonium Fortress · Act 5: Harrogath

They come with the NPCs the game places in them (Akara, Kashya, Charsi, Gheed, Warriv; Atma, Drognan, Fara; ...), who
stroll around their spot like in the game (guards stay put). Monsters that aren't fighting wander a little too.
Dungeons and the randomly generated wilderness (Blood Moor, ...) are not in the list (see [Feature Status](Feature-Status.md)).

## Monsters list and placement preview

**Monsters** opens the Bestiary list (Players / NPCs / Enemies / Bosses / Others). With it open, the selected monster
is shown see-through under the mouse, facing you, where right click would drop it, with a green marker (it can stand
there) or a red one (blocked). Turn it off with **Preview** at the bottom of the panel, or **P**. **Clear** (Testing
Grounds) removes everything you dropped.

## Combat toggle

A debugging switch for fighting between you and the NPCs/monsters:

- **Off**: nobody fights; walk around freely.
- **On**: NPCs and monsters within about 10 sub-tiles come at you (around walls if needed) and attack. Click one to attack it; its name and a
  life bar show over it.
  - Monster life and damage: the game's own tables (`monstats.txt` percentages of the `MonLvl.txt` base values,
    normal difficulty). A Zombie has 7-12 life and hits for 1-3.
  - Your life: your class's starting life (`charstats.txt`; Amazon 50). Your hits do a fixed 6-14 damage (debug
    value; NPCs, which have no life in the tables, get 20).
  - When you die you come back at the start after 3 seconds, and monsters leave you alone for 3 more.

## Known limits

- Units don't avoid bumping into each other (they can stand in the same spot).
- NPCs don't talk or trade yet.
- Combat is melee only, no skills, levels or experience.
- Objects (chests, waypoints, doors) aren't there yet.
