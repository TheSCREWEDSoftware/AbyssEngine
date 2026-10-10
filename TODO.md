# To do

What's broken, what needs polish and what's left to build, in rough order. Tick things off (or delete them) in the
same commit that fixes them. The wiki's [Feature Status](docs/wiki/Feature-Status.md) says what works; this file says
what's next.

## Bugs (fix first)

- [ ] **N does nothing in Free Roam.** It should give a new random world seed. The key is ignored whenever "text input
  is active", and on Windows SDL has text input on all the time, so N never gets through. Only ignore N while the
  seed box itself has focus.
- [ ] **Town folk attack you.** With combat on in Free Roam / Testing Grounds, the rogue scouts and archers in the
  Rogue Encampment come at you. Nobody in a town should fight, whatever the combat switch says (the switch is for
  monsters).
- [ ] **Going between areas doesn't line up.** You leave the encampment on one side and come into the Blood Moor
  through a gap on an unrelated side, away from where you'd expect. The gap in the next area should face the way you
  left (leave the town to the west, arrive on the Blood Moor's east edge), at the same place along the edge.
- [ ] **Entrances don't always take you to the right place.** Check that each cave mouth, tomb door and staircase
  leads to the right area and that you arrive at its matching entrance there (in particular areas with two entrances:
  Burial Grounds' Crypt and Mausoleum, Lut Gholein's sewers and harem, the Forgotten Tower).

## Polish

- [ ] **One continuous world outdoors.** Like the game, the land beyond a town's fence is the Blood Moor, not black
  void: draw (and let you walk into) the neighbouring areas around the one you're in, placed where they join, so
  "travelling" between outdoor areas is just walking. Needs world positions for each area (the game lays an act's
  outdoor areas out on one grid) and borders that match up.
- [ ] **"Entering the Blood Moor".** Show the game's red caption when you walk into a new area.
- [ ] **Automap (Tab).** An overlay map of what you've explored, with exits and waypoints marked, like the game's.
- [ ] **Long walks.** Clicking very far away can stop you part of the way (the pathfinder only searches a box around the
  start and goal); fall back to walking straight there, or search further in steps.
- [ ] **Control panel.** The game's bottom panel with the red and blue globes, run/walk button, belt and skill
  buttons, instead of our debug button bar (Single Player).
- [ ] **Act 5 Highlands / Tundra.** Border pieces and filler only roughly match the game (ravines, cliffs).
- [ ] **Bloody Foothills.** A couple of strips don't quite join up.
- [ ] **Build warnings on Windows.** Turn off MSVC's C4996 "unsafe function" warnings for the CRT, rename the `c_sound`
  local in Sounds.c, add casts for the C4244/C4267 ones, quiet the unreachable-code warning in Button.c.

## Left to build

### World

- [ ] Act 3: Kurast (Lower Kurast, Bazaar, Upper Kurast), Kurast Causeway, Travincal, Spider Cave/Cavern, and the
  jungle's links (Docktown - Spider Forest - Great Marsh - Flayer Jungle - Lower Kurast).
- [ ] The Monastery (Tamoe Highland - Monastery Gate - Outer Cloister - Barracks ...), Tristram's portal at the Cairn
  Stones, the Arcane Sanctuary, Chaos Sanctum, the rest of the Act 5 outdoors (Frigid Highlands to the Ancients'
  Way).
- [ ] Waypoints that work in Free Roam, and only the ones you've found in Single Player.
- [ ] The game's own rules for which side areas join on and where entrances go (it keeps them in its program).
- [ ] Per-area lighting, weather (rain, snow), ambient sounds and music per area.

### Single Player

- [ ] Items: inventory, equipment (shown on the character), drops, gold, the stash, belt and potions.
- [ ] Skills: the skill tree, spending skill points, casting (missiles, auras, summons).
- [ ] NPCs: talking, trading, repairs, healing (Akara), hirelings.
- [ ] Quests.
- [ ] Difficulty levels (Nightmare, Hell), monster levels from the area, champions and uniques.
- [ ] Monster AI beyond "walk at the player": ranged attackers, casters, fleeing, packs.
- [ ] Losing some experience / gold on death; picking up your corpse.

### Other

- [ ] Map Editor: undo, saving back to DS1.
- [ ] Credits and Cinematics menus.
- [ ] Multiplayer (our own, not compatible with Battle.net).
