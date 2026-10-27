# Multiplayer

**Main menu › Multiplayer.** Our own multiplayer, not compatible with Battle.net or the original game's TCP/IP
games.

> **What's shared:** who's in the game, where everyone is (you see each other walk and run between areas, with names
> and coloured circles), chat, the game's settings, the monsters (where they are, their life, who they're after),
> combat (with the monsters, and with each other when PvP is on), items on the ground and what monsters drop,
> chests, doors and shrines (opened once, for everyone), and parties.

## How it works

One **server** holds the game: its settings, who's in it, where everyone is and, on a dedicated server, the
characters. Players' games send it what they do; it checks that and passes it on to everyone (15 times a second for
positions). The same server runs in two ways:

| | Hosted (Multiplayer › Host) | Dedicated (`AbyssEngine --server`) |
|---|---|---|
| Runs | Inside the host's game; the host joins it like everyone else | On its own, no window (a home server, a VPS) |
| Characters | Stay on each player's computer. Ones made in the host's games are marked as made there | Kept on the server, per player (`server/characters/<player id>/`); players can't edit them |
| "Only characters made here" | Characters marked with the host's id | Characters made on that server |
| Ends | When the host leaves | When it's stopped (Ctrl+C) |

Each computer has a player id, made the first time and kept in `client.id` next to `abyss.ini`: a dedicated server
knows whose characters are whose by it.

## The Multiplayer screen

