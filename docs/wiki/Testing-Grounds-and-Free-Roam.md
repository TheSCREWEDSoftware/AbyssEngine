# Testing Grounds and Free Roam

Both are in **Main menu › Map Editor**. They share the same engine.

| | Testing Grounds | Free Roam |
|---|---|---|
| **Map** | Your `testing_grounds` map, or the built-in arena (grass with a dirt square and four training monsters), or the map from the editor's **Play** button | Starts in the **Rogue Encampment**; teleport to the towns, fixed areas, wilderness and dungeons |
| **Bottom buttons** | Back · Class · Walk · Monsters · Clear · Combat | Back · Class · Walk · Places · Monsters · Combat |

## Controls

| | |
|---|---|
| **Left click** | Walk there, around walls and objects (hold to keep following the mouse). If the spot can't be reached you walk to the closest point you can. With combat on, clicking a monster or NPC walks up and attacks it. Clicking a chest, door, shrine or waypoint walks over and opens / activates it. |
| **Right click** | Drop the monster selected in the **Monsters** list |
| **Shift + right click** | Remove the unit under the mouse |
| **R** / Walk button | Walk or run |
| **C** / Combat button | Combat on or off |
| **P** / Preview button | Placement preview on or off |
| **L** | Lighting: day (full brightness), dusk, night (a pool of light around you) |
| **B** | Show the sub-tiles you can't walk on |
| **N** | Free Roam, in a randomly built area: build it again with a new layout |
| Mouse wheel | Zoom |
| **Esc** / Back | Leave (back to the editor if you came from **Play**) |
| Class button | Switch character on the spot (Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin) |

## Places (Free Roam)

The list follows the game's order of areas, act by act.

- **Fixed areas**: the five towns, Monastery Gate, Outer and Inner Cloister, Tristram. They come with the NPCs the game
  places in them (Akara, Kashya, Charsi, Gheed, Warriv; Atma, Drognan, Fara; ...), who stroll around their spot like in
  the game (guards stay put).
- **Randomly built areas**: the wilderness (Blood Moor, Cold Plains, ..., the desert, the Outer Steppes, the Highlands,
  ...), the dungeons (Den of Evil, caves, Crypt, Jail, Catacombs, sewers, tombs, ..., Worldstone Keep), the Act 3 jungle
  and the Bloody Foothills. They're put together from the game's pieces every time you go there ("Building ..."), with
  monsters from the area's list; press **N** for another layout. You start at the entrance room of a dungeon, and on
  open ground near the middle of the wilderness.

Monsters that aren't fighting wander a little. Kurast, Travincal, the Arcane Sanctuary and the Chaos Sanctum aren't in
the list yet, and you can't walk from one area into the next (see [Feature Status](Feature-Status.md)).

## Monsters list and placement preview

**Monsters** opens the Bestiary list (Players / NPCs / Enemies / Bosses / Others). With it open, the selected monster
is shown see-through under the mouse, facing you, where right click would drop it, with a green marker (it can stand
there) or a red one (blocked). Turn it off with **Preview** at the bottom of the panel, or **P**. **Clear** (Testing
Grounds) removes everything you dropped.

## Combat toggle

A debugging switch for fighting between you and the NPCs/monsters:

- **Off**: nobody fights; walk around freely.
- **On**: NPCs and monsters within about 10 sub-tiles come at you (around walls if needed) and attack, but only those
  that have an attack animation (in town that's the rogue scouts; Gheed, Akara, Charsi and the other traders keep
  strolling). A unit only does what it has an animation for: without a get-hit animation it doesn't flinch, and
  without death or corpse graphics it simply disappears when killed. You can still attack anyone. Click one to attack it; its name and a
  life bar show over it.
  - Monster life and damage: the game's own tables (`monstats.txt` percentages of the `MonLvl.txt` base values,
    normal difficulty). A Zombie has 7-12 life and hits for 1-3.
  - Your life: your class's starting life (`charstats.txt`; Amazon 50). Your hits do a fixed 6-14 damage (debug
    value; NPCs, which have no life in the tables, get 20).
  - When you die you come back at the start after 3 seconds, and monsters leave you alone for 3 more.

## Objects

Areas come with their objects: chests, doors, shrines, waypoints, torches, campfires, corpses, ... Torches and fires
burn. Hovering a chest, door, shrine or waypoint shows its name; click it and you walk over and open it (chests open,
doors open and let you through, shrines and waypoints light up), with the game's sound. Objects block walking where
the game says they do (a closed door, a chest, a shrine's base). What they do beyond that (loot, shrine effects,
travelling by waypoint) isn't in yet. In the Testing Grounds objects can be dropped from the Monsters list's
**Others** tab.

## Known limits

- Units don't avoid bumping into each other (they can stand in the same spot).
- NPCs don't talk or trade yet.
- Combat is melee only, no skills, levels or experience.
- Areas aren't joined up: use the Places list (waypoints and cave entrances don't take you anywhere yet).
