# 4. Game Files and Configuration

**Installation guide:** [1. Requirements](01-Requirements.md) › [2. Getting the Source](02-Getting-the-Source.md) › [3. Building](03-Building.md) › **4. Game Files** › [5. First Run](05-First-Run.md)

## The game files

Abyss Engine runs on the original **Diablo II + Lord of Destruction** data. You need these MPQ files from your own,
legitimate copy of the game (classic, not *Resurrected*), all in one folder, normally the game's install folder:

| File | Contents |
|------|----------|
| `d2data.mpq`, `d2exp.mpq` | graphics, maps, data tables (classic and expansion) |
| `Patch_D2.mpq` | the 1.13/1.14 patch: updated data tables (strongly recommended) |
| `d2char.mpq` | player character graphics |
| `d2music.mpq`, `d2xmusic.mpq` | music |
| `d2video.mpq`, `d2xvideo.mpq` | intro videos |
| `d2sfx.mpq` | sound effects |
| `d2speech.mpq`, `d2xtalk.mpq` | speech (not used yet) |

Missing files are skipped, but without `d2data.mpq` and `d2exp.mpq` nothing will show. Modded or repacked copies of the
game are not supported.

## abyss.ini

The engine reads its settings from `abyss.ini`. A template is in the repository at
[`content/abyss.ini`](https://github.com/TheSCREWEDSoftware/AbyssEngine/blob/main/content/abyss.ini). Copy it to one
of these places (the first one found is used):

1. **Next to the program** (`AbyssEngine.exe`): handy for a portable setup or while developing, e.g.
   `build\RelWithDebInfo\abyss.ini`
2. Your user settings folder:
   - Windows: `%APPDATA%\abyss\abyss.ini`
   - Linux: `~/.config/abyss/abyss.ini`
   - macOS: `~/Library/Application Support/abyss/abyss.ini`

Then edit it:

```ini
[General]
# The folder with the MPQs (a slash at the end or not, both work)
BasePath = C:\Program Files (x86)\Diablo II\
Locale = latin
SkipIntroMovies = false

[Graphics]
ScaleQuality = nearest      # nearest, linear or best: how the 800x600 picture is scaled up
InitialScale = 1.0          # window = 800x600 times this, unless Resolution is set
FullScreen = false          # Alt+Enter toggles full screen while running
# Resolution = 1080p        # 720p, 1080p, 1440p, 4k or e.g. 1600x1200 (the game keeps its 4:3 shape)
FrameRate = 60              # frames a second at most: 25 = Classic (the game's), 60 = Modern, unlimited
Brightness = 50             # 0-100 each; 50 = the picture as the game made it
Contrast = 50
Gamma = 50

[Audio]
MasterVolume = 0.5
MusicVolume = 1.0
SfxVolume = 1.0
UIVolume = 1.0
SpeechVolume = 1.0

[Input]
MouseSpeedX = 100           # pointer speed in percent (100 = the system's own); 25-300
MouseSpeedY = 100
MatchAxes = true            # true: MouseSpeedX for both
WheelSpeed = 100            # percent of each wheel step; 25-400

[Network]
# Lobby = lobby.example.com:4002  # abyss-lobby, for Multiplayer > Browse > Online
Colour = 0                  # how other players see you (Options > Multiplayer)
ShowNames = true
ShowCircles = true

[MPQs]
# Highest priority first: the first MPQ that has a file wins
Patch_D2.mpq
d2exp.MPQ
d2xmusic.MPQ
d2xtalk.MPQ
d2xvideo.MPQ
d2data.MPQ
d2char.MPQ
d2music.MPQ
d2sfx.MPQ
d2video.MPQ
d2speech.MPQ
```

| Setting | Meaning |
|---------|---------|
| `BasePath` | Folder containing the MPQs, with or without a slash at the end (quotes around it are fine). An entry in `[MPQs]` given as a full path is used as it is. |
| `Locale` | Font set: `latin` for English and other Latin-alphabet languages. |
| `DataFolder` | Optional. A folder of loose game files read before the MPQs, in the MPQs' paths (`data/global/excel/weapons.txt`, lower case): what `AbyssEngine --extract` writes, or your own changes to the tables (a mod). Files that aren't there come from the MPQs as usual. Text mode runs from such a folder alone, with no MPQs at all (pictures and sounds it doesn't need are left out). |
| `SkipIntroMovies` | `true` starts at the main menu (the `DEBUG_SKIP_INTRO` build option does this regardless). |
| `Resolution` | Window size. The game is drawn at 800x600 and scaled, with black bars on wide screens. |
| `[MPQs]` | Which MPQs to load, highest priority first. On Linux the names are matched without caring about upper/lower case. |

## Files the engine writes

These go next to `abyss.ini`:

- `maps\`: maps saved from the [Map Editor](Map-Editor.md) (`.abm`), including `testing_grounds.abm`
- `loading.txt`: how long each loading screen took last time, so the progress bar is accurate

---

**Next step:** [5. First Run](05-First-Run.md)