| Tab | |
|---|---|
| **Browse** | **Local network (LAN)**: games on your network answer a broadcast; **Online**: the games a lobby server lists ([below](#online-games-the-lobby-server)). Pick one (its rules show underneath, and a password box if it has one), then **Join** or double click it. **Refresh** asks again |
| **Direct IP** | An address: a name or IP, with `:port` if it isn't **4000** (`1.2.3.4`, `example.com:4100`, `[::1]:4000`), and the password if there is one. The last address is remembered |
| **Host** | Your own game, with the settings below. **Start** opens it and takes you to the character screen |

Joining goes to the **character screen**, the same as Single Player's, showing the game's name. Characters the game's
rules don't let in say "Not allowed in this game", and picking one says why. A character someone is already
playing in that game (your own, from a second copy of the game on the same computer, say) shows "Already in this
game" and can't be picked until they leave. **Create New** makes the character
on the dedicated server, or (hosted) on your computer, marked as made in that host's games. **Leave** goes back.

In the game, **Enter** opens the chat line (Enter sends, Esc stops); what's said shows at the top left. **Esc ›
Save and Exit Game** leaves (and closes the game when you're the host). Losing the connection saves your character
(hosted games) and takes you back to the Multiplayer screen, which says why.

**Options › Debug › Free Roam in Single Player and Multiplayer** works here too: travel to any area from anywhere,
and sprint ([Free Roam](Testing-Grounds-and-Free-Roam.md#in-single-player-and-multiplayer)).

## Playing together

- **Monsters** are the same in everyone's game: the same monsters in the same places (from the world seed), with the
  same life. On each map one player's game runs them, the one with the lowest player id there (usually the host),
  and tells the others 15 times a second where they are, how hurt and what they're doing. When that player leaves
  the map, the next one takes over where they left off. Monsters go after whoever is nearest.
- **Hitting a monster** that another game runs: your game rolls whether you hit, then that game takes the life off.
  Whoever lands the killing blow gets the experience (no party sharing yet).
- **Getting hit**: your game rolls whether a blow lands, with your own defense, whether it's a monster's or a player's.
- **Difficulty**: Nightmare and Hell use the game's own numbers for them (`monstats.txt` and `MonLvl.txt`'s (N) and
  (H) columns): higher levels, much more life and damage, more experience.
- **PvP** (when the host turns it on): point at another player outside town (their name turns red) and click to
  attack them, like a monster. There's no fighting in town. "So-and-so killed you" says who.
- **Chests, doors and shrines**: the first to open one opens it for everyone (the server decides who was first), and
  what's in a chest drops once, for everyone to pick up. Coming into an area later, what's been opened shows open.
- **Parties**: **P** opens the party screen: everyone in the game, with **Invite** next to each (or **Accept** when
  they've invited you; the chat says so), and **Leave Party** when you're in one. Party members share the experience
  of kills made within about two screens of each other, with the game's bonus of 35% for each member after the first
  (two players get 67.5% each), and can't attack each other with PvP on. Leaving the game leaves the party.
- **Items on the ground** are the server's: everyone sees what's dropped, and the first to reach an item gets it. On
  a dedicated server the character is saved there as you drop or pick something up. Leaving the game, what's lying
  around stays for the others until the game ends.

Each of these changed what games send each other (now protocol version 5: magic items go over the network too, and blows carry their elemental damage):
everyone needs the same build, or the game says the host runs a different version.

## Game settings

The Host tab (remembered in `host.ini`) and a dedicated server's `server/server.ini`:

| Setting | `server.ini` | |
|---|---|---|
| Game name | `Name` | Shown in Browse |
| Password | `Password` | Empty: anyone can join. The Host tab doesn't remember it (it would be kept as plain text in `host.ini`): type it for each game. A dedicated server reads it from `server.ini`, so keep that file private |
| Players | `MaxPlayers` | 1 to 255 (8 by default, like Diablo II) |
| Difficulty | `Difficulty` | Normal, Nightmare, Hell: the monsters' levels, life, damage and experience ([above](#playing-together)) |
| Game speed | `Speed` | Diablo's Normal, Fast, Faster, Fastest (20, 30, 40, 50 frames' worth a second): everything runs that much faster |
| New characters only | `NewCharactersOnly` | Level 1, no experience |
| Only characters made in my games / here | `MadeHereOnly` | See the table above |
| Characters | `Hardcore` | Any, Softcore only, Hardcore only |
| Lowest / highest level | `LevelMin`, `LevelMax` | Diablo II's level range |
| PvP | `PvP` | Players can hurt each other outside town (D1's friendly fire, D2's hostile) |
| Inventory pages | `InventoryPages` | 1 to 255 |
| Port | `Port` | TCP, 4000 by default (Diablo II's own). To play over the internet the host opens it on their router |
| World seed | `Seed` | Empty / 0: a new world each game. Everyone gets the same world |
| Show on the local network | `LAN` | Answers LAN searches (UDP port 4001) |
| List online | `Public` | Tells the lobby server about the game every 30 seconds |

The dedicated server writes a commented `server.ini` the first time it starts, in the `server` folder next to
`abyss.ini`:

```
AbyssEngine --server              # port from server.ini
AbyssEngine --server --port 4100  # or this one
```

## Online games: the lobby server

**abyss-lobby** is a small program of its own (built with the game; CMake option `BUILD_LOBBY_SERVER`, meson
`build_lobby_server`) that keeps the list of public games. Hosts tell it about their game every 30 seconds; it
forgets one it hasn't heard from in 90. It keeps nothing on disk.

```
abyss-lobby          # port 4002
abyss-lobby 5000
```

Run it anywhere hosts and players can reach, then set its address in **Options › Multiplayer › Lobby server** (or
`abyss.ini`, `[Network] Lobby = host:port`).

## Options › Multiplayer

| Setting | `abyss.ini` `[Network]` | |
|---|---|---|
| Names over the other players | `ShowNames` | On by default |
| Circles under their feet | `ShowCircles` | On by default |
| Your colour | `Colour` | Gold, White, Red, Orange, Yellow, Green, Teal, Blue, Purple, Pink, Grey, Brown: the others see your name and circle in it |
| Lobby server | `Lobby` | `host` or `host:port` (4002 if left out) |

## Ports

| Port | | |
|---|---|---|
| 4000 TCP | The game | The host / dedicated server |
| 4001 UDP | LAN searches | Hosts answer them |
| 4002 TCP | abyss-lobby | The lobby server |
