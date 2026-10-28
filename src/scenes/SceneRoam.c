#include "../audio/Sounds.h"
#include "../common/AbyssConfiguration.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "../drawing/AmountDialog.h"
#include "../drawing/Button.h"
#include "../drawing/ConfirmDialog.h"
#include "../drawing/Cursor.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/LoadingScreen.h"
#include "../drawing/MissileSprite.h"
#include "../drawing/Screenshot.h"
#include "../drawing/Sprite.h"
#include "../drawing/TextBox.h"
#include "../game/Affixes.h"
#include "../game/Character.h"
#include "../game/Controls.h"
#include "../game/Cube.h"
#include "../game/Hirelings.h"
#include "../game/Inventory.h"
#include "../game/MissileData.h"
#include "../game/Quests.h"
#include "../game/Settings.h"
#include "../game/Shop.h"
#include "../game/SkillData.h"
#include "../game/Skills.h"
#include "../game/StateOverlays.h"
#include "../game/Treasure.h"
#include "../managers/AudioManager.h"
#include "../managers/InputManager.h"
#include "../managers/VideoManager.h"
#include "../map/Automap.h"
#include "../map/LevelTypes.h"
#include "../map/Map.h"
#include "../map/MapRenderer.h"
#include "../map/MapSession.h"
#include "../map/MapUnits.h"
#include "../map/MonsterStats.h"
#include "../map/Objects.h"
#include "../map/Pathfinding.h"
#include "../map/Places.h"
#include "../map/World.h"
#include "../net/Multiplayer.h"
#include "CatalogCharacters.h"
#include "GameInventory.h"
#include "GameSkills.h"
#include "SceneCharacterSelect.h"
#include "SceneFreeRoam.h"
#include "SceneGame.h"
#include "SceneMainMenu.h"
#include "SceneMapEditor.h"
#include "SceneMultiplayer.h"
#include "SceneOptions.h"
#include "SceneTestingGrounds.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Testing Grounds and Free Roam: a character walking around a map with NPCs and monsters, and optional combat.

#define VIEW_W           800
#define VIEW_H           558
#define BOTTOM_Y         562
#define BUTTON_STEP      132
#define PANEL_X          580
#define PANEL_INNER_X    588
#define PANEL_INNER_W    204
#define CATEGORIES_Y     48
#define PREVIEW_BUTTON_Y 500 // Monsters panel: placement preview toggle
#define LIST_ROWS        20
#define LIST_ROW_H       18
#define SEED_LABEL_Y     418 // Free Roam: the world seed, under the Places list
#define SEED_BOX_Y       432
#define SEED_BUTTONS_Y   460
#define MAX_UNITS        2048  // an act's outdoors joined into one map has many
#define WALK_SPEED       5.0f  // sub-tiles per second
#define ACTIVE_DISTANCE  90.0f // sub-tiles: units further from you than this wait (about two screens)
#define MONSTER_SYNC_DISTANCE                                                                                          \
    55.0f                     // multiplayer: the monsters this near a player are sent to the others (beyond a screen)
#define SPRINT_SPEED    18.0f // Free Roam's sprint: twice as fast as running, no stamina
#define LONG_WALK_LEGS  64    // a far click is walked in up to this many searches
#define RUN_SPEED       9.0f
#define ARRIVE_DISTANCE 0.15f

// Combat
#define AGGRO_RANGE       10.0f // sub-tiles
#define ATTACK_RANGE      1.6f
#define ATTACK_COOLDOWN   900  // ms between a monster's attacks
#define USE_RANGE         1.5f // how close to an object's footprint the player must be to open it
#define RESPAWN_DELAY     3000
#define RESPAWN_GRACE     3000 // after coming back, monsters leave you alone for a moment
#define PLAYER_DAMAGE_MIN 6
#define PLAYER_DAMAGE_MAX 14
// Wandering (NPCs in town, idle monsters): pause, then stroll to a random spot near home
#define WANDER_NPC_RADIUS     4.0f // sub-tiles from home
#define WANDER_MONSTER_RADIUS 3.0f
#define WANDER_PAUSE_MIN_MS   2500
#define WANDER_PAUSE_MAX_MS   7000
#define WANDER_SPEED_MAX      3.0f // strolling, not rushing
#define REPATH_PLAYER_MS      250  // while the mouse is held / the target moves
#define REPATH_MONSTER_MS     500
#define REPATH_DISTANCE       1.0f // re-plan when the goal moved this far
#define HOVER_RANGE           2.5f

#define DEFAULT_MAP_SIZE    32
#define DEFAULT_LEVEL_TYPE  "Act 1 - Town"
#define TESTING_GROUNDS_MAP "testing_grounds"
#define START_LEVEL_ID      1 // Rogue Encampment

static const char *player_classes[][3] = {
    // name, token, charstats class
    {"Amazon",      "am", "Amazon"     },
    {"Sorceress",   "so", "Sorceress"  },
    {"Necromancer", "ne", "Necromancer"},
    {"Paladin",     "pa", "Paladin"    },
    {"Barbarian",   "ba", "Barbarian"  },
    {"Druid",       "dz", "Druid"      },
    {"Assassin",    "ai", "Assassin"   },
};
#define PLAYER_CLASS_COUNT (int)(sizeof(player_classes) / sizeof(player_classes[0]))

static bool     combat_enabled = false; // kept between visits
static uint32_t fixed_seed     = 0;     // Free Roam: the world seed typed in (kept between visits), 0 = random

// Lighting: L cycles through these (a pool of light around the player, darkness everywhere else)
static const struct {
    const char *name;
    uint8_t     darkness;
    float       radius; // sub-tiles
} light_levels[] = {
    {"auto",  0,   0    }, // by the area: dark with a pool of light indoors; outside, the time of day
    {"day",   0,   0    },
    {"dusk",  120, 16.0f},
    {"night", 215, 11.0f},
};
#define LIGHT_LEVEL_COUNT (int)(sizeof(light_levels) / sizeof(light_levels[0]))
static int light_level = 0; // kept between visits

// The time of day outdoors: a day is 20 minutes (12 day, 2 dusk, 4 night, 2 dawn), counted from midday when the
// program started; darkness and the light radius around you go smoothly between day and night
#define DAY_MS 1200000ull
static float Roam__Night(void) {                                                // 0 day .. 1 night
    const float t = (float)(SDL_GetTicks64() % DAY_MS) / (float)DAY_MS * 20.0f; // minutes into the day
    if (t >= 12.0f && t < 14.0f) {
        return (t - 12.0f) / 2.0f;
    }
    if (t >= 14.0f && t < 18.0f) {
        return 1.0f;
    }
    if (t >= 18.0f) {
        return 1.0f - ((t - 18.0f) / 2.0f);
    }
    return 0.0f;
}

static void Roam__TimeOfDay(int *darkness, float *radius) {
    const float night = Roam__Night();
    *darkness         = (int)(night * 190.0f);
    *radius           = 30.0f - (night * 16.0f);
}

// How a monster closes in besides walking (monstats skills)
enum { MOVE_SKILL_NONE, MOVE_SKILL_TELEPORT, MOVE_SKILL_LEAP, MOVE_SKILL_BURROW };

typedef enum UnitState {
    UNIT_IDLE,
    UNIT_WALK,
    UNIT_ATTACK,
    UNIT_HIT,
    UNIT_DYING,
    UNIT_DEAD,
    UNIT_OPERATE,
    UNIT_CAST, // using a skill (its own animation: SC, A1, TH, S1, ...)
} UnitState;

#define UNIT_STATES 6 // buffs, an aura, a curse: how many show on a figure at once

typedef struct DrawContext {
    Figure     *figure;
    int         direction;
    int         frame;
    const char *palette;                 // the act's (the overlays')
    char        states[UNIT_STATES][32]; // states.txt names: their overlays are drawn with the figure
    int         state_count;
    uint8_t     alpha; // see-through (a ghostly champion); 0: solid
} DrawContext;

typedef struct Unit {
    char        base[12];
    char        id[40];
    char        name[48];
    const char *idle_mode;
    float       x; // sub-tiles
    float       y;
    bool        even_speed; // the player with "the same speed on screen": steps scaled by Roam__ScreenSpeedFactor
    float       target_x;   // walking towards
    float       target_y;
    int         turn;
    UnitState   state;
    const char *mode; // animation currently shown
    int         frame;
    uint64_t    frame_time;
    int         hp;
    int         max_hp;
    int         damage_min;
    int         damage_max;
    int         level;                 // monsters: from monstats; the player: the character's
    int         defense;               // armor class
    int         attack_rating;         // to-hit
    int         experience;            // given for killing it
    char        treasure[48];          // what it drops (TreasureClassEx.txt)
    int         resist[ELEMENT_COUNT]; // monsters: percent (100 or more: immune)
    int         element;               // monsters: their attack's elemental damage (0 none), chance, range
    int         element_chance;
    int         element_min;
    int         element_max;
    float       speed;
    bool        npc;
    bool        dropped;  // added in the Testing Grounds (Clear removes these)
    bool        provoked; // hit by the player: fights back even far away
    uint64_t    next_attack;
    MapPath     path;        // the way around walls to where it's going
    float       path_goal_x; // where the path was planned to
    float       path_goal_y;
    uint64_t    next_repath;
    float       home_x; // where the map placed it: wandering stays around here
    float       home_y;
    bool        wandering;   // on a stroll (not fighting)
    uint64_t    next_wander; // when to set off again
    int         can_walk;    // -1 = not checked yet, 0 = no walk animation, 1 = yes
    int         can_attack;  // -1 = not checked yet, 0 = no attack animation (never attacks), 1 = yes
    bool        vanished;    // died without death or corpse graphics: no longer drawn
    bool        is_object;   // a chest, door, shrine, torch, ...
    ObjectInfo  object;
    char        object_mode[4]; // the animation it rests in: "nu" (closed), "on" (opened, lit)
    bool        used;           // opened / activated: can't be clicked again
    bool        locked;         // a locked chest: a key (or an Assassin) opens it
    bool        trapped;        // goes off when opened
    uint64_t    ready_at;       // a used shrine that comes back: when (0: never)
    int         rest_frame;     // >= 0: show this frame of the "op" animation (doors that rest on their last frame)
    char        net_mode[4];    // another player (multiplayer): the animation their game says they're in
    bool        hireling;       // the character's hireling (an ally that comes along from area to area)
    uint8_t     rank;           // UnitRank: a champion, a unique and its minions, a super unique (named in blue / gold)
    // Monster AI (SceneRoamAI.inc): what it shoots, the spell it casts, when next, running away
    char            shot[32];
    const SkillRow *spell;
    int             spell_level;
    const SkillRow *spells[4]; // all the missile and nova spells it has (a boss's several), picked between
    int             spell_levels[4];
    int             spell_count;
    uint64_t        next_spell;
    const SkillRow *hex;  // a curse it casts on you (Amplify Damage, Weaken, Decrepify); NULL none
    const SkillRow *hex2; // a second one it picks between (the Succubus witches: Amplify Damage, Weaken)

    bool            hex_any; // MonCurseCast: one of the curses, picked each time
    int             hex_level;
    uint8_t         move_skill;   // MOVE_SKILL_*: how it closes in (Teleport, Leap)
    uint64_t        next_special; // when it can curse or teleport / leap again
    const SkillRow *aura;         // a unique's aura (aura enchanted: Might, Holy Fire, Holy Freeze, Holy Shock)
    int             aura_level;
    uint64_t        aura_pulse;   // its next pulse
    bool            burrowed;     // under the ground (Submerge / Emerge: frog demons, tentacles)
    uint8_t         portal_owner; // a Town Portal's: 0 ours, else the player id whose it is
    uint8_t         champion;     // CHAMPION_*: what kind of champion (shown under its name)
    bool            spectral;     // a unique's Spectral Hit: each blow fire, cold or lightning, picked each time
    bool            mana_burn;    // a unique's Mana Burn: its blows take mana as well as life
    char            power[48];    // a unique's powers, shown under its name ("Extra Strong, Cursed")
    uint64_t        next_neutral; // its idle sound's next time (0: not set yet)
    uint64_t        next_raise;   // a Fallen Shaman's / Greater Mummy's next raising, a Zakarum priest's healing
    int             leader;       // a minion: its unique (index + 1), whom it keeps close to; 0 none
    char            spawn[32];    // a nest's, egg's or mother's young (monstats spawn), "" none
    uint8_t         spawner;      // SPAWNER_*: how it makes them
    int             spawned;      // how many it's made
    bool            suicide;      // a suicide minion: blows itself up next to you

    uint64_t       flee_until;
    bool           ranged_swing; // the attack being played was a shot or a spell (no blow at the end)
    bool           pack_called;  // provoked: its pack has been called
    float          reach;        // an ally that attacks from afar (a Rogue Scout's bow): how far, 0 = melee
    bool           has_looks;    // a player: drawn in what they wear (looks), not the class's plain figure
    CharacterLooks looks;
    // Multiplayer monsters
    int      net_target; // the map's owner: the player it's after (0: the owner's own)
    float    net_from_x; // the others: shown moving smoothly to where the owner last said it is
    float    net_from_y;
    float    net_to_x;
    float    net_to_y;
    uint64_t net_at;
    int      net_state;    // the state the owner last said it's in (-1: not told yet)
    char     cast_mode[4]; // UNIT_CAST: the animation ("sc", "a1", ...)
    // Skills
    bool            ally;        // summoned by the player: fights the monsters
    const SkillRow *summoned_by; // the skill that called it
    uint64_t        ally_until;  // gone at this time (0: stays)
    bool            corpse_used; // a corpse a skill has already been used on (Find Item, Find Potion, Grim Ward)
    bool            decoy;       // the Amazon's Decoy: stands there drawing the monsters, never strikes
    bool            shadow;      // the Assassin's Shadow Warrior / Master: fights with your own skills
    int             crit;        // the Valkyrie: your Critical Strike's chance of double damage
    bool            turned;      // a monster on our side for a while (Attract): back to a monster then, not gone
    // Another player's summon or hireling (multiplayer), only to be seen: whose (player id), which of theirs, when
    // last heard of
    int      remote_owner;
    int      remote_slot;
    uint64_t remote_seen;
    // An Assassin's trap: it doesn't move or get attacked, it fires its missile at monsters so many times, then goes
    const SkillRow *trap;
    int             trap_level;
    int             trap_shots;
    uint64_t        next_shot;
    int             fight_ally; // a monster: the ally it's after (index + 1), 0 = a player
    const SkillRow *curse;      // the curse on it (one at a time, like the game)
    int             curse_level;
    uint64_t        curse_until;
    uint64_t        chill_until; // cold: slowed down until then
    float           poison_left; // poison still to do, and how fast
    float           poison_rate;
    float           poison_carry; // parts of a point done so far
    bool            poison_ours;  // our poison (the kill is ours)
    float           bleed_left;   // Open Wounds: bleeding still to do, and how fast (the kill is ours)
    float           bleed_rate;
    uint64_t        frozen_until; // Freezes Target: doesn't move until then
    int             slowed;       // Slows Target: percent slower (the most it's been slowed by, at most 50)
    bool            undead;       // monstats lUndead / hUndead, demon: what items against them work on
    bool            demon;
    bool            boss;
    DrawContext     draw;
} Unit;

// Another player's corpse, seen where they fell (multiplayer)
#define REMOTE_CORPSES 16
typedef struct RemoteCorpse {
    int      id;    // whose (player id), 0 = none
    uint64_t heard; // last told it's there
    uint64_t asked; // we last asked for its things
    Unit     unit;  // drawn as their figure, dead
} RemoteCorpse;

// Missiles: what skills (and the bow) fire, flying until they hit something or run out; explosions and the spots
// where an area skill (Meteor, Blizzard) lands are missiles too, standing still
#define MAX_MISSILES 384
#define MAX_BUFFS    12
typedef struct Missile {
    bool               active;
    const MissileInfo *info;
    MissileSprite     *sprite;
    float              x; // sub-tiles
    float              y;
    float              dx; // direction (length 1)
    float              dy;
    float              speed; // sub-tiles a second
    float              age;   // seconds
    float              life;
    bool               ours;     // ours (or an ally's): hurts monsters; otherwise a monster's: hurts us
    bool               harmless; // just to be seen (nova rings, explosions, other players' missiles)
    bool               lobbed;   // thrown in an arc to its spot (Fire Blast, Shock Web): flies there, then goes off
    bool               remote;   // another player's: stops (bursting) at the first monster it reaches
    int                damage[ELEMENT_COUNT];
    int                cold_frames;   // how long its cold slows
    int                poison_frames; // over how long its poison works
    float              radius;        // hits everything this close when it goes off (0: just what it hits)
    float              delay;         // an area skill: goes off after this long (stands still until then)
    int                pierce;        // goes through this many more
    int                hits[8];       // units already hit (pierce, novas)
    int                hit_count;
    int                chain; // Chain Lightning: jumps left
    float              ticks; // repeats every this many seconds (Blizzard, fire walls), 0 = once
    float              tick_at;
    float              ar_bonus; // attack rating percent (arrows)
    bool               uses_ar;  // can miss (arrows and javelins)
    const SkillRow    *skill;
} Missile;

// A Town Portal open (SceneRoamPortals.inc)
typedef struct RoamPortal {
    bool     open;
    int      act;   // its town's
    int      place; // where it was opened (Places; ours), its area and the world it's in
    int      level_id;
    uint32_t seed;
    float    x, y;     // its end there
    bool     town_set; // its end in town (placed on arriving there)
    float    town_x, town_y;
} RoamPortal;

typedef struct RoamBuff {
    const SkillRow *skill;
    int             level;
    uint64_t        until;
} RoamBuff;

typedef enum RoamPanel { PANEL_NONE, PANEL_MONSTERS, PANEL_PLACES } RoamPanel;

#define TOWN_COUNT 5
static const int town_level_ids[TOWN_COUNT] = {1, 40, 75, 103, 109}; // Rogue Encampment ... Harrogath

#define CHAT_LINES        6     // multiplayer: chat shown at the top left
#define LOG_LINES         80    // the message log (M): the last this many
#define CHAT_MS           12000 // for this long
#define MAX_AUTOMAP_MARKS 256   // objects marked on the automap (waypoints, shrines, wells, stairs)
#define WAYPOINT_RANGE    8.0f  // Single Player: how close to a waypoint the Waypoint button works
#define MESSAGE_MS        4000  // level up and other messages
#define CAPTION_MS        3500  // "Entering The Blood Moor"; fades out over the last second
#define HARDCORE_DEATH_MS 5000  // time to read the bad news
#define CHAR_PANEL        "/data/global/ui/PANEL/invchar6.dc6"
#define CHAR_PANEL_W      320
#define CHAR_PANEL_H      432

// Items on the ground (Single Player and multiplayer games): kept while the game lasts, per area, like the game's
#define MAX_GROUND_ITEMS NET_GROUND_ITEMS_MAX
#define ITEM_FLIP_MS     40   // the flip through the air, a frame each (the game's 25 a second)
#define PICKUP_RANGE     2.0f // how close the player must be to pick one up
#define ROAM_SHOPS       8    // merchants' stock kept in a game

// What the "how much gold" dialog is for
enum { AMOUNT_FOR_DROP = 1, AMOUNT_FOR_STASH_PUT, AMOUNT_FOR_STASH_TAKE, AMOUNT_FOR_TRADE };
#define ITEM_SPRITES   48 // flippy pictures kept loaded
#define PLACEHOLDER_ID 0xFFFFFFFFu

typedef struct GroundItem {
    Item     item;
    int      map_key; // the map it's on (Roam__MapKey)
    float    x;       // sub-tiles
    float    y;
    uint64_t dropped_at;
    bool     landed; // its landing sound has played
    // Drawing (worked out each frame)
    Sprite  *sprite;
    int      frame;
    bool     on_screen;
    int      screen_x; // where it lies
    int      screen_y;
    SDL_Rect rect;  // its picture on screen
    SDL_Rect label; // its name's box, when shown
    bool     label_shown;
    uint32_t net_id; // multiplayer: the server's id for it
    bool     asked;  // multiplayer: we asked the server for it
} GroundItem;

typedef struct ItemSprite {
    char    file[32];
    Sprite *sprite;
} ItemSprite;

typedef struct SceneRoam {
    bool            free_roam; // Free Roam, or the game (both travel between the game's areas)
    bool            game;      // Single Player: the character from Character_Current(), real combat and levels
    Map            *map;
    bool            borrowed; // the editor's map (play-test): not ours to free
    MapRenderer    *renderer;
    MapUnits       *units_cache; // figures
    MapCamera       camera;
    uint64_t        last_tick;
    unsigned        draw_flags;
    int             place;            // Free Roam: current Places index (the area you're in)
    int             area_level;       // the area you're in (joined-up maps: changes as you walk), Levels.txt Id
    WorldAtmosphere atmosphere;       // that area's: indoors, rain or snow, music, sounds
    uint64_t        ambient_event_at; // the next of its odd sounds (0: not set yet)
    uint64_t        thunder_heard;    // the lightning flash whose thunder was last played (its time)
    bool            night_sounds;     // the area's night sounds are on (outdoors at night)
    RoamPortal      portals[NET_MAX_PLAYERS + 1]; // the Town Portals open: ours [0], the other players' by their id
    int             shrine_code;                  // the shrine booster on (shrines.txt Code; 0 none) and until when
    int             shrine_arg0;
    int             shrine_arg1;
    uint64_t        shrine_until;

    // Player
    Unit     player;
    int      player_class;
    bool     running;
    bool     moving;
    int      attack_target; // index into units, -1 = none
    int      use_target;    // object being walked to, to open it; -1 = none
    float    spawn_x;
    float    spawn_y;
    uint64_t respawn_at;
    Unit     corpse;     // Single Player: where you fell, with what you wore (the character's corpse_*)
    int      corpse_key; // the map it's on (Roam__MapKey), 0 = none
    uint64_t safe_until; // grace period after respawning
    // The Assassin's charge-up attacks: which one holds charges, how many (3 at most), at what level, until when
    const SkillRow *charge_skill;
    int             charges;
    int             charge_level;
    uint64_t        charges_until;
    uint64_t        stamina_until;    // a stamina potion: running doesn't tire you until then
    uint64_t        antidote_until;   // an antidote potion: +50 poison resistance (most 75%) until then
    uint64_t        thawing_until;    // a thawing potion: +50 cold resistance, can't be chilled, until then
    uint64_t        conviction_until; // near a unique's Conviction: your resistances lowered by conviction until then
    int             conviction;
    int             step_frame; // walk frame the last footstep was played on, -1 = none

    Unit units[MAX_UNITS];
    int  unit_count;
    int  hovered; // unit under the mouse, -1 = none

    // UI
    Button   *buttons[6];
    Button   *back_button;
    Button   *class_button;
    Button   *run_button;
    Button   *monsters_button;
    Button   *places_button; // Free Roam
    Button   *clear_button;  // Testing Grounds
    Button   *combat_button;
    Button   *character_button; // Single Player
    Button   *town_button;      // Single Player
    Button   *preview_button;   // in the Monsters panel
    bool      preview;          // show what right click would place, under the mouse
    bool      hover_valid;
    float     hover_x; // sub-tile under the mouse
    float     hover_y;
    RoamPanel panel;
    ListBox  *list;
    char    **list_names;
    int   *list_places; // the places list: the place each row is (Single Player's waypoints list just the found ones)
    int    list_count;
    int    selected;
    Label *title_label;
    Label *status_label;
    Label *help_label;
    char   help_text[400]; // what it says now
    Label *life_label;
    Label *target_label;
    bool   prev_left;
    bool   prev_right;

    TextBox *seed_box; // Free Roam: world seed (in the Places panel)

    // Multiplayer: the other players, chat, and the game clock (game speed)
    bool     online;
    Unit     others[NET_MAX_PLAYERS];
    int      other_ids[NET_MAX_PLAYERS];
    char     other_states[NET_MAX_PLAYERS + 1][UNIT_STATES][32]; // by player id: their buffs and aura (to be seen)
    int      other_state_count[NET_MAX_PLAYERS + 1];
    int      other_colors[NET_MAX_PLAYERS];
    int      other_count;
    uint64_t last_sent;
    uint64_t last_monsters; // the map's owner: monsters last sent at
    uint64_t last_allies;   // our summons last sent at
    int      attack_other;  // PvP: the player being attacked (their id), 0 = none
    uint64_t storm_next;    // Thunder Storm's next bolt
    // Grim Wards: corpses turned into totems the monsters run from, while they last
    struct {
        float    x, y, radius;
        uint64_t until;
    } wards[4];
    // By player id: a party member's aura and warcries, which reach us when we're near them (Might, Battle Orders, ...)
    const SkillRow *party_aura[NET_MAX_PLAYERS + 1];
    int             party_aura_level[NET_MAX_PLAYERS + 1];
    RoamBuff        party_cries[NET_MAX_PLAYERS + 1][4];
    int             party_cry_count[NET_MAX_PLAYERS + 1];
    uint64_t        hostile_mine[NET_MAX_PLAYERS + 1];   // by id: when our hostility to them takes effect (0: at peace)
    uint64_t        hostile_theirs[NET_MAX_PLAYERS + 1]; // by id: when theirs to us does
    bool            loot_allowed[NET_MAX_PLAYERS + 1];   // by id: they may loot our corpse
    bool            loot_from[NET_MAX_PLAYERS + 1];      // by id: we may loot theirs
    RemoteCorpse    remote_corpses[REMOTE_CORPSES];      // the others' corpses on this map
    uint64_t        corpse_sent;                         // ours, last told at
    char            sent_looks[CHARACTER_LOOKS_TEXT];    // what we last told the others we wear
    bool            party_panel;                         // P: the party screen
    bool            use_is_ours; // the server said we're the one opening this object (its treasure is ours to drop)
    int      used_maps[NET_USED_OBJECTS_MAX]; // multiplayer: chests, doors and shrines opened (map, object index)
    int      used_objects[NET_USED_OBJECTS_MAX];
    int      used_count;
    int      hover_other;  // PvP: the other player under the mouse (index into others), -1 = none
    int      spread_place; // the place we were last moved off the arrival spot in (-1: not yet)
    uint64_t clock;        // the game's time (runs faster at Fast, Faster, Fastest)
    uint64_t clock_real;   // the real time it was last moved on at
    TextBox *chat_box;
    Label   *chat_label;
    Label   *log_label; // the message log and the help screen
    char     log_lines[LOG_LINES][NET_CHAT_MAX + 24];
    bool     log_chat[LOG_LINES]; // said by someone (white), or a message (gold)
    int      log_count;
    bool     log_open;
    bool     help_open;
    bool     hide_portraits; // Show portraits (Z) off
    bool     show_belt;      // Show belt: the belt's rows above the bottom one

    bool automap_fade; // the automap keys (F9-F12): see-through, no party, no names
    bool automap_no_party;
    bool automap_no_names;
    int  automap_size; // 0 as Options say, 1 full view, 2 corner (Center automap)

    char     chat_lines[CHAT_LINES][NET_CHAT_MAX + 24];
    uint64_t chat_times[CHAT_LINES];
    Button  *seed_use_button;
    Button  *seed_random_button;
    Label   *seed_label;
    int      long_walk_legs; // searches so far on the way to a far click
    // Free Roam's speed readout: where you were half a second ago, and the speed then
    float          speed_from_x;
    float          speed_from_y;
    uint64_t       speed_from_time;
    float          speed;        // sub-tiles per second, over the ground
    float          screen_speed; // pixels per second on screen (at zoom 1)
    float          key_dx; // Modern / Combined movement: where the movement keys (or stick) point, sub-tiles; 0 = none
    float          key_dy;
    bool           sprinting; // Free Roam: sprint (Caps Lock, or the Walk / Run / Sprint button)
    bool           menu_open; // the game menu (Esc): Options, leave, back to the game; the world waits
    Button        *menu_options_button;
    Button        *menu_leave_button;
    Button        *menu_return_button;
    OptionsPanel  *options_panel; // Options opened from that menu
    ConfirmDialog *confirm;       // "Return to the main menu?" (Back and Leave in Free Roam / Testing Grounds)
    AmountDialog  *amount;        // "How much gold?" (AMOUNT_FOR_*)
    int            stash_object;  // the stash being used (a unit index), -1 = none
    int            difficulty;    // Single Player: 0 Normal, 1 Nightmare, 2 Hell (multiplayer: the game's setting)
    // Town folk (SceneRoamNpc.inc): the menu of what the one you walked up to does, merchants' stock, the store open
    int        npc_menu; // unit index, -1 = none
    int        npc_options[HIRE_OFFERS + 12];
    int        npc_option_count;
    ShopStock *shops[ROAM_SHOPS];
    int        shop_count;
    ShopStock *gamble; // a gambler's (rolled again each time)
    // Talking (SceneRoamNpc.inc): what they say, scrolling in a box while the speech plays, and whose introductions
    // you've heard this game
    char         speech_text[3000];
    uint64_t     speech_at;
    int          speech_ms;
    int          speech_npc;   // unit index, -1 = nobody's talking
    bool         text_mode;    // shown as text (AbyssEngine --text): speech goes to the message line
    int          text_view;    // text mode: 0 the map, 1 your items, 2 a store
    int          text_pick;    // your items: the one picked (-1 none)
    bool         text_automap; // m: the map shown a tile a letter, what you've seen of the area
    bool         text_quiet;   // b: no bell when you're hit or your life is low
    int          text_last_hp; // (the bell: life last frame)
    uint64_t     text_warned;  // (the bell: when low life last rang it)
    ShopStock   *open_stock;   // the store open now (text mode reads it)
    char         npc_heard[32][40];
    int          npc_heard_count;
    int          council_killed;    // the Blackened Temple: High Council super uniques killed this game
    int          quest_objects_key; // the map the quests' missing objects were put in (key + 1)
    bool         ancients_called;   // Rite of Passage: the Ancients are up (this game)
    bool         hire_open;         // the hireling's screen (click their portrait)
    HirelingView hire_view;
    int          prisons_broken; // Rescue on Mount Arreat: prison doors broken (this game)
    int          shop_npc;       // trading with (unit index), -1 = none
    // Trading with another player (SceneRoamTrade.inc)
    int        trade_state;
    int        trade_with;     // their player id
    int        trade_asked_by; // who asked us (0: nobody)
    uint32_t   trade_my_version;
    uint64_t   trade_wait_until; // their offer changed: Accept waits until then
    uint32_t   trade_their_version;
    TradeView *trade;
    HireOffer  hire_offers[HIRE_OFFERS];
    uint64_t   quest_check_at; // quests (SceneRoamQuests.inc): when to look again
    int        quest_left;     // monsters left in the area being cleared
    bool       quest_panel;    // the quest log (Q)
    int        quest_act;      // its tab
    int        hire_offer_count;
    char       hire_seller[40];
    int        question;        // what it asks: ROAM_ASK_LEAVE, ROAM_ASK_NEW_SEED, ROAM_ASK_ENTER
    int        question_level;  // ROAM_ASK_ENTER: where the entrance leads
    int        hover_exit;      // the entrance under the mouse (its name is shown), -1 = none
    bool       screenshot;      // take one at the end of this frame's drawing
    bool       attack_in_place; // the swing under way was from where you stand: don't walk after the target
    bool exits_armed; // walked clear of every exit since arriving (so arriving on one doesn't send you straight back)

    // Single Player
    Character     *character;
    float          mana; // kept as fractions while regenerating
    float          stamina;
    bool           exhausted; // ran out of stamina: walking until it's back up a bit
    bool           in_town;
    bool           char_panel; // the character screen
    Sprite        *char_panel_sprite;
    bool           inventory_panel; // the inventory (right)
    bool           skill_panel;     // the skill tree (right, instead of the inventory)
    GameInventory *inventory_ui;
    GameSkills    *skills_ui;
    bool           ui_prev_left; // the mouse buttons last frame, for the panels' clicks
    bool           ui_prev_right;
    bool           ui_owns_press; // a press that started on the panels stays theirs until the button is let go
    float          potion_life;   // still to come from the potions drunk, and how fast
    float          potion_life_rate;
    float          potion_mana;
    float          potion_mana_rate;
    float          life_carry; // parts of a life point healed so far
    // Single Player: the game's control panel (globes, run/walk, stamina, experience, mini panel)
    Sprite  *ctrl_panel;
    Sprite  *ctrl_globes;  // the red and blue fills
    Sprite  *ctrl_overlap; // the glass over them
    Sprite  *panel_close;  // the inventory's and skill tree's close button (buysellbtn.dc6)
    Sprite  *ctrl_run;
    Sprite  *ctrl_menu; // the arrow that opens the mini panel
    Sprite  *ctrl_mini;
    Sprite  *ctrl_mini_buttons;
    Sprite  *ctrl_level; // "new stats"
    Sprite  *ctrl_skill; // skill icons
    bool     mini_open;
    int      ctrl_pressed;      // the panel button held down (drawn pressed), -1 = none
    Label   *panel_label;       // small text on the character screen
    Label   *panel_value_label; // its numbers
    Label   *message_label;     // "Level up!", ...
    Automap *automap;           // Tab
    bool     show_automap;
    Label   *caption_label; // "Entering The Blood Moor"
    uint64_t caption_until;
    bool     announce; // this teleport is walking through an exit: "Entering ..." (not spawning or waypoints)
    char     message[96];
    uint64_t message_until;
    uint64_t leave_at; // hardcore death: back to the character list at this time

    // Items on the ground
    GroundItem *ground;
    int         ground_count;
    int         item_target; // the ground item being walked to, to pick it up; -1 = none
    int         hover_item;  // the ground item under the mouse (its picture or name), -1 = none
    ItemSprite  item_sprites[ITEM_SPRITES];
    int         item_sprite_count;
    int         item_sprite_act; // the palette they were loaded with
    Label      *item_label;

    // Skills
    Missile         missiles[MAX_MISSILES];
    RoamBuff        buffs[MAX_BUFFS];
    int             buff_count;
    const SkillRow *cast_skill; // being cast now (NULL: the basic attack or nothing)
    int             cast_level;
    float           cast_x; // where it's aimed (sub-tiles)
    float           cast_y;
    int             cast_target; // the monster aimed at, -1 = none
    bool            cast_fired;  // its effect has happened (half way through the animation)
    const SkillRow *melee_skill; // the skill the swing under way uses (Bash, Jab, ...), NULL = a plain attack
    int             melee_level;
    float           bone_armor; // damage it still soaks up
    uint64_t        aura_tick;  // the aura's next pulse (Prayer's healing, Holy Fire's damage, ...)
    uint64_t        next_cast;  // holding the button: when to cast again
} SceneRoam;

static void Roam__TogglePanel(SceneRoam *roam, RoamPanel panel);

DEFINE_SCENE_CALLBACKS(TestingGrounds);
DEFINE_SCENE_CALLBACKS(FreeRoam);
DEFINE_SCENE_CALLBACKS(Game);

// Fighting is on: always in the game, by the debug switch elsewhere
static bool Roam__Combat(const SceneRoam *roam) { return roam->game || combat_enabled; }

// Single Player / Multiplayer with Options › Debug › "Free Roam in Single Player and Multiplayer": the Places list
// from anywhere (not only at a waypoint) and sprint
static bool Roam__DebugTools(const SceneRoam *roam) { return roam->game && Settings_GetDebug()->free_roam_in_game; }

// Town folk (Akara, the rogue scouts in the encampment, ...) never fight and can't be attacked, whatever the combat
// switch says: it's for monsters
static bool Roam__IsPeaceful(const SceneRoam *roam, const struct Unit *unit) {
    (void)roam;
    return unit->npc;
}

// ---------------------------------------------------------------------------------------------------------------------
// Units

static float Roam__Distance(const float ax, const float ay, const float bx, const float by) {
    return sqrtf(((ax - bx) * (ax - bx)) + ((ay - by) * (ay - by)));
}

static void Roam__TurnTowards(int *turn, const float from_x, const float from_y, const float to_x, const float to_y) {
    float ax;
    float ay;
    float bx;
    float by;
    MapView_SubtileToWorld(from_x, from_y, &ax, &ay);
    MapView_SubtileToWorld(to_x, to_y, &bx, &by);
    if (fabsf(bx - ax) + fabsf(by - ay) > 0.01f) {
        *turn = MapUnits_TurnTowards(bx - ax, by - ay);
    }
}

static bool Roam__IsPlayer(const SceneRoam *roam, const Unit *unit) { return unit == &roam->player; }

// The game's time now (what Roam__Update's "now" is: faster than real time at Fast and up in multiplayer), for
// timers set and checked outside the update
// A buff that reaches the party around you too: the Barbarian's warcries Battle Orders, Shout and Battle Command
static bool Roam__PartySkill(const SkillRow *skill) {
    return skill != NULL && (strcmp(skill->id, "Battle Orders") == 0 || strcmp(skill->id, "Shout") == 0 ||
                             strcmp(skill->id, "Battle Command") == 0);
}

static uint64_t Roam__Now(const SceneRoam *roam) {
    return roam->online && roam->clock != 0 ? roam->clock : SDL_GetTicks64();
}

// (a monster under the ground counts as gone until it comes up: it can't be seen, hit or targeted)
static bool Roam__IsAlive(const Unit *unit) {
    return unit->state != UNIT_DYING && unit->state != UNIT_DEAD && !unit->burrowed;
}

// Monsters and NPCs can fight; objects can't
static bool Roam__IsCombatant(const Unit *unit) { return unit->max_hp > 0; }

// Running: the Run / walk switch, flipped while the run key is held
static bool Roam__IsRunning(const SceneRoam *roam) {
    return roam->sprinting || ((roam->running != Controls_Held(ACTION_RUN_HOLD)) && !roam->exhausted);
}

// The player's speed: walking, running or (Free Roam) sprinting
// What a monster's curse on the player does to one of its stats (skills.txt aura stats: "damagepercent" for Weaken,
// "damageresist" for Amplify Damage, "velocitypercent" for Decrepify), in percent; 0 when not cursed
static int Roam__PlayerCurse(const SceneRoam *roam, const char *stat) {
    const Unit *player = &roam->player;
    if (!roam->game || player->curse == NULL || Roam__Now(roam) >= player->curse_until) {
        return 0;
    }
    int total = 0;
    for (int a = 0; a < SKILL_AURA_STATS; a++) {
        if (strcmp(player->curse->aura_stat[a], stat) == 0) {
            total += SkillData_Eval(roam->character, player->curse, player->curse_level, player->curse->aura_calc[a]);
        }
    }
    return total < -300 ? -300 : (total > 300 ? 300 : total); // (a curse's percent, whatever its level)
}

static float Roam__PlayerSpeed(const SceneRoam *roam) {
    if (roam->sprinting) {
        return SPRINT_SPEED;
    }
    const float speed = Roam__IsRunning(roam) ? RUN_SPEED : WALK_SPEED;
    // Faster Run/Walk on what's worn; slower under Decrepify, or chilled (a monster's cold, Holy Freeze)
    const float chilled = roam->game && Roam__Now(roam) < roam->player.chill_until ? 0.6f : 1.0f;
    return roam->game ? speed * chilled *
                            fmaxf(0.1f, 1.0f + ((float)(Affixes_Total(roam->character, MOD_FASTER_RUN) +
                                                        roam->character->bonus[BONUS_SPEED_PCT] +
                                                        Roam__PlayerCurse(roam, "velocitypercent")) /
                                                100.0f))
                      : speed;
}

// How fast you're going, over the last half second: over the ground (sub-tiles) and on screen (pixels)
static void Roam__MeasureSpeed(SceneRoam *roam, const uint64_t now) {
    if (roam->speed_from_time == 0 || now < roam->speed_from_time) {
        roam->speed_from_time = now;
        roam->speed_from_x    = roam->player.x;
        roam->speed_from_y    = roam->player.y;
        return;
    }
    const uint64_t elapsed = now - roam->speed_from_time;
    if (elapsed < 500) {
        return;
    }
    const float dx  = roam->player.x - roam->speed_from_x;
    const float dy  = roam->player.y - roam->speed_from_y;
    const float sec = (float)elapsed / 1000.0f;
    float       wx0;
    float       wy0;
    float       wx1;
    float       wy1;
    MapView_SubtileToWorld(roam->speed_from_x, roam->speed_from_y, &wx0, &wy0);
    MapView_SubtileToWorld(roam->player.x, roam->player.y, &wx1, &wy1);
    const float distance = sqrtf((dx * dx) + (dy * dy));
    // Teleports and area changes aren't walking
    roam->speed = distance / sec > 40.0f ? 0.0f : distance / sec;
    roam->screen_speed =
        roam->speed == 0.0f ? 0.0f : sqrtf(((wx1 - wx0) * (wx1 - wx0)) + ((wy1 - wy0) * (wy1 - wy0))) / sec;
    roam->speed_from_time = now;
    roam->speed_from_x    = roam->player.x;
    roam->speed_from_y    = roam->player.y;
}

// The settings for the class being played (or all classes')
static int Roam__Profile(const SceneRoam *roam) {
    if (roam->game) {
        return roam->character != NULL ? (int)roam->character->class_id + 1 : 0; // (all classes' until it's loaded)
    }
    return roam->player_class + 1;
}

static const GameplaySettings *Roam__Gameplay(const SceneRoam *roam) {
    return Settings_GetGameplay(Roam__Profile(roam));
}

// The player's town animations (relaxed) when nobody's fighting: in the game that's in town
static bool Roam__PlayerAtEase(const SceneRoam *roam) { return roam->game ? roam->in_town : !combat_enabled; }

static const char *Roam__Mode(const SceneRoam *roam, const Unit *unit) {
    if (unit->net_mode[0] != '\0') {
        return unit->net_mode; // another player: as their game shows them
    }
    const bool player = Roam__IsPlayer(roam, unit);
    switch (unit->state) {
    case UNIT_WALK:
        if (player) {
            return Roam__IsRunning(roam) ? "rn" : (Roam__PlayerAtEase(roam) ? "tw" : "wl");
        }
        return "wl";
    case UNIT_ATTACK:
        return "a1";
    case UNIT_HIT:
        return "gh";
    case UNIT_DYING:
        return "dt";
    case UNIT_DEAD:
        return "dd";
    case UNIT_OPERATE:
        return "op";
    case UNIT_CAST:
        return unit->cast_mode[0] != '\0' ? unit->cast_mode : "sc";
    case UNIT_IDLE:
        break;
    }
    if (unit->is_object) {
        return unit->object_mode;
    }
    if (player) {
        return Roam__PlayerAtEase(roam) ? "tn" : "nu";
    }
    return unit->idle_mode;
}

// A player in what they wear, for one animation: NULL if the class has no such animation with that weapon
static Figure *Roam__LooksFigure(SceneRoam *roam, const Unit *unit, const char *mode, const bool bare) {
    FigureSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.base = "chars";
    snprintf(spec.token, sizeof(spec.token), "%.7s", unit->id);
    snprintf(spec.weapon_class, sizeof(spec.weapon_class), "%s", bare ? "hth" : unit->looks.weapon_class);
    for (int c = 0; c < COF_COMPONENT_COUNT && c < CHARACTER_LOOK_LAYERS; c++) {
        if (!bare || (c != COF_COMPONENT_RH && c != COF_COMPONENT_LH)) {
            snprintf(spec.armor[c], sizeof(spec.armor[c]), "%s", unit->looks.layer[c]);
            spec.tint[c] = unit->looks.tint[c]; // (what's worn's colour)
        }
    }
    return MapUnits_GetSpecFigure(roam->units_cache, &spec, mode);
}

// The figure for the unit's current animation, falling back to standing still when it has no such animation
static Figure *Roam__Figure(SceneRoam *roam, const Unit *unit) {
    // The Druid in a shape (Werewolf, Werebear: ours from the buff, the others' from their looks): the wolf's or
    // bear's pictures, in their animations
    if (unit->has_looks && unit->looks.shape != 0) {
        const char *shape  = unit->looks.shape == 'b' ? "bear" : "wolf";
        const char *mode   = Roam__Mode(roam, unit);
        const char *as     = strcmp(mode, "tn") == 0   ? "nu"
                             : strcmp(mode, "tw") == 0 ? "wl"
                             : strcmp(mode, "sc") == 0 ? "a1"
                                                       : mode;
        Figure     *figure = MapUnits_GetFigure(roam->units_cache, "monsters", shape, as);
        if (figure == NULL && strcmp(as, "rn") == 0) {
            figure = MapUnits_GetFigure(roam->units_cache, "monsters", shape, "wl");
        }
        if (figure == NULL) {
            figure = MapUnits_GetFigure(roam->units_cache, "monsters", shape, "nu");
        }
        if (figure != NULL) {
            return figure;
        }
    }
    if (unit->has_looks) {
        const char *mode   = Roam__Mode(roam, unit);
        Figure     *figure = Roam__LooksFigure(roam, unit, mode, false);
        if (figure == NULL && unit->state == UNIT_DEAD) {
            figure = Roam__LooksFigure(roam, unit, "dt", false);
        }
        if (figure == NULL) {
            figure = Roam__LooksFigure(roam, unit, Roam__IsPlayer(roam, unit) ? "tn" : "nu", false);
        }
        if (figure == NULL) {
            figure = Roam__LooksFigure(roam, unit, mode, true); // the weapon has no such animation: bare handed
        }
        if (figure != NULL) {
            return figure;
        }
    }
    Figure *figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, Roam__Mode(roam, unit));
    if (figure == NULL && unit->state == UNIT_DEAD) {
        figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "dt"); // last frame of dying
    }
    if (figure == NULL) {
        figure = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id,
                                    Roam__IsPlayer(roam, unit) ? "tn" : unit->idle_mode);
    }
    return figure;
}

// Frames of the animation shown: [first, first + count). Objects' files often hold every state's frames (a chest's
// closed and open pictures): objects.txt says which ones each animation uses.
static int Roam__FrameRange(const Unit *unit, const Figure *figure, int *first) {
    int count = Figure_GetFramesPerDirection(figure);
    *first    = 0;
    if (unit->is_object) {
        const int mode = unit->state == UNIT_OPERATE ? 1 : Objects_ModeIndex(unit->object_mode);
        Objects_FrameRange(&unit->object, mode, count, first, &count);
    }
    return count;
}

static void Roam__SetState(Unit *unit, const UnitState state, const uint64_t now) {
    if (unit->state == state) {
        return;
    }
    unit->state      = state;
    unit->frame      = 0;
    unit->frame_time = now;
}

// Advances the animation. Returns true when a one-shot animation (attack, hit, dying) just finished.
static bool Roam__Animate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    Figure *figure = Roam__Figure(roam, unit);
    if (figure == NULL) {
        return unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING ||
               unit->state == UNIT_OPERATE || unit->state == UNIT_CAST;
    }
    int       first  = 0; // (where the frames start doesn't matter for timing)
    const int frames = Roam__FrameRange(unit, figure, &first);
    (void)first;
    int ms = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
    if (roam->game && unit == &roam->player && (unit->state == UNIT_ATTACK || unit->state == UNIT_HIT)) {
        // Increased Attack Speed and Faster Hit Recovery: quicker swings and getting over blows sooner (with the
        // game's diminishing returns: 120 x / (120 + x))
        const int raw       = unit->state == UNIT_ATTACK ? Affixes_Total(roam->character, MOD_ATTACK_SPEED) +
                                                         roam->character->bonus[BONUS_ATTACK_SPEED]
                                                         : Affixes_Total(roam->character, MOD_HIT_RECOVERY);
        const int effective = raw > 0 ? 120 * raw / (120 + raw) : 0;
        ms                  = ms * 100 / (100 + effective);
        ms                  = ms < 10 ? 10 : ms;
    }
    const bool one_shot = unit->state == UNIT_ATTACK || unit->state == UNIT_HIT || unit->state == UNIT_DYING ||
                          unit->state == UNIT_OPERATE || unit->state == UNIT_CAST;
    if (unit->state == UNIT_DEAD) {
        unit->frame = frames > 0 ? frames - 1 : 0;
        return false;
    }
    if (now - unit->frame_time > 1000) {
        unit->frame_time = now - (uint64_t)ms; // after a pause, don't race through frames
    }
    while (frames > 0 && now - unit->frame_time >= (uint64_t)ms) {
        unit->frame_time += (uint64_t)ms;
        if (++unit->frame >= frames) {
            if (one_shot) {
                unit->frame = frames - 1;
                return true;
            }
            unit->frame = 0;
        }
    }
    return false;
}

static int Roam__Roll(const int low, const int high) { return high > low ? low + (rand() % (high - low + 1)) : low; }

typedef enum UnitRank { RANK_NORMAL, RANK_CHAMPION, RANK_UNIQUE, RANK_SUPER_UNIQUE, RANK_MINION } UnitRank;

// Monsters that make others (monstats spawn and AI): eggs hatch when you come near, nests and mothers keep making them
enum { SPAWNER_NONE, SPAWNER_EGG, SPAWNER_NEST };

// The game's kinds of champion: plain, Fanatic (fast), Berserker (hits hard, little defense), Ghostly (see-through,
// slow, cold, half physical damage), Possessed (Hell only: twice the life)
enum { CHAMPION_PLAIN, CHAMPION_FANATIC, CHAMPION_BERSERKER, CHAMPION_GHOSTLY, CHAMPION_POSSESSED };

static int init_area_level = 0; // Roam__Populate: the area's monster level for the monsters being made (0: theirs)
static int init_players    = 1; // Single Player's /players setting: monsters made with half as much life and
                                // experience again for each player after the first (multiplayer: 1)

// An object named the game's way: objects.txt's Name through the string tables ("chest" -> "Chest")
static void Roam__NameObject(Unit *unit) {
    const char *shown = unit->object.name != NULL ? StringTable_Find(unit->object.name) : NULL;
    if (shown != NULL && shown[0] != '\0') {
        snprintf(unit->name, sizeof(unit->name), "%s", shown);
    } else if (unit->object.name != NULL && unit->object.name[0] != '\0') {
        snprintf(unit->name, sizeof(unit->name), "%s", unit->object.name);
    }
}

static bool Roam__InitUnit(Unit *unit, const char *base, const char *id, const float x, const float y) {
    CatalogCharacterInfo info;
    if (!CatalogCharacters_Find(MapSession_GetBestiary(), base, id, &info)) {
        return false; // no graphics
    }
    memset(unit, 0, sizeof(Unit));
    snprintf(unit->base, sizeof(unit->base), "%s", base);
    snprintf(unit->id, sizeof(unit->id), "%s", id);
    snprintf(unit->name, sizeof(unit->name), "%s", info.name);
    unit->idle_mode  = info.idle_mode;
    unit->x          = x;
    unit->y          = y;
    unit->target_x   = x;
    unit->target_y   = y;
    unit->turn       = rand() % 16;
    unit->home_x     = x;
    unit->home_y     = y;
    unit->can_walk   = -1;
    unit->can_attack = -1;
    unit->rest_frame = -1;
    unit->state      = UNIT_IDLE;
    unit->net_state  = -1;

    MonsterStats stats;
    if (strcmp(base, "monsters") == 0 && MonsterStats_GetAtLevel(id, init_area_level, &stats)) {
        unit->max_hp        = Roam__Roll(stats.hp_min, stats.hp_max > stats.hp_min ? stats.hp_max : stats.hp_min);
        unit->max_hp        = unit->max_hp > 0 ? unit->max_hp : 20;
        unit->damage_min    = stats.damage_min > 0 ? stats.damage_min : 1;
        unit->damage_max    = stats.damage_max > unit->damage_min ? stats.damage_max : unit->damage_min + 2;
        unit->speed         = (float)(stats.velocity > 0 ? stats.velocity : 3);
        unit->npc           = stats.npc;
        unit->level         = stats.level > 0 ? stats.level : 1;
        unit->defense       = stats.defense;
        unit->attack_rating = stats.attack_rating > 0 ? stats.attack_rating : 10;
        unit->experience    = stats.experience;
        if (init_players > 1 && !stats.npc) { // (/players)
            unit->max_hp     = unit->max_hp * (100 + (50 * (init_players - 1))) / 100;
            unit->experience = unit->experience * (100 + (50 * (init_players - 1))) / 100;
        }
        unit->undead = stats.undead;
        if (stats.spawn[0] != '\0') {
            snprintf(unit->spawn, sizeof(unit->spawn), "%s", stats.spawn);
            unit->spawner = strcmp(stats.ai, "MaggotEgg") == 0 ? SPAWNER_EGG
                            : (strstr(stats.ai, "Nest") != NULL || strcmp(stats.ai, "VileMother") == 0 ||
                               strcmp(stats.ai, "SandMaggotQueen") == 0 || strcmp(stats.ai, "MinionSpawner") == 0)
                                ? SPAWNER_NEST
                                : SPAWNER_NONE;
        }
        unit->suicide = strcmp(stats.ai, "SuicideMinion") == 0;
        unit->demon   = stats.demon;
        unit->boss    = stats.boss;
        snprintf(unit->treasure, sizeof(unit->treasure), "%s", stats.treasure);
        memcpy(unit->resist, stats.resist, sizeof(unit->resist));
        unit->element        = stats.element;
        unit->element_chance = stats.element_chance;
        unit->element_min    = stats.element_min;
        unit->element_max    = stats.element_max;
        // Archers' arrows; casters' bolts and novas (a skill with a missile that the engine has)
        if (stats.missile[0] != '\0' && MissileData_Get(stats.missile) != NULL) {
            snprintf(unit->shot, sizeof(unit->shot), "%s", stats.missile);
        }
        // Its spells: every missile and nova among its skills (bosses have several: Mephisto's lightning, bolts,
        // poison nova and frost nova), one picked each time; an aura among them is always on (Duriel's Holy Freeze)
        for (int s = 0; s < MONSTER_SKILLS; s++) {
            const SkillRow *spell = stats.skill[s][0] != '\0' ? SkillData_Get(stats.skill[s]) : NULL;
            const char     *name = spell != NULL ? (spell->missile[0] != '\0' ? spell->missile : spell->missile_a) : "";
            const int       level = stats.skill_level[s] > 0 ? stats.skill_level[s] : 1;
            if (spell != NULL && (spell->kind == SKILL_KIND_MISSILE || spell->kind == SKILL_KIND_NOVA) &&
                MissileData_Get(name) != NULL && unit->spell_count < 4) {
                unit->spells[unit->spell_count]         = spell;
                unit->spell_levels[unit->spell_count++] = level;
                if (unit->spell == NULL) {
                    unit->spell       = spell;
                    unit->spell_level = level;
                }
            } else if (spell != NULL && spell->kind == SKILL_KIND_AURA && unit->aura == NULL && stats.boss) {
                unit->aura       = spell;
                unit->aura_level = level;
            }
        }
        // Curses it lays on you, and how it gets to you (monstats Skill1..8)
        for (int s = 0; s < MONSTER_SKILLS; s++) {
            const char *name = stats.skill[s];
            if (strcmp(name, "Amplify Damage") == 0 || strcmp(name, "Weaken") == 0 || strcmp(name, "Decrepify") == 0 ||
                strcmp(name, "MonCurseCast") == 0) {
                const SkillRow *row = SkillData_Get(strcmp(name, "MonCurseCast") == 0 ? "Amplify Damage" : name);
                if (unit->hex == NULL) {
                    unit->hex = row;
                } else if (unit->hex2 == NULL) {
                    unit->hex2 = row; // (two at most)
                }
                unit->hex_any   = unit->hex_any || strcmp(name, "MonCurseCast") == 0;
                unit->hex_level = stats.skill_level[s] > 0 ? stats.skill_level[s] : 1;
            } else if (strcmp(name, "MonTeleport") == 0 || strcmp(name, "Imp Teleport") == 0 ||
                       strcmp(name, "Teleport 2") == 0) {
                unit->move_skill = MOVE_SKILL_TELEPORT;
            } else if (strcmp(name, "Leap") == 0 || strcmp(name, "Charge") == 0) { // (Duriel charges like a leap)
                unit->move_skill = MOVE_SKILL_LEAP;
            } else if (strcmp(name, "Submerge") == 0) {
                unit->move_skill = MOVE_SKILL_BURROW;
            }
        }
    } else if (strcmp(base, "chars") == 0) {
        unit->max_hp     = 50;
        unit->damage_min = 2;
        unit->damage_max = 6;
        unit->speed      = WALK_SPEED;
    }
    unit->speed = unit->speed < 2.0f ? 2.0f : (unit->speed > 9.0f ? 9.0f : unit->speed);
    unit->hp    = unit->max_hp;
    if (strcmp(base, "objects") == 0 && Objects_Get(Objects_FindToken(id), &unit->object)) {
        unit->is_object = true;
        unit->turn      = 0;
        Roam__NameObject(unit);
        if (unit->object.row == 251) {
            unit->object.selectable = true; // the Gidbinn's altar (its quest uses it)
        }
        snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.idle_mode);
    }
    return true;
}

static Unit *Roam__AddUnit(SceneRoam *roam, const char *base, const char *id, const float x, const float y) {
    if (roam->unit_count >= MAX_UNITS) {
        return NULL;
    }
    Unit *unit = &roam->units[roam->unit_count];
    if (!Roam__InitUnit(unit, base, id, x, y)) {
        return NULL;
    }
    roam->unit_count++;
    return unit;
}

// One step towards (tx, ty); slides along walls. Returns false when blocked or arrived.
// "The same speed on screen" (Options › Gameplay): how much faster or slower to go this way over the ground so it's
// as fast on screen as going along a tile's edge. Isometric tiles are twice as wide as tall, so the same ground speed
// looks twice as fast sideways as up and down; the game keeps the ground speed, which is the default here too.
static float Roam__ScreenSpeedFactor(const float dx, const float dy) {
    const float length = sqrtf((dx * dx) + (dy * dy));
    if (length < 0.0001f) {
        return 1.0f;
    }
    const float sx     = ((dx - dy) / length) * 16.0f; // screen pixels per sub-tile this way
    const float sy     = ((dx + dy) / length) * 8.0f;
    const float screen = sqrtf((sx * sx) + (sy * sy));
    return screen > 0.0001f ? 17.888544f / screen : 1.0f; // 17.9: along a tile edge (16, 8)
}

static bool Roam__Step(const Map *map, Unit *unit, const float tx, const float ty, const float speed,
                       const float seconds) {
    const float dx       = tx - unit->x;
    const float dy       = ty - unit->y;
    const float distance = sqrtf((dx * dx) + (dy * dy));
    if (distance < ARRIVE_DISTANCE) {
        return false;
    }
    const float step   = speed * seconds * (unit->even_speed ? Roam__ScreenSpeedFactor(dx, dy) : 1.0f);
    const float amount = step >= distance ? 1.0f : step / distance;
    const float next_x = unit->x + (dx * amount);
    const float next_y = unit->y + (dy * amount);

    if (Map_IsWalkable(map, (int)floorf(next_x), (int)floorf(next_y))) {
        unit->x = next_x;
        unit->y = next_y;
    } else if (Map_IsWalkable(map, (int)floorf(next_x), (int)floorf(unit->y))) {
        unit->x = next_x;
    } else if (Map_IsWalkable(map, (int)floorf(unit->x), (int)floorf(next_y))) {
        unit->y = next_y;
    } else {
        return false;
    }
    return true;
}

// Plans a path to (tx, ty) when there is none yet, the goal moved, or it's time to look again
static void Roam__PlanPath(const Map *map, Unit *unit, const float tx, const float ty, const uint64_t now,
                           const uint64_t interval) {
    const bool moved_goal = Roam__Distance(tx, ty, unit->path_goal_x, unit->path_goal_y) > REPATH_DISTANCE;
    if (unit->path.next < unit->path.count && !moved_goal && now < unit->next_repath) {
        return;
    }
    if (!moved_goal && unit->path.next >= unit->path.count && now < unit->next_repath) {
        return; // tried recently and got nowhere
    }
    unit->path_goal_x = tx;
    unit->path_goal_y = ty;
    unit->next_repath = now + interval;
    Map_FindPath(map, unit->x, unit->y, tx, ty, &unit->path);
}

// Walks along the planned path. Returns false when it has arrived or is stuck.
static bool Roam__FollowPath(const Map *map, Unit *unit, const float speed, const float seconds) {
    while (unit->path.next < unit->path.count) {
        const float px = unit->path.x[unit->path.next];
        const float py = unit->path.y[unit->path.next];
        if (Roam__Distance(unit->x, unit->y, px, py) < ARRIVE_DISTANCE) {
            unit->path.next++;
            continue;
        }
        Roam__TurnTowards(&unit->turn, unit->x, unit->y, px, py);
        if (Roam__Step(map, unit, px, py, speed, seconds)) {
            return true;
        }
        unit->path.count = 0; // blocked
        return false;
    }
    return false;
}

static void Roam__ClearPath(Unit *unit) {
    unit->path.count  = 0;
    unit->path.next   = 0;
    unit->next_repath = 0;
}

// Whether the unit has graphics for an animation. A unit only does what it can be shown doing: no attack animation,
// no attacking (Gheed, Akara, ...); no get-hit animation, no flinching; no death animation, it drops straight dead.
static bool Roam__HasMode(SceneRoam *roam, const Unit *unit, const char *mode) {
    if (Roam__IsPlayer(roam, unit)) {
        return true; // players have every animation
    }
    // The Bestiary's list: the animations monstats2 says it has, that also have graphics (some, like Gheed's
    // attack, have files but aren't used by the game)
    return CatalogCharacters_HasMode(MapSession_GetBestiary(), unit->base, unit->id, mode);
}

static bool Roam__CanAttack(SceneRoam *roam, Unit *unit) {
    if (unit->can_attack < 0) {
        unit->can_attack = Roam__HasMode(roam, unit, "a1") ? 1 : 0;
    }
    return unit->can_attack == 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Sounds

// Swings, getting hit, dying: the player's class sounds (bare-handed swing), a monster's from MonSounds.txt
static void Roam__UnitSound(const SceneRoam *roam, const Unit *unit, const MonsterSound which) {
    if (Roam__IsPlayer(roam, unit)) {
        if (which == MONSTER_SOUND_ATTACK) {
            Sounds_Play("weapon_punch_1", SOUND_KIND_SFX);
            return;
        }
        char name[64];
        snprintf(name, sizeof(name), "%s_%s_1", player_classes[roam->player_class][2],
                 which == MONSTER_SOUND_HIT ? "hit" : "death");
        for (char *c = name; *c != '\0'; c++) {
            *c = (char)tolower((unsigned char)*c);
        }
        Sounds_Play(name, SOUND_KIND_SFX);
        return;
    }
    if (strcmp(unit->base, "monsters") == 0) {
        // From where the monster is: quieter far off, from its side of the screen
        const char *name = Sounds_MonsterSound(unit->id, which);
        if (name != NULL) {
            Sounds_PlayAt(name, SOUND_KIND_SFX, unit->x - roam->player.x, unit->y - roam->player.y);
        }
    }
}

// The footstep for the floor under the player: "light_walk_dirt_1", "light_run_snow_1", ...
static void Roam__Footstep(const SceneRoam *roam) {
    const uint16_t material = Map_GetFloorMaterial(roam->map, (int)floorf(roam->player.x), (int)floorf(roam->player.y));
    const char    *surface  = "dirt";
    if (material & (DT1_MATERIAL_WATER | DT1_MATERIAL_LAVA)) {
        return; // no footsteps for these
    }
    if (material & DT1_MATERIAL_SNOW) {
        surface = "snow";
    } else if (material & DT1_MATERIAL_SAND) {
        surface = "sand";
    } else if (material & DT1_MATERIAL_WOOD) {
        surface = "wood";
    } else if (material & DT1_MATERIAL_OUTSIDE_STONE) {
        surface = "ostone";
    } else if (material & DT1_MATERIAL_INSIDE_STONE) {
        surface = "istone";
    }
    char name[48];
    snprintf(name, sizeof(name), "light_%s_%s_1", Roam__IsRunning(roam) ? "run" : "walk", surface);
    Sounds_Play(name, SOUND_KIND_SFX);
}

static void Roam__DropTreasure(SceneRoam *roam, const char *treasure, int level, float x, float y);
static int  Roam__Act(const SceneRoam *roam);
static bool Roam__RunsMonsters(const SceneRoam *roam);

static void Roam__ApplyCharacter(SceneRoam *roam);
static void Roam__ShowMessage(SceneRoam *roam, const char *text);
static void Roam__QuestKill(SceneRoam *roam, const Unit *victim);
static void Roam__ItemCasts(SceneRoam *roam, int type, const Unit *target, uint64_t now);
static bool Roam__QuestObject(SceneRoam *roam, Unit *unit);

// Durability: the weapon wears with blows that land (1 in 25), what's worn with blows taken (1 in 10, a random piece)
static void Roam__Wear(SceneRoam *roam, const bool weapon) {
    if (!roam->game || Roam__Roll(1, weapon ? 25 : 10) != 1) {
        return;
    }
    static const EquipSlot armor[]   = {EQUIP_HEAD, EQUIP_TORSO, EQUIP_LEFT_ARM, EQUIP_GLOVES, EQUIP_FEET, EQUIP_BELT};
    Item                  *item      = NULL;
    Character             *character = roam->character;
    if (weapon) {
        for (int s = EQUIP_RIGHT_ARM; s <= EQUIP_LEFT_ARM && item == NULL; s++) {
            const ItemBase *base = Items_Find(character->equipped[s].code);
            item                 = base != NULL && base->kind == ITEM_KIND_WEAPON ? &character->equipped[s] : NULL;
        }
    } else {
        const EquipSlot slot = armor[Roam__Roll(0, (int)(sizeof(armor) / sizeof(armor[0])) - 1)];
        const ItemBase *base = Items_Find(character->equipped[slot].code);
        item                 = base != NULL && base->kind == ITEM_KIND_ARMOR ? &character->equipped[slot] : NULL;
    }
    if (item == NULL || item->max_durability <= 0 || item->durability <= 0) {
        return;
    }
    item->durability--;
    char name1[96];
    char name2[96];
    Affixes_Name(item, name1, sizeof(name1), name2, sizeof(name2));
    char text[160];
    if (item->durability == 0) {
        snprintf(text, sizeof(text), "Your %s has broken: a blacksmith in town can repair it.", name1);
        Sounds_Play("cursor_durability_break", SOUND_KIND_UI);
        Roam__ShowMessage(roam, text);
        Roam__ApplyCharacter(roam); // it does nothing now
    } else if (item->durability * 10 == item->max_durability || (item->max_durability < 10 && item->durability == 1)) {
        snprintf(text, sizeof(text), "Your %s is about to break.", name1);
        Roam__ShowMessage(roam, text);
    }
}

static void Roam__Damage(SceneRoam *roam, Unit *unit, int amount, const uint64_t now) {
    if (!Roam__IsAlive(unit)) {
        return;
    }
    if (roam->game && unit == &roam->player) {
        Roam__Wear(roam, false);
        Roam__ItemCasts(roam, MOD_SKILL_ON_STRUCK, NULL, now);                // ("chance to cast ... when struck")
        int percent = Affixes_Total(roam->character, MOD_DAMAGE_REDUCED_PCT); // "Damage Reduced by N%" (at most 50)
        percent     = percent > 50 ? 50 : percent;
        amount      = amount - (amount * percent / 100);
        const int reduced = Affixes_Total(roam->character, MOD_DAMAGE_REDUCED); // "Damage Reduced by N"
        amount            = amount - reduced > 1 ? amount - reduced : 1;
        const int to_mana = Affixes_Total(roam->character, MOD_DAMAGE_TO_MANA); // "Damage Taken Goes To Mana"
        if (to_mana > 0) {
            roam->mana = fminf((float)Character_MaxMana(roam->character),
                               roam->mana + ((float)amount * (float)to_mana / 100.0f));
        }
        // Energy Shield: its share of the blow comes off your mana instead (so much mana a point, in sixteenths)
        for (int b = 0; b < roam->buff_count; b++) {
            const SkillRow *shield = roam->buffs[b].skill;
            if (shield == NULL || shield->do_func != 23 || strcmp(shield->id, "Energy Shield") != 0 ||
                (roam->buffs[b].until != 0 && now >= roam->buffs[b].until)) {
                continue;
            }
            const int   level   = roam->buffs[b].level;
            const int   percent = SkillData_Eval(roam->character, shield, level, shield->calc[0]);
            const int   mult    = SkillData_Eval(roam->character, shield, level, shield->calc[1]);
            int         soaked  = amount * (percent < 95 ? percent : 95) / 100;
            const float cost    = (float)(mult > 1 ? mult : 1) / 16.0f;
            if ((float)soaked * cost > roam->mana) {
                soaked = (int)(roam->mana / cost); // (as much as the mana left covers)
            }
            roam->mana -= (float)soaked * cost;
            amount     -= soaked;
            break;
        }
        if (amount <= 0) {
            return;
        }
    }
    unit->hp -= amount;
    if (unit->hp <= 0) {
        unit->hp = 0;
        if (roam->game && unit != &roam->player && Roam__RunsMonsters(roam)) {
            Roam__DropTreasure(roam, unit->treasure, unit->level, unit->x, unit->y); // what it had
            Roam__QuestKill(roam, unit);
        }
        const bool dying = Roam__HasMode(roam, unit, "dt");
        unit->vanished   = !dying && !Roam__HasMode(roam, unit, "dd");
        Roam__SetState(unit, dying ? UNIT_DYING : UNIT_DEAD, now);
        Roam__UnitSound(roam, unit, MONSTER_SOUND_DEATH);
    } else {
        Roam__UnitSound(roam, unit, MONSTER_SOUND_HIT);
        // Hit recovery, the game's rule: a blow of at least a twelfth of your life (an eighth of a monster's) makes
        // you flinch; a monster's swing is cut short by it too (not a boss's or a unique's)
        const bool player = unit == &roam->player;
        const bool hard   = !roam->game || amount * (player ? 12 : 8) >= (unit->max_hp > 0 ? unit->max_hp : 1);
        const bool stops  = unit->state == UNIT_IDLE || unit->state == UNIT_WALK ||
                           (!player && unit->state == UNIT_ATTACK && !unit->boss && unit->rank == 0);
        if (hard && stops && Roam__HasMode(roam, unit, "gh")) {
            Roam__SetState(unit, UNIT_HIT, now);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Multiplayer: who runs the monsters, and who they go after

static int Roam__MapKey(const SceneRoam *roam);

// On each map the player with the lowest id runs the monsters (and tells the others where they are); alone, or not
// online, that's us
static bool Roam__RunsMonsters(const SceneRoam *roam) {
    if (!roam->online) {
        return true;
    }
    const int me = NetClient_MyId();
    for (int o = 0; o < roam->other_count; o++) {
        if (roam->other_ids[o] < me) {
            return false;
        }
    }
    return true;
}

static bool Roam__OtherAlive(const Unit *other) {
    return strcmp(other->net_mode, "dt") != 0 && strcmp(other->net_mode, "dd") != 0;
}

// How far the nearest player on this map (us or another) is from a spot
static float Roam__NearestPlayer(const SceneRoam *roam, const float x, const float y) {
    float nearest = Roam__Distance(x, y, roam->player.x, roam->player.y);
    for (int o = 0; o < roam->other_count; o++) {
        nearest = fminf(nearest, Roam__Distance(x, y, roam->others[o].x, roam->others[o].y));
    }
    return nearest;
}

// The player a monster goes after: the nearest one that can be fought (us, when we can be; the others alive). False
// when there's nobody.
static bool Roam__MonsterTarget(const SceneRoam *roam, const Unit *unit, const bool player_alive, float *x, float *y,
                                int *id) {
    float best = 1e9f;
    if (player_alive) {
        best = Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y);
        *x   = roam->player.x;
        *y   = roam->player.y;
        *id  = 0;
    }
    for (int o = 0; o < roam->other_count; o++) {
        const Unit *other = &roam->others[o];
        const float d     = Roam__Distance(unit->x, unit->y, other->x, other->y);
        if (Roam__OtherAlive(other) && d < best) {
            best = d;
            *x   = other->x;
            *y   = other->y;
            *id  = roam->other_ids[o];
        }
    }
    return best < 1e8f;
}

// Where another player is now (by id): false when they're not on this map
static bool Roam__OtherById(const SceneRoam *roam, const int id, const Unit **other) {
    for (int o = 0; o < roam->other_count; o++) {
        if (roam->other_ids[o] == id) {
            *other = &roam->others[o];
            return true;
        }
    }
    return false;
}

static bool Roam__HostileTo(const SceneRoam *roam, int id);

// PvP: the game allows players to fight, and we're out of town
static bool Roam__PvP(const SceneRoam *roam) {
    return roam->online && NetClient_Settings() != NULL && NetClient_Settings()->pvp && !roam->in_town;
}

// The other player nearest the mouse (within reach, alive), -1 if none
static int Roam__OtherAt(const SceneRoam *roam, const float sub_x, const float sub_y) {
    int   nearest  = -1;
    float distance = HOVER_RANGE;
    for (int o = 0; o < roam->other_count; o++) {
        const Unit      *other  = &roam->others[o];
        const float      d      = Roam__Distance(other->x - 0.8f, other->y - 0.8f, sub_x, sub_y);
        const NetPlayer *player = NetClient_Player(roam->other_ids[o]);
        const bool friend       = NetClient_MyParty() != 0 && player != NULL && player->party == NetClient_MyParty();
        if (Roam__OtherAlive(other) && !friend && d < distance) {
            distance = d;
            nearest  = o;
        }
    }
    return nearest;
}

// Normal (0), Nightmare (1) or Hell (2): the multiplayer game's setting, or the Single Player game's
static int Roam__Difficulty(const SceneRoam *roam) {
    const int difficulty =
        roam->online && NetClient_Settings() != NULL ? NetClient_Settings()->difficulty : roam->difficulty;
    return difficulty >= 0 && difficulty < 3 ? difficulty : 0;
}

// The difficulty's penalty to the player's resistances: the game's 0, 40 (Nightmare), 100 (Hell)
static int Roam__ResistPenalty(const SceneRoam *roam) {
    static const int penalties[3] = {0, 40, 100};
    const int        difficulty   = Roam__Difficulty(roam);
    return difficulty >= 0 && difficulty < 3 ? penalties[difficulty] : 0;
}

// Damage after a resistance: 100% or more is immune; below 0 it hurts more
static int Roam__Resisted(const int damage, const int resist) {
    if (resist >= 100 || damage <= 0) {
        return 0;
    }
    return damage * (100 - resist) / 100;
}

// Fire, cold or lightning damage reaching the player: after resistance, less "Magic Damage Reduced by N"
static int Roam__ElementTaken(const SceneRoam *roam, const Element element, const int damage) {
    if (!roam->game) {
        return damage;
    }
    int resist = Character_Resist(roam->character, element, Roam__ResistPenalty(roam));
    if (element != ELEMENT_PHYSICAL && Roam__Now(roam) < roam->conviction_until) {
        resist -= roam->conviction; // (a monster's Conviction)
    }
    if ((element == ELEMENT_POISON && Roam__Now(roam) < roam->antidote_until) ||
        (element == ELEMENT_COLD && Roam__Now(roam) < roam->thawing_until)) { // (antidote / thawing potions)
        resist = resist >= 75 ? resist : (resist + 50 < 75 ? resist + 50 : 75);
    }
    int taken = Roam__Resisted(damage, resist);
    if (element == ELEMENT_POISON) { // Poison Length Reduced: poison here is all at once, so less of it (at most 75%)
        const int shorter = Affixes_Total(roam->character, MOD_POISON_LENGTH);
        taken             = taken * (100 - (shorter < 75 ? shorter : 75)) / 100;
    }
    if (element == ELEMENT_FIRE || element == ELEMENT_COLD || element == ELEMENT_LIGHTNING) {
        taken -= taken > 0 ? Affixes_Total(roam->character, MOD_MAGIC_DAMAGE_REDUCED) : 0;
    }
    return taken > 0 ? taken : 0;
}

// Chilled or frozen: slowed for so long, unless what's worn says otherwise (Cannot Be Frozen, Half Freeze Duration)
static void Roam__ChillMe(SceneRoam *roam, int ms, const uint64_t now) {
    if (now < roam->thawing_until || (roam->game && Affixes_Total(roam->character, MOD_CANNOT_FREEZE) > 0)) {
        return;
    }
    if (roam->game && Affixes_Total(roam->character, MOD_HALF_FREEZE) > 0) {
        ms /= 2;
    }
    const uint64_t until = now + (uint64_t)(ms > 0 ? ms : 0);
    if (until > roam->player.chill_until) {
        roam->player.chill_until = until;
    }
}

// A monster's defense against our blows: -% Target Defense takes some off (at most half, a quarter for bosses)
static int Roam__DefenseAgainstUs(const SceneRoam *roam, const Unit *target) {
    if (!roam->game) {
        return target->defense;
    }
    const int most = target->boss || target->rank != 0 ? 25 : 50;
    int       cut  = Affixes_Total(roam->character, MOD_REDUCE_DEFENSE);
    cut            = cut < most ? (cut > 0 ? cut : 0) : most;
    return target->defense * (100 - cut) / 100;
}

// The player's blow, each kind of damage rolled: physical, then fire, cold, lightning, poison (what's worn)
static void Roam__RollPlayerDamage(const SceneRoam *roam, int damage[ELEMENT_COUNT]) {
    damage[ELEMENT_PHYSICAL] = Roam__Roll(roam->player.damage_min, roam->player.damage_max);
    // Weakened (a monster's curse): less of it
    const int weakened       = 100 + Roam__PlayerCurse(roam, "damagepercent");
    damage[ELEMENT_PHYSICAL] = damage[ELEMENT_PHYSICAL] * (weakened > 0 ? weakened : 0) / 100;
    for (int e = ELEMENT_FIRE; e < ELEMENT_COUNT; e++) {
        int low  = 0;
        int high = 0;
        if (roam->game) {
            Character_ElementDamage(roam->character, (Element)e, &low, &high);
        }
        damage[e] = high > 0 ? Roam__Roll(low, high) : 0;
    }
}

// A blow at another player (a monster's, or ours in PvP): they roll whether it lands, with their own defense
static void Roam__SendHitPlayer(const int target, const bool pvp, const int attack_rating, const int level,
                                const int damage[ELEMENT_COUNT]) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)target);
    NetWriter_U8(writer, pvp ? 1 : 0);
    NetWriter_I32(writer, attack_rating);
    NetWriter_I32(writer, level);
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        NetWriter_I32(writer, damage[e]); // physical, then fire, cold, lightning, poison: they resist those
    }
    NetClient_Send(MSG_HIT_PLAYER, writer);
    free(writer);
}

// A monster's curse (skill), or its cold (NULL: chilled), on another player: the map's owner tells them
static void Roam__SendAfflict(const int target, const SkillRow *curse, const int level, const int ms) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    NetWriter_Begin(writer);
    NetWriter_U8(writer, (uint8_t)target);
    NetWriter_U16(writer, curse != NULL ? (uint16_t)curse->row : 0xFFFF);
    NetWriter_U8(writer, (uint8_t)level);
    NetWriter_U16(writer, (uint16_t)(ms > 65535 ? 65535 : ms));
    NetClient_Send(MSG_AFFLICT, writer);
    free(writer);
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects

static bool Roam__CanUse(const Unit *unit) { return unit->is_object && unit->object.selectable && !unit->used; }

// The object's footprint in sub-tiles (what it blocks), at least the sub-tile it stands on
static void Roam__Footprint(const Unit *unit, int *x0, int *y0, int *width, int *height) {
    *width  = unit->object.size_x > 0 ? unit->object.size_x : 1;
    *height = unit->object.size_y > 0 ? unit->object.size_y : 1;
    *x0     = (int)floorf(unit->x) - (*width / 2);
    *y0     = (int)floorf(unit->y) - (*height / 2);
}

// How far a point is from the edge of an object's footprint
static float Roam__DistanceToObject(const Unit *unit, const float x, const float y) {
    int x0;
    int y0;
    int width;
    int height;
    Roam__Footprint(unit, &x0, &y0, &width, &height);
    const float dx = fmaxf(fmaxf((float)x0 - x, 0.0f), x - (float)(x0 + width));
    const float dy = fmaxf(fmaxf((float)y0 - y, 0.0f), y - (float)(y0 + height));
    return sqrtf((dx * dx) + (dy * dy));
}

// Objects' footprints block walking while their animation says so (chests, closed doors, ...)
static void Roam__UpdateBlocking(SceneRoam *roam) {
    Map_ClearBlocked(roam->map);
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!unit->is_object) {
            continue;
        }
        const int mode = unit->state == UNIT_OPERATE ? 1 : Objects_ModeIndex(unit->object_mode);
        if (mode < 0 || !unit->object.collision[mode] || unit->object.size_x <= 0 || unit->object.size_y <= 0) {
            continue;
        }
        int x0;
        int y0;
        int width;
        int height;
        Roam__Footprint(unit, &x0, &y0, &width, &height);
        Map_SetBlocked(roam->map, x0, y0, width, height, true);
    }
}

static void Roam__ObjectSound(const Unit *unit) {
    const char *name = NULL;
    if (unit->object.is_door) {
        name = "object_door_wood_open";
    } else {
        switch (unit->object.operate_fn) {
        case 1:
            name = "object_casket";
            break;
        case 2:
            name = "object_shrine_holy";
            break;
        case 4:
            name = unit->object.size_x * unit->object.size_y >= 6 ? "object_chest_large" : "object_chest_small";
            break;
        case 23:
            name = "object_waypoint_open";
            break;
        default:
            break;
        }
    }
    if (name != NULL) {
        Sounds_Play(name, SOUND_KIND_SFX);
    }
}

// Done opening: rest in the "opened" animation (a chest's open lid, an active waypoint, an open door)
static void Roam__FinishOperate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    snprintf(unit->object_mode, sizeof(unit->object_mode), "%s", unit->object.has_mode[2] ? "on" : "nu");
    // Some doors have nothing in their "on" files but end their opening on the open door: rest on that frame
    Figure *op = MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op");
    if (unit->object.is_door && op != NULL && Figure_GetFramesPerDirection(op) > 1) {
        snprintf(unit->object_mode, sizeof(unit->object_mode), "op");
        unit->rest_frame = Figure_GetFramesPerDirection(op) - 1;
    }
    Roam__SetState(unit, UNIT_IDLE, now);
    Roam__UpdateBlocking(roam);
}

// Multiplayer: what was opened on this map before we came in shows open
static void Roam__ApplyUsedObjects(SceneRoam *roam) {
    if (!roam->online) {
        return;
    }
    const int key = Roam__MapKey(roam);
    for (int i = 0; i < roam->used_count; i++) {
        const int index = roam->used_objects[i];
        if (roam->used_maps[i] == key && index < roam->unit_count && roam->units[index].is_object) {
            roam->units[index].used = true;
            Roam__FinishOperate(roam, &roam->units[index], SDL_GetTicks64());
        }
    }
}

static void     Roam__ShowMessage(SceneRoam *roam, const char *text);
static void     Roam__RefreshList(SceneRoam *roam);
static void     Roam__UseShrine(SceneRoam *roam, Unit *shrine, uint64_t now);
static void     Roam__RechargeShrines(SceneRoam *roam, uint64_t now);
static Missile *Roam__SpawnEffect(SceneRoam *roam, const char *name, float x, float y, float dx, float dy);
static void     Roam__ArrivalSpot(const Map *map, float x, float y, float *out_x, float *out_y);

// A trapped chest goes off at whoever opens it: a bolt of lightning, stronger in later areas
static void Roam__SpringTrap(SceneRoam *roam, Unit *object, const uint64_t now) {
    object->trapped = false;
    if (!roam->game || (roam->online && !roam->use_is_ours)) {
        return;
    }
    const int level  = MonsterStats_AreaLevel(roam->area_level);
    const int damage = Roam__Roll(level, (level * 3) + 5);
    Roam__SpawnEffect(roam, "lightningstrike", roam->player.x, roam->player.y, 0.0f, 1.0f);
    Roam__Damage(roam, &roam->player, damage, now);
    Roam__ShowMessage(roam, "It was trapped!");
}

// A key for a locked chest: one off the stack (the Assassin needs none)
// use: take the key (false: only whether there is one)
static bool Roam__Unlock(SceneRoam *roam, const bool use) {
    if (!roam->game || roam->character == NULL || roam->character->class_id == CHARACTER_ASSASSIN) {
        return true;
    }
    Character *c = roam->character;
    for (int i = 0; i < c->inventory_count; i++) {
        Item *item = &c->inventory[i].item;
        if (strcmp(item->code, "key") == 0) {
            if (!use) {
                return true;
            }
            if (item->quantity > 1) {
                item->quantity--;
            } else {
                Inventory_Remove(c, i);
            }
            return true;
        }
    }
    return false;
}
static void Roam__TakePortal(SceneRoam *roam, const Unit *unit);
static void Roam__PlacePortals(SceneRoam *roam);
static void Roam__UseWell(SceneRoam *roam);

static void Roam__Operate(SceneRoam *roam, Unit *unit, const uint64_t now) {
    if (unit->object.operate_fn == 15) {
        Roam__TakePortal(roam, unit); // a Town Portal
        return;
    }
    if (unit->object.operate_fn == 27) {
        // A teleportation pad (the Arcane Sanctuary): to the nearest pad on another platform
        int   best     = -1;
        float distance = 1e9f;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *pad = &roam->units[i];
            const float d   = Roam__Distance(pad->x, pad->y, unit->x, unit->y);
            if (pad != unit && pad->is_object && pad->object.operate_fn == 27 && d > 20.0f && d < distance) {
                distance = d;
                best     = i;
            }
        }
        if (best >= 0) {
            Roam__SpawnEffect(roam, "teleport", roam->player.x, roam->player.y, 1.0f, 1.0f);
            Roam__ArrivalSpot(roam->map, roam->units[best].x + 2.0f, roam->units[best].y + 2.0f, &roam->player.x,
                              &roam->player.y);
            roam->player.target_x = roam->player.x;
            roam->player.target_y = roam->player.y;
            roam->moving          = false;
            Roam__ClearPath(&roam->player);
            Sounds_Play("object_teleportpad", SOUND_KIND_SFX);
        }
        return;
    }
    if (unit->object.operate_fn == 32) {
        // The stash: opens on the left, with the inventory on the right
        if (roam->game && roam->character != NULL) {
            roam->stash_object    = (int)(unit - roam->units);
            roam->inventory_panel = true;
            roam->skill_panel     = false;
            roam->char_panel      = false;
            roam->panel           = PANEL_NONE;
            GameInventory_SetStash(roam->inventory_ui, true);
            GameInventory_SetSharedStash(roam->inventory_ui, !roam->online); // (the shared page is this computer's)
            Sounds_Play("object_chest_large", SOUND_KIND_SFX);
        }
        return;
    }
    if (unit->object.operate_fn == 23) {
        // A waypoint: lights up the first time (a game keeps it among the ones found), and opens the list of places
        // every time
        const MapArea *area = Map_AreaAt(roam->map, unit->x, unit->y);
        const int      wp   = World_WaypointIndex(area != NULL ? area->level_id : roam->area_level);
        if (roam->game && roam->character != NULL && wp >= 0 && (roam->character->waypoints & (1ull << wp)) == 0) {
            roam->character->waypoints |= 1ull << wp;
            Roam__ShowMessage(roam, "Waypoint found.");
            if (roam->panel == PANEL_PLACES) {
                Roam__RefreshList(roam);
            }
        }
        if (strcmp(unit->object_mode, "on") != 0 && unit->state != UNIT_OPERATE) {
            Roam__ObjectSound(unit);
            if (unit->object.has_mode[1] && MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
                Roam__SetState(unit, UNIT_OPERATE, now);
            } else {
                Roam__FinishOperate(roam, unit, now);
            }
        }
        if (roam->panel != PANEL_PLACES) {
            Roam__TogglePanel(roam, PANEL_PLACES);
        }
        return;
    }
    unit->used = true;
    if (unit->locked) {
        // Its key: ours if we open it (someone else's game opened it with theirs)
        if (!roam->online || roam->use_is_ours) {
            Roam__Unlock(roam, true);
        }
        unit->locked = false;
        if (strncmp(unit->name, "Locked ", 7) == 0) {
            memmove(unit->name, unit->name + 7, strlen(unit->name + 7) + 1);
        }
    }
    Roam__ObjectSound(unit);
    if (Roam__QuestObject(roam, unit)) {
        Roam__FinishOperate(roam, unit, now);
        return; // a quest's chest, altar, orifice or orb
    }
    if (unit->object.operate_fn == 2) {
        Roam__UseShrine(roam, unit, now);
    } else if (unit->object.operate_fn == 22) {
        Roam__UseWell(roam);
    }
    // Chests, caskets, urns, barrels, crates, corpses and racks have something in them (multiplayer: the player the
    // server let open it drops it, once, for everyone)
    const int fn = unit->object.operate_fn;
    if (roam->game && (!roam->online || roam->use_is_ours) &&
        (fn == 1 || fn == 3 || fn == 4 || fn == 5 || fn == 14 || fn == 19 || fn == 20)) {
        char treasure[32];
        snprintf(treasure, sizeof(treasure), "Act %d Chest A", Roam__Act(roam) + 1);
        Roam__DropTreasure(roam, treasure, roam->character->level, unit->x, unit->y);
    }
    if (unit->trapped) {
        Roam__SpringTrap(roam, unit, now);
    }
    if (unit->object.has_mode[1] && unit->object.frame_count[1] > 0 &&
        MapUnits_GetFigure(roam->units_cache, unit->base, unit->id, "op") != NULL) {
        Roam__SetState(unit, UNIT_OPERATE, now);
        Roam__UpdateBlocking(roam);
    } else {
        Roam__FinishOperate(roam, unit, now);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Player

static int Roam__PlayerLife(const int player_class) {
    int        life  = 50;
    DataTable *table = DataTable_Load(EXCEL_CHARSTATS);
    if (table != NULL) {
        const int c_class = DataTable_GetColumnIndex(table, "class");
        const int c_vit   = DataTable_GetColumnIndex(table, "vit");
        const int c_add   = DataTable_GetColumnIndex(table, "hpadd");
        for (int row = 0; row < DataTable_GetRowCount(table); row++) {
            if (strcmp(DataTable_GetString(table, row, c_class), player_classes[player_class][2]) == 0) {
                life = DataTable_GetInt(table, row, c_vit) + DataTable_GetInt(table, row, c_add);
                break;
            }
        }
        DataTable_Destroy(&table);
    }
    return life > 0 ? life : 50;
}

// The character's numbers onto the player (after levelling up or spending points); life stays as it is
static void Roam__ApplyCharacter(SceneRoam *roam) {
    if (!roam->game) {
        return;
    }
    Character *character  = roam->character;
    Unit      *player     = &roam->player;
    player->max_hp        = Character_MaxLife(character);
    player->hp            = player->hp > player->max_hp ? player->max_hp : player->hp;
    player->level         = character->level;
    player->defense       = Character_Defense(character);
    player->attack_rating = Character_AttackRating(character);
    Character_Damage(character, &player->damage_min, &player->damage_max);
    const float max_mana    = (float)Character_MaxMana(character);
    const float max_stamina = (float)Character_MaxStamina(character);
    roam->mana              = roam->mana > max_mana ? max_mana : roam->mana;
    roam->stamina           = roam->stamina > max_stamina ? max_stamina : roam->stamina;
}

// What the character has now goes back into it (before saving)
static void Roam__StoreCharacter(SceneRoam *roam) {
    if (!roam->game) {
        return;
    }
    roam->character->life    = roam->player.hp;
    roam->character->mana    = (int)roam->mana;
    roam->character->stamina = (int)roam->stamina;
    if (roam->inventory_ui != NULL && GameInventory_Holding(roam->inventory_ui)) {
        // Saved as if the carried item were put back (it stays on the cursor: saving happens mid-game too, on a
        // level up or going through an exit)
        Character *copy = malloc(sizeof(Character));
        FAIL_IF_NULL(copy);
        *copy = *roam->character;
        GameInventory_PutBackCopy(roam->inventory_ui, copy);
        Character_Save(copy);
        roam->character->last_played = copy->last_played;
        free(copy);
        return;
    }
    Character_Save(roam->character);
}

static void Roam__LogAdd(SceneRoam *roam, const char *text, bool chat);

static void Roam__ShowMessage(SceneRoam *roam, const char *text) {
    if (strcmp(text, roam->message) != 0 || SDL_GetTicks64() >= roam->message_until) {
        Roam__LogAdd(roam, text, false); // (the same message again while it shows isn't logged twice)
    }
    snprintf(roam->message, sizeof(roam->message), "%s", text);
    Label_SetText(roam->message_label, text);
    roam->message_until = SDL_GetTicks64() + MESSAGE_MS;
}

static void Roam__ApplyCharacter(SceneRoam *roam);

// Swap weapons (W, or the inventory's I / II tabs): the expansion's other weapon set and its skills
static void Roam__SwapWeapons(SceneRoam *roam) {
    if (!roam->game || roam->character == NULL) {
        return;
    }
    if (GameInventory_Holding(roam->inventory_ui)) {
        Roam__ShowMessage(roam, "Put down what you're carrying first.");
        return;
    }
    if (!Character_SwapWeapons(roam->character)) {
        Roam__ShowMessage(roam, "Weapon sets are the expansion's: classic characters have one.");
        return;
    }
    Roam__ApplyCharacter(roam); // damage and defense from what's in your hands now
    Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    Roam__ShowMessage(roam, roam->character->second_set ? "Weapon set II" : "Weapon set I");
}

// The party screen (P, or the mini panel's Party): multiplayer only
static void Roam__ToggleParty(SceneRoam *roam) {
    if (roam->online) {
        roam->party_panel = !roam->party_panel;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    } else {
        Roam__ShowMessage(roam, "Parties are for multiplayer games.");
    }
}

static void Roam__ToggleHireling(SceneRoam *roam);

// ---------------------------------------------------------------------------------------------------------------------
// Items on the ground

// Which map this is, for the items lying on it: an act's outdoors joined into one map is the same map whichever area
// you're in (its lowest level Id, the town), otherwise the area's level Id
static int Roam__MapKey(const SceneRoam *roam) {
    if (roam->map == NULL) {
        return -1;
    }
    int key = roam->area_level;
    for (int a = 0; a < roam->map->area_count; a++) {
        key = a == 0 || roam->map->areas[a].level_id < key ? roam->map->areas[a].level_id : key;
    }
    return key;
}

static int Roam__Act(const SceneRoam *roam) {
    const Place *place = roam->place >= 0 && roam->place < Places_GetCount() ? Places_Get(roam->place) : NULL;
    return place != NULL ? place->act : 0;
}

// An item's picture on the ground (items/<flippyfile>.dc6, in the act's colours)
static Sprite *Roam__ItemSprite(SceneRoam *roam, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL || base->flippy_file[0] == '\0') {
        return NULL;
    }
    int        shift  = 0;
    int        color  = -1;
    const bool tinted = Affixes_InvTint(item, &shift, &color); // (a unique's colour, like in the inventory)
    char       key[32];
    snprintf(key, sizeof(key), tinted ? "%.20s#%d.%d" : "%.20s", base->flippy_file, shift, color);
    const int act = Roam__Act(roam);
    if (act != roam->item_sprite_act) {
        for (int i = 0; i < roam->item_sprite_count; i++) {
            Sprite_Destroy(&roam->item_sprites[i].sprite);
        }
        roam->item_sprite_count = 0;
        roam->item_sprite_act   = act;
    }
    for (int i = 0; i < roam->item_sprite_count; i++) {
        if (strcmp(roam->item_sprites[i].file, key) == 0) {
            return roam->item_sprites[i].sprite;
        }
    }
    if (roam->item_sprite_count >= ITEM_SPRITES) {
        return NULL;
    }
    char path[96];
    snprintf(path, sizeof(path), "/data/global/items/%s.dc6", base->flippy_file);
    ItemSprite *entry = &roam->item_sprites[roam->item_sprite_count++];
    snprintf(entry->file, sizeof(entry->file), "%s", key);
    uint8_t remap[256];
    entry->sprite = tinted && Sprite_ItemShift(shift, color, remap)
                        ? Sprite_CreateShifted(path, MapView_ActPalette(act), remap)
                        : Sprite_Create(path, MapView_ActPalette(act));
    return entry->sprite;
}

// Puts an item on the ground by the player: on the nearest free spot (not on another item, not in a wall)
// The nearest free spot to a place for something dropped: not on another item, not in a wall
static void Roam__FreeSpotNear(const SceneRoam *roam, const float near_x, const float near_y, float *spot_x,
                               float *spot_y) {
    const int key = Roam__MapKey(roam);
    const int cx  = (int)near_x;
    const int cy  = (int)near_y;
    *spot_x       = near_x;
    *spot_y       = near_y;
    for (int ring = 0; ring <= 8; ring++) {
        for (int dy = -ring; dy <= ring; dy++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if (abs(dx) != ring && abs(dy) != ring) {
                    continue; // only the ring's edge (the inside was looked at already)
                }
                const float x = (float)(cx + dx) + 0.5f;
                const float y = (float)(cy + dy) + 0.5f;
                if (!Map_IsWalkable(roam->map, cx + dx, cy + dy)) {
                    continue;
                }
                bool taken = false;
                for (int g = 0; g < roam->ground_count && !taken; g++) {
                    taken = roam->ground[g].map_key == key && fabsf(roam->ground[g].x - x) < 0.9f &&
                            fabsf(roam->ground[g].y - y) < 0.9f;
                }
                if (!taken) {
                    *spot_x = x;
                    *spot_y = y;
                    return;
                }
            }
        }
    }
}

static void Roam__FreeSpot(const SceneRoam *roam, float *spot_x, float *spot_y) {
    Roam__FreeSpotNear(roam, roam->player.x, roam->player.y, spot_x, spot_y);
}

// An item lying on the ground: fresh ones flip out (with a sound when it's on our map)
static void Roam__AddGroundItem(SceneRoam *roam, const Item *item, const int map_key, const float x, const float y,
                                const bool fresh, const uint32_t net_id) {
    if (roam->ground_count >= MAX_GROUND_ITEMS) {
        return;
    }
    const uint64_t now    = SDL_GetTicks64();
    GroundItem    *ground = &roam->ground[roam->ground_count++];
    memset(ground, 0, sizeof(GroundItem));
    ground->item       = *item;
    ground->map_key    = map_key;
    ground->x          = x;
    ground->y          = y;
    ground->net_id     = net_id;
    ground->dropped_at = fresh ? now : (now > 60000 ? now - 60000 : 0);
    ground->landed     = !fresh;
    if (fresh && map_key == Roam__MapKey(roam)) {
        Sounds_Play("item_flippy", SOUND_KIND_SFX);
    }
}

// An item onto the ground at a spot (multiplayer: the server puts it there, for everyone)
static void Roam__PutOnGroundAt(SceneRoam *roam, const Item *item, const int map_key, const float x, const float y) {
    if (!roam->online) {
        Roam__AddGroundItem(roam, item, map_key, x, y, true, 0);
        return;
    }
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_I32(writer, map_key);
    NetWriter_F32(writer, x);
    NetWriter_F32(writer, y);
    NetItem_Write(writer, item);
    NetClient_Send(MSG_ITEM_DROP, writer);
    free(writer);
}

static void Roam__PutOnGround(SceneRoam *roam, const Item *item, const float x, const float y) {
    Roam__PutOnGroundAt(roam, item, Roam__MapKey(roam), x, y);
}

// Blocking with a shield (Character_BlockChance), with the game's sounds
static bool Roam__Blocks(SceneRoam *roam, const bool missile) {
    if (!roam->game) {
        return false;
    }
    if (Roam__Roll(1, 100) > Character_BlockChance(roam->character)) {
        return false;
    }
    char sound[24];
    snprintf(sound, sizeof(sound), "%s_%d", missile ? "block_arrow" : "block_weapon", Roam__Roll(1, 3));
    Sounds_Play(sound, SOUND_KIND_SFX);
    return true;
}

// What a monster or chest had: rolled from its treasure class, scattered around where it was
static void Roam__DropTreasure(SceneRoam *roam, const char *treasure, const int level, const float x, const float y) {
    Item items[TREASURE_MAX_DROPS];
    Affixes_SetMagicFind(roam->game ? Affixes_Total(roam->character, MOD_MAGIC_FIND) : 0); // (yours)
    Affixes_SetClassic(roam->character != NULL && !roam->character->expansion);
    const int count = Treasure_Roll(treasure, level, items);
    Affixes_SetMagicFind(0);
    for (int i = 0; i < count; i++) {
        float spot_x;
        float spot_y;
        Roam__FreeSpotNear(roam, x, y, &spot_x, &spot_y);
        if (!roam->online) {
            Roam__AddGroundItem(roam, &items[i], Roam__MapKey(roam), spot_x, spot_y, true, 0);
        } else {
            // The server adds it in a moment: meanwhile a stand-in keeps the next one off this spot
            Roam__PutOnGround(roam, &items[i], spot_x, spot_y);
            Roam__AddGroundItem(roam, &items[i], Roam__MapKey(roam), spot_x, spot_y, false, PLACEHOLDER_ID);
        }
    }
    for (int g = roam->ground_count - 1; g >= 0; g--) { // the stand-ins go again
        if (roam->ground[g].net_id == PLACEHOLDER_ID) {
            roam->ground[g] = roam->ground[--roam->ground_count];
        }
    }
}

// The item on the cursor onto the ground (multiplayer: the server puts it there, for everyone)
static void Roam__DropHeld(SceneRoam *roam) {
    if (roam->ground_count >= MAX_GROUND_ITEMS) {
        Roam__ShowMessage(roam, "There are too many items on the ground already.");
        return;
    }
    Item item;
    bool equipment_changed = false;
    if (!GameInventory_Drop(roam->inventory_ui, roam->character, &item, &equipment_changed)) {
        return;
    }
    float x;
    float y;
    Roam__FreeSpot(roam, &x, &y);
    Roam__PutOnGround(roam, &item, x, y);
    if (equipment_changed) {
        Roam__ApplyCharacter(roam); // Modern pick-up: it was still worn until now
    }
    if (roam->online && NetClient_ServerKeepsCharacters()) {
        Roam__StoreCharacter(roam); // the server's copy of the character without it, at once
    }
}

static void Roam__RemoveGroundItem(SceneRoam *roam, const int index) {
    roam->ground[index] = roam->ground[--roam->ground_count];
    roam->item_target   = -1;
    roam->hover_item    = -1;
}

// An item comes to us: onto the cursor with the inventory open, otherwise into the belt (potions) or the inventory.
// False when there's no room.
// Gold you can carry: the game's 10,000 a level
static int Roam__GoldLimit(const SceneRoam *roam) { return roam->character->level * 10000; }

static bool Roam__GiveItem(SceneRoam *roam, const Item *item) {
    if (strcmp(item->code, "gld") == 0) {
        const int limit       = Roam__GoldLimit(roam);
        const int amount      = item->quantity + (item->quantity * Affixes_Total(roam->character, MOD_GOLD_FIND) / 100);
        roam->character->gold = roam->character->gold + amount > limit ? limit : roam->character->gold + amount;
        return true;
    }
    const ItemBase *base = Items_Find(item->code);
    if (roam->inventory_panel && !GameInventory_Holding(roam->inventory_ui)) {
        GameInventory_Hold(roam->inventory_ui, item);
        return true;
    }
    if (base != NULL && base->beltable && Inventory_AddToBelt(roam->character, item)) {
        return true;
    }
    return Inventory_Add(roam->character, item);
}

// Whether an item would fit (without taking it)
static bool Roam__HasRoomFor(SceneRoam *roam, const Item *item) {
    if (strcmp(item->code, "gld") == 0) {
        return roam->character->gold < Roam__GoldLimit(roam);
    }
    if (roam->inventory_panel && !GameInventory_Holding(roam->inventory_ui)) {
        return true;
    }
    Character *copy = malloc(sizeof(Character));
    FAIL_IF_NULL(copy);
    *copy                = *roam->character;
    const ItemBase *base = Items_Find(item->code);
    const bool room = (base != NULL && base->beltable && Inventory_AddToBelt(copy, item)) || Inventory_Add(copy, item);
    free(copy);
    return room;
}

// Picks a ground item up (multiplayer: asks the server for it; the first to ask gets it)
static void Roam__PickUp(SceneRoam *roam, const int index) {
    GroundItem *ground = &roam->ground[index];
    if (!Roam__HasRoomFor(roam, &ground->item)) {
        Roam__ShowMessage(roam, strcmp(ground->item.code, "gld") == 0 ? "You can't carry any more gold."
                                                                      : "There's no room for that in your inventory.");
        Sounds_Play("cursor_error", SOUND_KIND_UI);
        roam->item_target = -1;
        return;
    }
    if (roam->online) {
        if (!ground->asked) {
            ground->asked     = true;
            NetWriter *writer = malloc(sizeof(NetWriter));
            FAIL_IF_NULL(writer);
            NetWriter_Begin(writer);
            NetWriter_U32(writer, ground->net_id);
            NetClient_Send(MSG_ITEM_PICK, writer);
            free(writer);
        }
        roam->item_target = -1;
        return;
    }
    Roam__GiveItem(roam, &ground->item);
    Sounds_Play(strcmp(ground->item.code, "gld") == 0 ? "item_gold" : "item_pickup", SOUND_KIND_SFX);
    Roam__RemoveGroundItem(roam, index);
}

// Walking to a ground item: picks it up once close enough. True when that's done (or it's gone).
static bool Roam__TryPickUp(SceneRoam *roam) {
    if (roam->item_target < 0 || roam->item_target >= roam->ground_count) {
        roam->item_target = -1;
        return true;
    }
    const GroundItem *ground = &roam->ground[roam->item_target];
    if (Roam__Distance(roam->player.x, roam->player.y, ground->x, ground->y) > PICKUP_RANGE) {
        return false;
    }
    roam->moving = false;
    Roam__ClearPath(&roam->player);
    Roam__PickUp(roam, roam->item_target);
    roam->item_target = -1;
    return true;
}

// The flip and its landing sound
static void Roam__UpdateGroundItems(SceneRoam *roam, const uint64_t now) {
    const int key = Roam__MapKey(roam);
    for (int g = 0; g < roam->ground_count; g++) {
        GroundItem *ground = &roam->ground[g];
        if (ground->map_key != key || ground->landed) {
            continue;
        }
        const ItemBase *base  = Items_Find(ground->item.code);
        const int       frame = (int)((now - ground->dropped_at) / ITEM_FLIP_MS);
        if (base == NULL || frame >= base->drop_sound_frame) {
            ground->landed = true;
            if (base != NULL && base->drop_sound[0] != '\0') {
                Sounds_PlayAt(base->drop_sound, SOUND_KIND_SFX, ground->x - roam->player.x, ground->y - roam->player.y);
            }
        }
    }
}

// Which ground item is under the mouse (from where they were drawn last frame): its name when shown, or its picture
static int Roam__GroundItemAt(const SceneRoam *roam, const int mouse_x, const int mouse_y) {
    const SDL_Point mouse = {mouse_x, mouse_y};
    const int       key   = Roam__MapKey(roam);
    for (int g = 0; g < roam->ground_count; g++) {
        const GroundItem *ground = &roam->ground[g];
        if (ground->map_key == key && ground->label_shown && SDL_PointInRect(&mouse, &ground->label)) {
            return g;
        }
    }
    int   best      = -1;
    float best_dist = 1e9f;
    for (int g = 0; g < roam->ground_count; g++) {
        const GroundItem *ground = &roam->ground[g];
        if (ground->map_key != key || !ground->on_screen) {
            continue;
        }
        SDL_Rect box  = ground->rect; // a little bigger: small items are hard to hit
        box.x        -= 4;
        box.y        -= 4;
        box.w        += 8;
        box.h        += 8;
        if (SDL_PointInRect(&mouse, &box)) {
            const float dx   = (float)(mouse_x - ground->screen_x);
            const float dy   = (float)(mouse_y - ground->screen_y);
            const float dist = (dx * dx) + (dy * dy);
            if (dist < best_dist) {
                best      = g;
                best_dist = dist;
            }
        }
    }
    return best;
}

static void Roam__ResetPlayer(SceneRoam *roam) {
    Unit *player = &roam->player;
    memset(player, 0, sizeof(Unit));
    snprintf(player->base, sizeof(player->base), "chars");
    snprintf(player->id, sizeof(player->id), "%s", player_classes[roam->player_class][1]);
    snprintf(player->name, sizeof(player->name), "%s", player_classes[roam->player_class][0]);
    player->idle_mode   = "tn";
    player->x           = roam->spawn_x;
    player->y           = roam->spawn_y;
    player->max_hp      = Roam__PlayerLife(roam->player_class);
    player->hp          = player->max_hp;
    player->damage_min  = PLAYER_DAMAGE_MIN;
    player->damage_max  = PLAYER_DAMAGE_MAX;
    player->state       = UNIT_IDLE;
    player->frame_time  = SDL_GetTicks64();
    player->level       = 1;
    roam->moving        = false;
    roam->attack_target = -1;
    roam->use_target    = -1;
    if (roam->game) {
        player->hp = roam->character->life > 0 ? roam->character->life : Character_MaxLife(roam->character);
        snprintf(player->name, sizeof(player->name), "%s", roam->character->name);
        Roam__ApplyCharacter(roam);
    }
}

static bool Roam__CanTalk(const SceneRoam *roam, const Unit *unit);
static bool Roam__UpdateHirelingPortrait(SceneRoam *roam, int mouse_x, int mouse_y, bool click);
static void Roam__KeepHirelingScreen(SceneRoam *roam);
static bool Roam__NpcHasTalk(const SceneRoam *roam, const Unit *unit);
static void Roam__TalkTo(SceneRoam *roam, int npc);

// The player opens the object being walked to once close enough (or talks to the town folk). Returns true when that's
// done (or there's nothing to open any more).
static bool Roam__TryUse(SceneRoam *roam, const uint64_t now) {
    Unit *player = &roam->player;
    Unit *object = &roam->units[roam->use_target];
    if (roam->game && Roam__CanTalk(roam, object)) {
        if (Roam__Distance(object->x, object->y, player->x, player->y) > 2.5f) {
            return false;
        }
        Roam__TurnTowards(&player->turn, player->x, player->y, object->x, object->y);
        roam->moving = false;
        Roam__ClearPath(player);
        Roam__TalkTo(roam, roam->use_target);
        roam->use_target = -1;
        return true;
    }
    if (!Roam__CanUse(object)) {
        roam->use_target = -1;
        return true;
    }
    if (Roam__DistanceToObject(object, player->x, player->y) > USE_RANGE) {
        return false;
    }
    Roam__TurnTowards(&player->turn, player->x, player->y, object->x, object->y);
    roam->moving     = false;
    roam->use_target = -1;
    Roam__ClearPath(player);
    if (object->locked && !Roam__Unlock(roam, false)) {
        Roam__ShowMessage(roam, "It's locked: you need a key.");
        Sounds_Play("cursor_error", SOUND_KIND_UI);
        return true; // (the key's taken when it opens: in multiplayer, only by the one the server lets open it)
    }
    if (roam->online && object->is_object && object->object.operate_fn != 23 && object->object.operate_fn != 32 &&
        object->object.operate_fn != 15 && object->object.operate_fn != 27) { // (a portal, a pad: your own)
        // Multiplayer: the server says who opens it (the first to ask), and everyone's game shows it open
        object->used      = true;
        NetWriter *writer = malloc(sizeof(NetWriter));
        FAIL_IF_NULL(writer);
        NetWriter_Begin(writer);
        NetWriter_I32(writer, Roam__MapKey(roam));
        NetWriter_U16(writer, (uint16_t)(object - roam->units));
        NetClient_Send(MSG_OBJECT_USE, writer);
        free(writer);
        return true;
    }
    Roam__Operate(roam, object, now);
    return true;
}

// Start on the walkable sub-tile closest to the area's entrance (generated areas), or else the middle of the map
static void Roam__FindSpawn(SceneRoam *roam) {
    const int cx  = roam->map->has_start ? (int)roam->map->start_x : (roam->map->width * MAP_SUBTILES) / 2;
    const int cy  = roam->map->has_start ? (int)roam->map->start_y : (roam->map->height * MAP_SUBTILES) / 2;
    roam->spawn_x = (float)cx + 0.5f;
    roam->spawn_y = (float)cy + 0.5f;
    for (int radius = 0; radius < MAP_MAX_SIZE * MAP_SUBTILES; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if ((abs(dx) == radius || abs(dy) == radius) && Map_IsWalkable(roam->map, cx + dx, cy + dy)) {
                    roam->spawn_x = (float)(cx + dx) + 0.5f;
                    roam->spawn_y = (float)(cy + dy) + 0.5f;
                    return;
                }
            }
        }
    }
}

// Units for the map's NPCs, monsters and objects; then the player at the spawn point
// Unique monsters' names, the game's way: a prefix, a suffix and sometimes a title ("Bile Spine the Hungry")
static void Roam__UniqueName(char *out, const size_t size) {
    static const char *files[3] = {"/data/global/excel/UniquePrefix.txt", "/data/global/excel/UniqueSuffix.txt",
                                   "/data/global/excel/UniqueAppellation.txt"};
    static DataTable  *tables[3];
    static bool        loaded = false;
    if (!loaded) {
        loaded = true;
        for (int t = 0; t < 3; t++) {
            tables[t] = DataTable_Load(files[t]);
        }
    }
    char parts[3][48] = {"", "", ""};
    for (int t = 0; t < 3; t++) {
        const int rows = tables[t] != NULL ? DataTable_GetRowCount(tables[t]) : 0;
        if (rows <= 0 || (t == 2 && rand() % 2 == 0)) {
            continue;
        }
        const char *key  = DataTable_GetString(tables[t], rand() % rows, DataTable_GetColumnIndex(tables[t], "Name"));
        const char *text = StringTable_Find(key);
        snprintf(parts[t], sizeof(parts[t]), "%s", text != NULL ? text : key);
    }
    snprintf(out, size, "%s%s%s%s%s", parts[0], parts[1][0] != '\0' ? " " : "", parts[1],
             parts[2][0] != '\0' ? " " : "", parts[2]);
}

// Champions, uniques (with a power or two) and their minions, super uniques: tougher, worth more, better drops
// A unique's power (MonUMod.txt): what it does, and its name added to what's shown under the unique's name
static void Roam__UniquePower(Unit *unit, const int power) {
    static const char *const names[12] = {"Extra Strong",   "Extra Fast",          "Stone Skin",     "Fire Enchanted",
                                          "Cold Enchanted", "Lightning Enchanted", "Aura Enchanted", "Magic Resistant",
                                          "Cursed",         "Mana Burn",           "Spectral Hit",   "Teleportation"};
    switch (power) {
    case 0:
        unit->damage_min += unit->damage_min / 2;
        unit->damage_max += unit->damage_max / 2;
        break;
    case 1:
        unit->speed *= 1.4f;
        break;
    case 2:
        unit->defense *= 3;
        break;
    case 3:
    case 4:
    case 5: { // fire, cold or lightning enchanted
        const int element     = ELEMENT_FIRE + (power - 3);
        unit->element         = element;
        unit->element_chance  = 100;
        unit->element_min     = unit->damage_min;
        unit->element_max     = unit->damage_max;
        unit->resist[element] = unit->resist[element] > 75 ? unit->resist[element] : 75;
        break;
    }
    case 6: { // aura enchanted (Conviction too, like the game's)
        static const char *const auras[5] = {"Might", "Holy Fire", "Holy Freeze", "Holy Shock", "Conviction"};
        unit->aura                        = SkillData_Get(auras[rand() % 5]);
        unit->aura_level                  = 1 + (unit->level / 4);
        break;
    }
    case 7: // magic resistant: +40% fire, cold and lightning resistance
        for (int e = ELEMENT_FIRE; e <= ELEMENT_LIGHTNING; e++) {
            unit->resist[e] =
                unit->resist[e] + 40 < 95 ? unit->resist[e] + 40 : (unit->resist[e] > 95 ? unit->resist[e] : 95);
        }
        break;
    case 8: // cursed: Amplify Damage when it comes for you
        if (unit->hex == NULL) {
            unit->hex       = SkillData_Get("Amplify Damage");
            unit->hex_level = 1 + (unit->level / 6);
        }
        break;
    case 9:
        unit->mana_burn = true;
        break;
    case 10:
        unit->spectral       = true;
        unit->element        = ELEMENT_FIRE;
        unit->element_chance = 100;
        unit->element_min    = 1 + (unit->damage_min / 2);
        unit->element_max    = 1 + (unit->damage_max / 2);
        break;
    default: // teleportation
        if (unit->move_skill == MOVE_SKILL_NONE) {
            unit->move_skill = MOVE_SKILL_TELEPORT;
        }
        break;
    }
    const size_t used = strlen(unit->power);
    snprintf(unit->power + used, sizeof(unit->power) - used, "%s%s", used == 0 ? "" : ", ", names[power % 12]);
}

// MonUMod.txt's id -> the engine's power (Roam__UniquePower), -1 for one it hasn't
static int Roam__PowerOfMod(const int mod) {
    switch (mod) {
    case 5:
        return 0; // strong
    case 6:
        return 1; // fast
    case 28:
        return 2; // stone skin
    case 9:
        return 3; // fire
    case 18:
        return 4; // cold
    case 17:
        return 5; // lightning
    case 30:
        return 6; // aura
    case 8:
        return 7; // magic resistant
    case 7:
        return 8; // cursed
    case 25:
        return 9; // mana burn
    case 27:
        return 10; // spectral hit
    case 26:
        return 11; // teleport
    default:
        return -1;
    }
}

static void Roam__MakeRank(SceneRoam *roam, Unit *unit, const char *mark) {
    MonsterStats stats;
    const bool   have = MonsterStats_GetAtLevel(unit->id, 0, &stats);
    if (mark[0] == '*') {
        char cls[40];
        char tc[48];
        if (MonsterStats_SuperUnique(mark + 1, cls, sizeof(cls), tc, sizeof(tc))) {
            const char *name = StringTable_Find(mark + 1);
            snprintf(unit->name, sizeof(unit->name), "%s", name != NULL ? name : mark + 1);
            if (tc[0] != '\0') {
                snprintf(unit->treasure, sizeof(unit->treasure), "%s", tc);
            }
        }
        unit->rank = RANK_SUPER_UNIQUE;
        // Its own powers (SuperUniques.txt): the ones the engine has
        int mods[3];
        for (int m = 0, count = MonsterStats_SuperUniqueMods(mark + 1, mods); m < count; m++) {
            const int power = Roam__PowerOfMod(mods[m]);
            if (power >= 0) {
                Roam__UniquePower(unit, power);
            }
        }
        unit->max_hp     *= 4;
        unit->damage_min += unit->damage_min / 2;
        unit->damage_max += unit->damage_max / 2;
        unit->experience *= 5;
        unit->level      += 3;
    } else if (strcmp(mark, "@champion") == 0) {
        unit->rank           = RANK_CHAMPION;
        unit->max_hp        *= 3;
        unit->damage_min    += unit->damage_min / 4;
        unit->damage_max    += unit->damage_max / 4;
        unit->attack_rating += unit->attack_rating / 4;
        unit->experience    *= 3;
        unit->level         += 2;
        unit->speed         *= 1.15f;
        if (have && stats.treasure_champion[0] != '\0') {
            snprintf(unit->treasure, sizeof(unit->treasure), "%s", stats.treasure_champion);
        }
        // Which kind: the whole pack shares it in the game; here each rolls (the same for everyone in multiplayer)
        const int kind = rand() % 100;
        if (kind >= 95 && Roam__Difficulty(roam) >= 2) {
            unit->champion  = CHAMPION_POSSESSED;
            unit->max_hp   *= 2;
        } else if (kind >= 80) {
            unit->champion                  = CHAMPION_GHOSTLY;
            unit->draw.alpha                = 130;
            unit->speed                    *= 0.75f;
            unit->resist[ELEMENT_PHYSICAL]  = unit->resist[ELEMENT_PHYSICAL] > 50 ? unit->resist[ELEMENT_PHYSICAL] : 50;
            if (unit->element == ELEMENT_PHYSICAL || unit->element_chance <= 0) {
                unit->element        = ELEMENT_COLD;
                unit->element_chance = 100;
                unit->element_min    = 1 + (unit->damage_min / 2);
                unit->element_max    = 1 + (unit->damage_max / 2);
            }
        } else if (kind >= 60) {
            unit->champion    = CHAMPION_BERSERKER;
            unit->damage_min += unit->damage_min / 2;
            unit->damage_max += unit->damage_max / 2;
            unit->defense    /= 2;
        } else if (kind >= 40) {
            unit->champion  = CHAMPION_FANATIC;
            unit->speed    *= 1.25f;
        }
    } else if (strcmp(mark, "@unique") == 0) {
        unit->rank = RANK_UNIQUE;
        Roam__UniqueName(unit->name, sizeof(unit->name));
        unit->max_hp        *= 4;
        unit->damage_min    += unit->damage_min / 2;
        unit->damage_max    += unit->damage_max / 2;
        unit->attack_rating += unit->attack_rating / 3;
        unit->experience    *= 5;
        unit->level         += 3;
        // Two of the game's powers (MonUMod.txt's), different ones
        int first = -1;
        for (int p = 0; p < 2; p++) {
            int power = rand() % 12;
            if (power == first) {
                power = (power + 1) % 12;
            }
            first = power;
            Roam__UniquePower(unit, power);
        }
        if (have && stats.treasure_unique[0] != '\0') {
            snprintf(unit->treasure, sizeof(unit->treasure), "%s", stats.treasure_unique);
        }
    } else if (strcmp(mark, "@minion") == 0) {
        unit->rank        = RANK_MINION;
        unit->max_hp     *= 2;
        unit->experience *= 2;
    }
    unit->speed = unit->speed > 9.0f ? 9.0f : unit->speed;
    unit->hp    = unit->max_hp;
}

static void Roam__PlaceQuestObjects(SceneRoam *roam);

// The acts' films (the game's cut-scenes, from d2video.mpq / d2xvideo.mpq), each once a character: 0-2 Act II-IV's
// start, 3 Diablo's end, 4 Baal's end. Not in multiplayer or text mode, nor when the film isn't there.
static void Roam__PlayFilm(SceneRoam *roam, const int film) {
    static const char *const films[5] = {
        "/data/local/video/act02start640x480.bik", "/data/local/video/act03start640x480.bik",
        "/data/local/video/act04start640x480.bik", "/data/local/video/act04end640x480.bik",
        "/data/local/video/d2x_out_640x480.bik",
    };
    if (!roam->game || roam->character == NULL || film < 0 || film >= 5 ||
        (roam->character->films & (1u << film)) != 0) {
        return;
    }
    roam->character->films |= (uint8_t)(1u << film);
    if (!roam->online && !roam->text_mode && FileManager_FileExists(films[film])) {
        VideoManager_PlayVideo(films[film]);
    }
}

// Chests: some are locked (Lockable; named "Locked Chest"), some trapped (TrapProb), the same in everyone's game (from
// the world seed, the map and the object)
static void Roam__LockAndTrap(SceneRoam *roam, Unit *unit, const int index) {
    const int fn = unit->object.operate_fn;
    if (!roam->game || !(fn == 1 || fn == 3 || fn == 4 || fn == 5 || fn == 14)) {
        return;
    }
    const MapArea *area = Map_AreaAt(roam->map, unit->x, unit->y);
    for (int t = 0; t < TOWN_COUNT; t++) {
        if (town_level_ids[t] == (area != NULL ? area->level_id : roam->area_level)) {
            return; // (none in town)
        }
    }
    uint32_t h    = (World_GetSeed() * 2654435761u) ^ ((uint32_t)Roam__MapKey(roam) * 40503u) ^ ((uint32_t)index * 97u);
    h            ^= h >> 13;
    h            *= 0x5bd1e995u;
    h            ^= h >> 15;
    unit->locked  = unit->object.lockable && (h % 100) < 15;
    unit->trapped = unit->object.trap_prob > 0 && ((h >> 8) % 100) < (uint32_t)unit->object.trap_prob;
    if (unit->locked) { // (a trap gives no sign, like the game's)
        const size_t length = strlen(unit->name);
        if (length + 7 < sizeof(unit->name)) { // "Locked " before its name
            memmove(unit->name + 7, unit->name, length + 1);
            memcpy(unit->name, "Locked ", 7);
        }
    }
}

static void Roam__Populate(SceneRoam *roam) {
    roam->stash_object = -1; // (the stash panel closes: Roam__UpdateUi sees there's no stash)
    roam->npc_menu     = -1;
    if (roam->speech_text[0] != '\0') {
        Sounds_StopSpeech();
    }
    roam->speech_npc        = -1;
    roam->speech_text[0]    = '\0';
    roam->quest_objects_key = 0; // (the quests' missing objects go in again)
    roam->ancients_called   = false;
    roam->shop_npc          = -1; // (the store too)
    if (roam->online) {
        // Multiplayer: everyone's game makes the same monsters (the same life, facing the same way) for the same map,
        // so their numbers match between games
        srand(NetClient_Seed() ^ ((uint32_t)Roam__MapKey(roam) * 2654435761u));
    }
    roam->unit_count = 0;
    roam->hovered    = -1;
    roam->hover_exit = -1;
    int unique       = -1; // the last unique made: its minions come right after it
    for (int e = 0; e < roam->map->entity_count; e++) {
        const MapEntity *entity = &roam->map->entities[e];
        // The expansion's monster levels: from the area (Levels.txt MonLvl for the difficulty)
        const MapArea *area = roam->game ? Map_AreaAt(roam->map, entity->x, entity->y) : NULL;
        init_area_level     = roam->game ? MonsterStats_AreaLevel(area != NULL ? area->level_id : roam->area_level) : 0;
        Unit *unit          = Roam__AddUnit(roam, entity->base, entity->id, entity->x, entity->y);
        init_area_level     = 0;
        if (unit != NULL && roam->game && strcmp(entity->base, "monsters") == 0) {
            Roam__MakeRank(roam, unit, entity->name);
            if (unit->rank == RANK_UNIQUE) {
                unique = (int)(unit - roam->units);
            } else if (unit->rank == RANK_MINION && unique >= 0) {
                unit->leader = unique + 1;
            }
        }
        if (unit != NULL) {
            unit->turn = entity->direction;
            if (unit->is_object && entity->object_row >= 0) {
                Objects_Get(entity->object_row, &unit->object); // the exact object the map placed
                Roam__NameObject(unit);
                snprintf(unit->object_mode, sizeof(unit->object_mode), "%s",
                         entity->mode[0] != '\0' ? entity->mode : unit->object.idle_mode);
                Roam__LockAndTrap(roam, unit, e);
            }
        }
    }
    Roam__UpdateBlocking(roam);
    Roam__FindSpawn(roam);
    Roam__ResetPlayer(roam);
    if (roam->online && roam->game) {
        // Multiplayer: the quests' bosses, objects, prisons and town folk go in now, the same for everyone whatever
        // each one's quests say, so the monsters' numbers keep matching between games
        Roam__PlaceQuestObjects(roam);
        roam->quest_objects_key = Roam__MapKey(roam) + 1;
    }
    if (roam->online) {
        srand((unsigned)SDL_GetTicks64()); // back to our own luck
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Default map

void TestingGrounds_MapPath(char *path, const size_t size) {
    snprintf(path, size, "%s/%s.abm", Map_GetSaveFolder(), TESTING_GROUNDS_MAP);
}

bool TestingGrounds_ResetMapFile(void) {
    char path[4200];
    TestingGrounds_MapPath(path, sizeof(path));
    Map *map = TestingGrounds_CreateDefaultMap();
    if (map == NULL) {
        return false;
    }
    const bool saved = Map_SaveFile(map, path);
    Map_Destroy(&map);
    return saved;
}

void TestingGrounds_EnsureMapFile(void) {
    char path[4200];
    TestingGrounds_MapPath(path, sizeof(path));
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return;
    }
    TestingGrounds_ResetMapFile();
}

Map *TestingGrounds_CreateDefaultMap(void) {
    const int        type_index = LevelTypes_Find(DEFAULT_LEVEL_TYPE);
    const LevelType *type       = LevelTypes_Get(type_index >= 0 ? type_index : 0);
    Map             *map        = NULL;
    if (type != NULL) {
        map = Map_CreateBlank(DEFAULT_MAP_SIZE, DEFAULT_MAP_SIZE, type->act, (const char *const *)type->files,
                              type->file_count);
    } else {
        map = Map_CreateBlank(DEFAULT_MAP_SIZE, DEFAULT_MAP_SIZE, 0, NULL, 0);
    }
    snprintf(map->name, sizeof(map->name), "Testing Grounds");

    // Grass everywhere, a dirt square in the middle
    const MapTileChoice *floors;
    const int            floor_count = Map_GetChoices(map, MAP_LAYER_FLOOR, &floors);
    const MapTileChoice *grass       = floor_count > 0 ? &floors[0] : NULL;
    const MapTileChoice *dirt        = floor_count > 1 ? &floors[1] : grass;
    for (int i = 0; i < floor_count; i++) {
        if (floors[i].style == 5 && floors[i].sequence == 0) {
            grass = &floors[i];
        }
        if (floors[i].style == 0 && floors[i].sequence == 1) {
            dirt = &floors[i];
        }
    }
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            const bool middle = tx >= 12 && tx < 20 && ty >= 12 && ty < 20;
            Map_SetCell(map, MAP_LAYER_FLOOR, 0, tx, ty, middle ? dirt : grass);
        }
    }

    // A few training targets around the middle
    static const struct {
        const char *id;
        const char *name;
        float       x;
        float       y;
    } dummies[] = {
        {"zombie1",   "Zombie",    70.5f, 70.5f},
        {"fallen1",   "Fallen",    90.5f, 70.5f},
        {"quillrat1", "Quill Rat", 70.5f, 90.5f},
        {"skeleton1", "Skeleton",  90.5f, 90.5f},
    };
    for (size_t d = 0; d < sizeof(dummies) / sizeof(dummies[0]); d++) {
        Map_AddEntity(map, "monsters", dummies[d].id, dummies[d].name, dummies[d].x, dummies[d].y);
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
// UI

static void Roam__UpdateButtons(SceneRoam *roam) {
    Button_SetText(roam->run_button, roam->sprinting ? "Sprint" : (roam->running ? "Run" : "Walk"));
    Button_SetText(roam->preview_button, roam->preview ? "Preview On" : "Preview Off");
    Button_SetToggled(roam->preview_button, roam->preview);
    if (roam->game) {
        Button_SetText(roam->places_button,
                       roam->panel == PANEL_PLACES ? "Close" : (Roam__DebugTools(roam) ? "Places" : "Waypoint"));
        Button_SetToggled(roam->character_button, roam->char_panel);
        Button_SetEnabled(roam->town_button, !roam->in_town);
        return;
    }
    Button_SetText(roam->class_button, player_classes[roam->player_class][0]);
    Button_SetText(roam->monsters_button, roam->panel == PANEL_MONSTERS ? "Hide List" : "Monsters");
    if (roam->places_button != NULL) {
        Button_SetText(roam->places_button, roam->panel == PANEL_PLACES ? "Hide List" : "Places");
    }
    Button_SetText(roam->combat_button, combat_enabled ? "Combat On" : "Combat Off");
    Button_SetToggled(roam->combat_button, combat_enabled);
}

static void Roam__FreeList(SceneRoam *roam) {
    for (int i = 0; i < roam->list_count; i++) {
        free(roam->list_names[i]);
    }
    free(roam->list_names);
    free(roam->list_places);
    roam->list_names  = NULL;
    roam->list_places = NULL;
    roam->list_count  = 0;
}

static void Roam__RefreshList(SceneRoam *roam) {
    Roam__FreeList(roam);
    int list_y = CATEGORIES_Y;
    if (roam->panel == PANEL_PLACES) {
        Label_SetText(roam->title_label, roam->game && !Roam__DebugTools(roam) ? "Waypoint" : "Places");
        // Free Roam (and the debug tools) can go anywhere; a game only to the waypoints found
        const bool waypoints   = roam->game && !Roam__DebugTools(roam) && roam->character != NULL;
        const int  place_count = Places_GetCount();
        roam->list_names       = calloc(place_count > 0 ? (size_t)place_count : 1, sizeof(char *));
        roam->list_places      = calloc(place_count > 0 ? (size_t)place_count : 1, sizeof(int));
        FAIL_IF_NULL(roam->list_names);
        FAIL_IF_NULL(roam->list_places);
        roam->list_count = 0;
        for (int i = 0; i < place_count; i++) {
            const int wp = World_WaypointIndex(Places_Get(i)->level_id);
            if (waypoints && (wp < 0 || (roam->character->waypoints & (1ull << wp)) == 0)) {
                continue;
            }
            char name[80];
            snprintf(name, sizeof(name), "Act %d - %s", Places_Get(i)->act + 1, Places_Get(i)->name);
            roam->list_names[roam->list_count] = strdup(name);
            FAIL_IF_NULL(roam->list_names[roam->list_count]);
            roam->list_places[roam->list_count++] = i;
        }
    } else {
        Label_SetText(roam->title_label, "Monsters");
        const CatalogCharacters *bestiary = MapSession_GetBestiary();
        roam->list_count                  = CatalogCharacters_GetCount(bestiary);
        roam->list_names                  = calloc(roam->list_count > 0 ? (size_t)roam->list_count : 1, sizeof(char *));
        FAIL_IF_NULL(roam->list_names);
        for (int i = 0; i < roam->list_count; i++) {
            roam->list_names[i] = strdup(CatalogCharacters_GetName(bestiary, i));
            FAIL_IF_NULL(roam->list_names[i]);
        }
        list_y += CatalogCharacters_GetCategoriesHeight(bestiary, PANEL_INNER_W) + 4; // under the category bar
    }
    ListBox_SetItems(roam->list, (const char *const *)roam->list_names, roam->list_count);
    ListBox_SetPosition(roam->list, PANEL_INNER_X, list_y);
    if (roam->panel == PANEL_PLACES) {
        int row = -1;
        for (int i = 0; i < roam->list_count; i++) {
            row = roam->list_places[i] == roam->place ? i : row;
        }
        ListBox_SetSelectedIndex(roam->list, row);
    }
    roam->selected = ListBox_GetSelectedIndex(roam->list);
}

static void Roam__TogglePanel(SceneRoam *roam, const RoamPanel panel) {
    roam->panel = roam->panel == panel ? PANEL_NONE : panel;
    if (roam->panel != PANEL_NONE) {
        Roam__RefreshList(roam);
    }
    Roam__UpdateButtons(roam);
}

// Free Roam: go to another place
static void Roam__EnteredArea(SceneRoam *roam, int level_id, bool caption);
static void Roam__UpdateAtmosphere(SceneRoam *roam);
static int  Roam__FindPlace(int level_id);
static void Roam__ToWaypoint(SceneRoam *roam);
static void Roam__ArrivalSpot(const Map *map, float x, float y, float *out_x, float *out_y);

// Rain (snow in Act V) in the areas Levels.txt says it falls in: showers that come and go, a few minutes each
static float Roam__WeatherLevel(const SceneRoam *roam, const uint64_t now) { // 0 dry .. 1 the heaviest
    if ((!roam->atmosphere.rain && !roam->atmosphere.snow) || roam->atmosphere.inside) {
        return 0.0f;
    }
    const float cycle = (float)((now / 1000) % 360) / 360.0f; // a six-minute cycle: two of rain, four dry
    return cycle < 0.33f ? sinf(cycle / 0.33f * 3.14159f) : 0.0f;
}

// Lightning in a heavy shower now and then (a flash every half a minute or so, at its heaviest): when the one in this
// stretch of time flashes (0: none in it)
static uint64_t Roam__LightningAt(const SceneRoam *roam, const uint64_t now) {
    if (roam->atmosphere.snow) {
        return 0;
    }
    const uint64_t slot  = now / 25000;
    uint32_t       h     = (uint32_t)slot * 2654435761u;
    h                   ^= h >> 13;
    h                   *= 0x5bd1e995u;
    h                   ^= h >> 15;
    const uint64_t at    = (slot * 25000) + (h % 20000);
    return h % 4 != 0 && Roam__WeatherLevel(roam, at) > 0.55f ? at : 0;
}

static void Roam__DrawWeather(const SceneRoam *roam, const SDL_Rect *view) {
    const uint64_t now   = SDL_GetTicks64();
    const float    level = Roam__WeatherLevel(roam, now);
    const int      drops = (int)(level * (roam->atmosphere.snow ? 220.0f : 320.0f));
    if (drops <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < drops; i++) {
        // Each drop is its own falling line, placed by a hash of its number so they don't all move together
        uint32_t h  = (uint32_t)i * 2654435761u;
        h          ^= h >> 15;
        const float speed = roam->atmosphere.snow ? 0.05f + (float)(h % 40) / 1000.0f : 0.9f + (float)(h % 50) / 100.0f;
        const float fall  = (float)(now % 100000) * speed;
        const float span_y = (float)view->h + 40.0f;
        const float y      = fmodf((float)((h >> 8) % 1000) / 1000.0f * span_y + fall, span_y) - 20.0f;
        float       x      = (float)((h >> 4) % (uint32_t)(view->w + 80)) - 40.0f;
        if (roam->atmosphere.snow) {
            x += sinf((float)now / 700.0f + (float)i) * 6.0f; // drifting
            SDL_SetRenderDrawColor(sdl_renderer, 235, 240, 255, 200);
            const SDL_Rect flake = {view->x + (int)x, view->y + (int)y, 2, 2};
            SDL_RenderFillRect(sdl_renderer, &flake);
        } else {
            x -= y * 0.25f; // slanting in the wind
            SDL_SetRenderDrawColor(sdl_renderer, 150, 160, 190, 110);
            SDL_RenderDrawLine(sdl_renderer, view->x + (int)x, view->y + (int)y, view->x + (int)(x - 3.0f),
                               view->y + (int)(y + 12.0f));
        }
    }
    if (roam->atmosphere.snow) {
        return;
    }
    // A lightning flash: a bright blink, then a weaker second one
    const uint64_t flash = Roam__LightningAt(roam, now);
    if (flash != 0 && now >= flash && now < flash + 320) {
        const uint64_t into = now - flash;
        const int      a    = into < 90 ? 110 : (into >= 200 ? 60 : 0);
        if (a > 0) {
            SDL_SetRenderDrawColor(sdl_renderer, 225, 230, 255, (uint8_t)a);
            SDL_RenderFillRect(sdl_renderer, view);
        }
    }
    // Splashes where drops land: each a little spreading ring for a fifth of a second, somewhere new each time
    const int splashes = drops / 3;
    for (int i = 0; i < splashes; i++) {
        const uint32_t slot  = (uint32_t)((now + ((uint64_t)i * 37)) / 200) + ((uint32_t)i * 7919u);
        uint32_t       h     = (slot * 2654435761u) ^ ((uint32_t)i * 40503u);
        h                   ^= h >> 13;
        h                   *= 0x5bd1e995u;
        h                   ^= h >> 15;
        const float age      = (float)((now + (uint64_t)i * 37) % 200) / 200.0f; // 0..1 through its life
        const int   sx       = view->x + (int)(h % (uint32_t)view->w);
        const int   sy       = view->y + (int)((h >> 12) % (uint32_t)view->h);
        const int   w        = 2 + (int)(age * 5.0f);
        SDL_SetRenderDrawColor(sdl_renderer, 170, 180, 205, (uint8_t)(120.0f * (1.0f - age)));
        SDL_RenderDrawLine(sdl_renderer, sx - w, sy, sx - (w / 2), sy - 1);
        SDL_RenderDrawLine(sdl_renderer, sx + (w / 2), sy - 1, sx + w, sy);
    }
}

// Next to the waypoint of the area you're in (after travelling by waypoint)
static void Roam__ToWaypoint(SceneRoam *roam) {
    for (int u = 0; u < roam->unit_count; u++) {
        const Unit    *unit = &roam->units[u];
        const MapArea *area =
            unit->is_object && unit->object.operate_fn == 23 ? Map_AreaAt(roam->map, unit->x, unit->y) : NULL;
        if (!unit->is_object || unit->object.operate_fn != 23 || (area != NULL && area->level_id != roam->area_level)) {
            continue;
        }
        Roam__ArrivalSpot(roam->map, unit->x + 3.0f, unit->y + 3.0f, &roam->spawn_x, &roam->spawn_y);
        roam->player.x = roam->player.target_x = roam->spawn_x;
        roam->player.y = roam->player.target_y = roam->spawn_y;
        roam->moving                           = false;
        Roam__ClearPath(&roam->player);
        return;
    }
}

static void Roam__Teleport(SceneRoam *roam, const int place_index) {
    const Place *place = Places_Get(place_index);
    if (place == NULL) {
        return;
    }
    roam->item_target  = -1;
    roam->hover_item   = -1;
    roam->attack_other = 0;
    // Already on the map (an act's outdoors are one map): just go there
    const MapArea *area = roam->map != NULL && !roam->borrowed && roam->map->seed == World_GetSeed()
                              ? Map_FindArea(roam->map, place->level_id)
                              : NULL;
    if (area != NULL) {
        Roam__ArrivalSpot(roam->map, area->start_x, area->start_y, &roam->spawn_x, &roam->spawn_y);
        roam->player.x        = roam->spawn_x;
        roam->player.y        = roam->spawn_y;
        roam->player.target_x = roam->spawn_x;
        roam->player.target_y = roam->spawn_y;
        roam->moving          = false;
        roam->attack_target   = -1;
        Roam__ClearPath(&roam->player);
        roam->exits_armed = false;
        Roam__EnteredArea(roam, place->level_id, roam->announce);
        Roam__PlacePortals(roam);
        return;
    }
    if (roam->game && roam->map != NULL && Roam__IsAlive(&roam->player)) {
        Roam__StoreCharacter(roam); // life as you leave
    }
    char text[96];
    snprintf(text, sizeof(text), place->generated ? "Building %s..." : "Loading %s...", place->name);
    LoadingScreen_Begin(text);
    Map *map = Places_LoadMap(place);
    if (map == NULL) {
        LoadingScreen_End();
        return;
    }
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
    }
    roam->map         = map;
    roam->borrowed    = false;
    roam->place       = place_index;
    roam->area_level  = place->level_id;
    roam->exits_armed = false;
    Roam__UpdateAtmosphere(roam);
    MapRenderer_ClearCache(roam->renderer);
    roam->in_town = false;
    for (int t = 0; t < TOWN_COUNT; t++) {
        roam->in_town = roam->in_town || town_level_ids[t] == place->level_id;
    }
    Roam__Populate(roam);
    Roam__ApplyUsedObjects(roam);
    Roam__PlacePortals(roam);
    memset(roam->missiles, 0, sizeof(roam->missiles)); // what was flying stays behind
    // The automap of an act's outdoors is kept under its lowest level Id (the town), whichever area you came in by
    int automap_key = place->level_id;
    for (int a = 0; a < roam->map->area_count; a++) {
        automap_key = roam->map->areas[a].level_id < automap_key ? roam->map->areas[a].level_id : automap_key;
    }
    Automap_SetArea(roam->automap, roam->map, automap_key, World_GetSeed());
    if (roam->free_roam && roam->announce) {
        // The game's caption as you come in through an exit: "Entering The Blood Moor" (not where you start)
        char caption[96];
        World_GetEnteringText(place->level_id, caption, sizeof(caption));
        if (caption[0] != '\0') {
            Label_SetText(roam->caption_label, caption);
            roam->caption_until = SDL_GetTicks64() + CAPTION_MS;
        }
    }
    if (roam->game) {
        // The film of a new act, the first time you come to its town (Act II, III, IV)
        if (roam->in_town && place->act > roam->character->act && place->act >= 1 && place->act <= 3) {
            Roam__PlayFilm(roam, place->act - 1);
        }
        roam->character->act = place->act;
        roam->exhausted      = false;
        roam->panel          = PANEL_NONE;
        if (roam->in_town) {
            // In town you're looked after: life and mana back to full
            roam->player.hp = roam->player.max_hp;
            roam->mana      = (float)Character_MaxMana(roam->character);
        }
        Roam__StoreCharacter(roam);
    }
    LoadingScreen_End();
}

static int Roam__FindPlace(const int level_id) {
    for (int i = 0; i < Places_GetCount(); i++) {
        if (Places_Get(i)->level_id == level_id) {
            return i;
        }
    }
    return -1;
}

// Single Player: back to the act's town (dying, the Town button)
static void Roam__GoToTown(SceneRoam *roam, const int act) {
    const int place = Roam__FindPlace(town_level_ids[act >= 0 && act < TOWN_COUNT ? act : 0]);
    if (place >= 0) {
        Roam__Teleport(roam, place);
    }
}

// Free Roam: a new world seed (0 = random); the area you're in is built again from it
static void Roam__NewWorld(SceneRoam *roam, const uint32_t seed) {
    fixed_seed = seed;
    World_SetSeed(seed);
    Roam__Teleport(roam, roam->place);
    char text[64];
    snprintf(text, sizeof(text), "World seed %u", World_GetSeed());
    Roam__ShowMessage(roam, text);
}

// A walkable sub-tile near (x, y) that isn't on any exit
static void Roam__ArrivalSpot(const Map *map, const float x, const float y, float *out_x, float *out_y) {
    const int cx = (int)x;
    const int cy = (int)y;
    *out_x       = x;
    *out_y       = y;
    for (int radius = 0; radius < 60; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if ((abs(dx) != radius && abs(dy) != radius) || !Map_IsWalkable(map, cx + dx, cy + dy) ||
                    Map_ExitAt(map, (float)(cx + dx) + 0.5f, (float)(cy + dy) + 0.5f) != NULL) {
                    continue;
                }
                *out_x = (float)(cx + dx) + 0.5f;
                *out_y = (float)(cy + dy) + 0.5f;
                return;
            }
        }
    }
}

// Outdoors after dusk the area has its night sounds (SoundEnviron.txt's Night Ambience / Night Event)
static bool Roam__NightSounds(const SceneRoam *roam) { return !roam->atmosphere.inside && Roam__Night() >= 0.5f; }

static const char *Roam__AmbienceNow(const SceneRoam *roam) {
    return roam->night_sounds && roam->atmosphere.night_ambience[0] != '\0' ? roam->atmosphere.night_ambience
                                                                            : roam->atmosphere.ambience;
}

// Walked into another area of a joined-up map (or arrived in one): the game's caption, and town or not
// The area's music, and whether it's dark or raining there
static void Roam__UpdateAtmosphere(SceneRoam *roam) {
    World_GetAtmosphere(roam->area_level, &roam->atmosphere);
    if (roam->atmosphere.music[0] != '\0') {
        AudioManager_PlayMusic(roam->atmosphere.music, true); // the same track carries on
    }
    roam->night_sounds = Roam__NightSounds(roam);
    // The wind, a cave's hum (the same one carries on); crickets and the like at night outdoors
    Sounds_PlayAmbience(Roam__AmbienceNow(roam));
    roam->ambient_event_at = 0;
}

// The area's odd sounds now and then (birds, drips, distant howls): SoundEnviron.txt's event, about every Event Delay
static void Roam__AmbientEvents(SceneRoam *roam, const uint64_t now) {
    if (roam->game && !roam->online) {
        Roam__RechargeShrines(roam, now);
    }
    // The rain's sound, as loud as the shower is heavy (snow falls quietly)
    const float rain = roam->atmosphere.snow ? 0.0f : Roam__WeatherLevel(roam, SDL_GetTicks64()); // (as drawn)
    Sounds_PlayWeather(rain > 0.02f ? "scene_rain" : NULL, rain);
    // The thunder after a flash (a moment later: it's some way off)
    const uint64_t ticks = SDL_GetTicks64();
    const uint64_t flash = Roam__LightningAt(roam, ticks);
    if (flash != 0 && flash != roam->thunder_heard && ticks >= flash + 900 && ticks < flash + 4000) {
        roam->thunder_heard = flash;
        char name[24];
        snprintf(name, sizeof(name), "event_thunder_%d", Roam__Roll(1, 3));
        Sounds_Play(name, SOUND_KIND_SFX);
    }
    // Night falls or the day breaks: the background changes over
    const bool night = Roam__NightSounds(roam);
    if (night != roam->night_sounds) {
        roam->night_sounds = night;
        Sounds_PlayAmbience(Roam__AmbienceNow(roam));
    }
    const char *event =
        night && roam->atmosphere.night_event[0] != '\0' ? roam->atmosphere.night_event : roam->atmosphere.event;
    if (event[0] == '\0' || roam->atmosphere.event_delay <= 0) {
        return;
    }
    const uint64_t delay = (uint64_t)roam->atmosphere.event_delay * 40;
    if (roam->ambient_event_at == 0) {
        roam->ambient_event_at = now + (delay / 2) + (uint64_t)Roam__Roll(0, (int)delay);
        return;
    }
    if (now >= roam->ambient_event_at) {
        // (a group: one of them at random), from somewhere around you
        const float angle = (float)Roam__Roll(0, 359) * 0.01745f;
        const float away  = (float)Roam__Roll(4, 14);
        Sounds_PlayAt(event, SOUND_KIND_SFX, cosf(angle) * away, sinf(angle) * away);
        roam->ambient_event_at = now + (delay / 2) + (uint64_t)Roam__Roll(0, (int)delay);
    }
}

static void Roam__EnteredArea(SceneRoam *roam, const int level_id, const bool caption_wanted) {
    if (level_id == roam->area_level) {
        return;
    }
    roam->area_level = level_id;
    const int place  = Roam__FindPlace(level_id);
    roam->place      = place >= 0 ? place : roam->place;
    char caption[96];
    World_GetEnteringText(level_id, caption, sizeof(caption));
    if (caption_wanted && caption[0] != '\0') {
        Label_SetText(roam->caption_label, caption);
        roam->caption_until = SDL_GetTicks64() + CAPTION_MS;
    }
    const bool was_in_town = roam->in_town;
    roam->in_town          = false;
    for (int t = 0; t < TOWN_COUNT; t++) {
        roam->in_town = roam->in_town || town_level_ids[t] == level_id;
    }
    if (roam->game && roam->in_town && !was_in_town) {
        // In town you're looked after: life and mana back to full
        roam->player.hp = roam->player.max_hp;
        roam->mana      = (float)Character_MaxMana(roam->character);
        Roam__StoreCharacter(roam);
    }
    Roam__UpdateAtmosphere(roam);
    Roam__UpdateButtons(roam);
    Roam__PlacePortals(roam); // (walking into town on an act's joined map: a Town Portal's end there)
}

// The name of where you are: the area you're standing in on a joined-up map
static const char *Roam__AreaName(const SceneRoam *roam) {
    const MapArea *area = Map_AreaAt(roam->map, roam->player.x, roam->player.y);
    return area != NULL ? area->name : roam->map->name;
}

// Through an exit into the next area, coming in at its way back
static void Roam__Travel(SceneRoam *roam, const int target_level) {
    const int from_level = roam->area_level;
    const int place      = Roam__FindPlace(target_level);
    if (place < 0) {
        Roam__ShowMessage(roam, "That way isn't open yet.");
        roam->exits_armed = false;
        return;
    }
    roam->announce = true;
    Roam__Teleport(roam, place);
    roam->announce      = false;
    const MapExit *back = Map_ExitTo(roam->map, from_level);
    if (back != NULL) {
        Roam__ArrivalSpot(roam->map, back->arrive_x, back->arrive_y, &roam->spawn_x, &roam->spawn_y);
        roam->player.x        = roam->spawn_x;
        roam->player.y        = roam->spawn_y;
        roam->player.target_x = roam->spawn_x;
        roam->player.target_y = roam->spawn_y;
    }
    roam->exits_armed = false;
}

// Single Player: whether a waypoint is near enough to use the Waypoint button
static bool Roam__NearWaypoint(const SceneRoam *roam) {
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->is_object && unit->object.operate_fn == 23 &&
            Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) <= WAYPOINT_RANGE) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

// The help line under the view, from what's bound now: "Mouse Left walk/attack  R run  C character ..."
static void Roam__UpdateHelp(SceneRoam *roam) {
    static const struct {
        ControlAction action;
        const char   *what;
        int           modes; // 1 Single Player, 2 Free Roam, 4 Testing Grounds, 8 Free Roam's tools in a game
    } entries[] = {
        {ACTION_MOVE_ATTACK, "walk/attack", 7 },
        {ACTION_RIGHT_SKILL, "drop",        6 },
        {ACTION_RUN_TOGGLE,  "run",         7 },
        {ACTION_CHARACTER,   "character",   1 },
        {ACTION_COMBAT,      "combat",      6 },
        {ACTION_WAYPOINT,    "waypoint",    1 },
        {ACTION_TOWN,        "town",        1 },
        {ACTION_PREVIEW,     "preview",     4 },
        {ACTION_LIGHT,       "light",       7 },
        {ACTION_COLLISION,   "collision",   6 },
        {ACTION_AUTOMAP,     "map",         3 },
        {ACTION_NEW_WORLD,   "new world",   2 },
        {ACTION_SPRINT,      "sprint",      10},
    };
    const int mode    = roam->game ? (Roam__DebugTools(roam) ? 9 : 1) : (roam->free_roam ? 2 : 4);
    const int profile = Roam__Profile(roam);
    char      text[400];
    text[0] = '\0';
    for (size_t e = 0; e < sizeof(entries) / sizeof(entries[0]); e++) {
        if (!(entries[e].modes & mode)) {
            continue;
        }
        const ControlBindings *list = Controls_GetBindings(profile, entries[e].action);
        if (list->count == 0) {
            continue;
        }
        char name[64];
        Controls_InputName(&list->inputs[0], name, sizeof(name));
        const char  *shown = strncmp(name, "Mouse ", 6) == 0 ? name + 6 : name; // "Left walk/attack"
        const size_t used  = strlen(text);
        snprintf(text + used, sizeof(text) - used, "%s%s %s", used > 0 ? "  " : "", shown, entries[e].what);
        if (entries[e].action == ACTION_RIGHT_SKILL && !(mode & 1)) {
            const size_t more = strlen(text);
            snprintf(text + more, sizeof(text) - more, "  Shift+%s remove", shown);
        }
    }
    const size_t used = strlen(text);
    snprintf(text + used, sizeof(text) - used, "%sEsc menu", used > 0 ? "  " : "");
    if (strcmp(text, roam->help_text) != 0) {
        snprintf(roam->help_text, sizeof(roam->help_text), "%s", text);
        Label_SetText(roam->help_label, text);
    }
}

static void *Roam__Create(const bool free_roam, const bool game) {
    Cursor_SetVisible(true);

    SceneRoam *roam = calloc(1, sizeof(SceneRoam));
    FAIL_IF_NULL(roam);
    MonsterStats_SetDifficulty(0); // (a multiplayer game sets its own below)
    roam->free_roam       = free_roam || game;
    roam->game            = game;
    roam->renderer        = MapRenderer_Create();
    roam->units_cache     = MapUnits_Create();
    roam->camera.zoom     = 1.0f;
    roam->last_tick       = SDL_GetTicks64();
    roam->draw_flags      = MAP_DRAW_DEFAULT;
    roam->attack_target   = -1;
    roam->use_target      = -1;
    roam->item_target     = -1;
    roam->hover_item      = -1;
    roam->hover_other     = -1;
    roam->item_sprite_act = -1;
    roam->step_frame      = -1;
    roam->hovered         = -1;
    srand((unsigned)SDL_GetTicks64());

    roam->title_label  = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    roam->status_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    roam->help_label   = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    roam->life_label   = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    roam->target_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(roam->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(roam->title_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_SetAlignment(roam->target_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_END);
    Label_SetColor(roam->help_label, 0xA0, 0xA0, 0xA0, 0xFF);
    // The help line comes from the bindings (Options › Controls): worked out as it's drawn

    const char *names[6] = {
        "Back", "Class", "Walk", free_roam ? "Places" : "Monsters", free_roam ? "Monsters" : "Clear", "Combat"};
    if (game) {
        names[0] = "Exit Game";
        names[1] = "Character";
        names[3] = "Waypoint";
        names[4] = "Town";
    }
    for (int i = 0; i < 6; i++) {
        roam->buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, names[i]);
        Button_SetPosition(roam->buttons[i], 8 + (i * BUTTON_STEP), BOTTOM_Y);
    }
    roam->back_button   = roam->buttons[0];
    roam->class_button  = roam->buttons[1];
    roam->run_button    = roam->buttons[2];
    roam->combat_button = roam->buttons[5];
    if (game) {
        roam->class_button     = NULL;
        roam->combat_button    = NULL;
        roam->character_button = roam->buttons[1];
        roam->places_button    = roam->buttons[3];
        roam->town_button      = roam->buttons[4];
        Button_SetVisible(roam->buttons[5], false);

        roam->character    = Character_Current();
        roam->player_class = (int)roam->character->class_id;
        roam->online       = NetClient_Stage() == NETC_IN_GAME;
        // Monsters as tough as the game's Difficulty (multiplayer: the game's; Single Player: picked on the character
        // list)
        roam->difficulty = roam->online ? 0 : Character_Difficulty();
        MonsterStats_SetDifficulty(Roam__Difficulty(roam));
        roam->spread_place = -1;
        init_players       = 1; // (/players: back to one each game, like the game's)
        Treasure_SetPlayers(1);
        if (roam->online || roam->game) { // (Single Player: for commands, /players)
            roam->chat_box   = TextBox_Create(FONTS_FONT16, 120, 500, 560, 22,
                                            roam->online ? "Say something (Enter sends, Esc stops)"
                                                           : "A command: /players 1-8 (Enter, Esc stops)");
            roam->chat_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
            Label_SetAlignment(roam->chat_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
        }
        roam->running = true;
        roam->mana    = (float)(roam->character->mana > 0 ? roam->character->mana : Character_MaxMana(roam->character));
        roam->stamina =
            (float)(roam->character->stamina > 0 ? roam->character->stamina : Character_MaxStamina(roam->character));
        roam->char_panel_sprite = Sprite_Create(CHAR_PANEL, PALETTE_UNITS);
        roam->ctrl_panel        = Sprite_Create("/data/global/ui/PANEL/800ctrlpnl7.dc6", PALETTE_SKY);
        roam->ctrl_globes       = Sprite_Create("/data/global/ui/PANEL/hlthmana.dc6", PALETTE_SKY);
        roam->ctrl_overlap      = Sprite_Create("/data/global/ui/PANEL/overlap.dc6", PALETTE_SKY);
        roam->panel_close       = Sprite_Create("/data/global/ui/PANEL/buysellbtn.dc6", PALETTE_SKY);
        roam->ctrl_run          = Sprite_Create("/data/global/ui/PANEL/runbutton.dc6", PALETTE_SKY);
        roam->ctrl_menu         = Sprite_Create("/data/global/ui/PANEL/menubutton.dc6", PALETTE_SKY);
        roam->ctrl_mini         = Sprite_Create("/data/global/ui/PANEL/minipanel.dc6", PALETTE_SKY);
        roam->ctrl_mini_buttons = Sprite_Create("/data/global/ui/PANEL/minipanelbtn.dc6", PALETTE_SKY);
        roam->ctrl_level        = Sprite_Create("/data/global/ui/PANEL/level.dc6", PALETTE_SKY);
        roam->ctrl_skill        = Sprite_Create("/data/global/ui/SPELLS/Skillicon.dc6", PALETTE_SKY);
        roam->ctrl_pressed      = -1;
        for (int b = 0; b < 6; b++) {
            Button_SetVisible(roam->buttons[b], false); // the control panel does their jobs (Esc menu: exit)
        }
        roam->panel_label       = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        roam->panel_value_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        roam->inventory_ui      = GameInventory_Create();
        roam->ground            = calloc(MAX_GROUND_ITEMS, sizeof(GroundItem));
        FAIL_IF_NULL(roam->ground);
        roam->item_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        roam->skills_ui  = GameSkills_Create(roam->character->class_id);
    } else if (free_roam) {
        roam->places_button   = roam->buttons[3];
        roam->monsters_button = roam->buttons[4];
    } else {
        roam->monsters_button = roam->buttons[3];
        roam->clear_button    = roam->buttons[4];
    }

    roam->menu_options_button = Button_Create(BUTTON_TYPE_WIDE, "Options");
    roam->menu_leave_button   = Button_Create(BUTTON_TYPE_WIDE, game ? "Save and Exit Game" : "Leave");
    roam->menu_return_button  = Button_Create(BUTTON_TYPE_WIDE, game ? "Return to Game" : "Back to Free Roam");
    roam->confirm             = ConfirmDialog_Create();
    roam->amount              = AmountDialog_Create();
    roam->stash_object        = -1;
    roam->npc_menu            = -1;
    roam->shop_npc            = -1;
    Button_SetPosition(roam->menu_options_button, 264, 210);
    Button_SetPosition(roam->menu_leave_button, 264, 260);
    Button_SetPosition(roam->menu_return_button, 264, 310);
    roam->automap       = Automap_Create();
    roam->caption_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetAlignment(roam->caption_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    roam->message_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(roam->message_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    roam->log_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetAlignment(roam->log_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    Label_SetColor(roam->message_label, 0xFF, 0xE8, 0x60, 0xFF);

    roam->preview        = true;
    roam->preview_button = Button_Create(BUTTON_TYPE_MEDIUM, "Preview On");
    Button_SetPosition(roam->preview_button, PANEL_INNER_X + 38, PREVIEW_BUTTON_Y);

    roam->list = ListBox_Create(FONTS_FONT8, PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W, LIST_ROWS, LIST_ROW_H);
    ListBox_SetBackground(roam->list, 0, 0, 0, 160);

    if (game || free_roam) {
        // A new world for every game (multiplayer: the server's, the same for everyone); Free Roam keeps a seed you
        // typed in
        World_SetSeed(game && NetClient_Stage() == NETC_IN_GAME ? NetClient_Seed() : (game ? 0 : fixed_seed));
    }
    if (free_roam && !game) {
        roam->seed_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        roam->seed_box   = TextBox_Create(FONTS_FONT8, PANEL_INNER_X, SEED_BOX_Y, PANEL_INNER_W, 20, "type a seed");
        roam->seed_use_button    = Button_Create(BUTTON_TYPE_CANCEL, "Use");
        roam->seed_random_button = Button_Create(BUTTON_TYPE_CANCEL, "Random");
        Button_SetPosition(roam->seed_use_button, PANEL_INNER_X, SEED_BUTTONS_Y);
        Button_SetPosition(roam->seed_random_button, PANEL_INNER_X + PANEL_INNER_W - 96, SEED_BUTTONS_Y);
    }
    if (game) {
        Roam__GoToTown(roam, roam->character->act); // where the character was last
    } else if (free_roam) {
        // Start in the Rogue Encampment
        int start = 0;
        for (int i = 0; i < Places_GetCount(); i++) {
            if (Places_Get(i)->level_id == START_LEVEL_ID) {
                start = i;
            }
        }
        Roam__Teleport(roam, start);
    }
    if (roam->map == NULL) {
        if (!free_roam && MapSession_IsPlaytest() && MapSession_PeekMap() != NULL) {
            roam->map      = (Map *)MapSession_PeekMap();
            roam->borrowed = true;
        } else {
            char path[4200];
            if (!free_roam) {
                TestingGrounds_EnsureMapFile();
            }
            TestingGrounds_MapPath(path, sizeof(path));
            roam->map = free_roam ? NULL : Map_LoadFile(path);
            if (roam->map == NULL) {
                roam->map = TestingGrounds_CreateDefaultMap();
            }
        }
        Roam__Populate(roam);
    }
    Roam__UpdateButtons(roam);
    return roam;
}

void *TestingGrounds_Create(void) { return Roam__Create(false, false); }

void *FreeRoam_Create(void) { return Roam__Create(true, false); }

void *Game_Create(void) { return Roam__Create(false, true); }

static void Roam__Free(void **scene_ref) {
    SceneRoam *roam = *(SceneRoam **)scene_ref;
    TextBox_Destroy(&roam->chat_box);
    Label_Destroy(&roam->chat_label);
    Label_Destroy(&roam->log_label);
    if (!roam->borrowed) {
        Map_Destroy(&roam->map);
    } else if (roam->map != NULL) {
        Map_ClearBlocked(roam->map); // the editor's map: leave it as it was
    }
    MapRenderer_Destroy(&roam->renderer);
    MapUnits_Destroy(&roam->units_cache);
    Roam__FreeList(roam);
    ListBox_Destroy(&roam->list);
    for (int i = 0; i < 6; i++) {
        Button_Destroy(&roam->buttons[i]);
    }
    Button_Destroy(&roam->preview_button);
    Label_Destroy(&roam->title_label);
    Label_Destroy(&roam->status_label);
    Label_Destroy(&roam->help_label);
    Label_Destroy(&roam->life_label);
    Label_Destroy(&roam->target_label);
    Sprite_Destroy(&roam->char_panel_sprite);
    GameInventory_Destroy(&roam->inventory_ui);
    GameSkills_Destroy(&roam->skills_ui);
    Sprite *ctrl[] = {roam->ctrl_panel, roam->ctrl_globes, roam->ctrl_overlap,      roam->ctrl_run,
                      roam->ctrl_menu,  roam->ctrl_mini,   roam->ctrl_mini_buttons, roam->ctrl_level,
                      roam->ctrl_skill, roam->panel_close};
    for (size_t c = 0; c < sizeof(ctrl) / sizeof(ctrl[0]); c++) {
        Sprite_Destroy(&ctrl[c]);
    }
    TextBox_Destroy(&roam->seed_box);
    Button_Destroy(&roam->seed_use_button);
    Button_Destroy(&roam->seed_random_button);
    Label_Destroy(&roam->seed_label);
    Label_Destroy(&roam->caption_label);
    Automap_Destroy(&roam->automap);
    Button_Destroy(&roam->menu_options_button);
    Button_Destroy(&roam->menu_leave_button);
    Button_Destroy(&roam->menu_return_button);
    OptionsPanel_Destroy(&roam->options_panel);
    ConfirmDialog_Destroy(&roam->confirm);
    AmountDialog_Destroy(&roam->amount);
    for (int s = 0; s < roam->shop_count; s++) {
        free(roam->shops[s]);
    }
    free(roam->gamble);
    Sounds_PlayAmbience(NULL); // (the area's background stops with the game)
    Sounds_PlayWeather(NULL, 0.0f);
    free(roam->trade);
    roam->shop_count = 0;
    Label_Destroy(&roam->panel_label);
    Label_Destroy(&roam->panel_value_label);
    Label_Destroy(&roam->message_label);
    Label_Destroy(&roam->item_label);
    for (int i = 0; i < roam->item_sprite_count; i++) {
        Sprite_Destroy(&roam->item_sprites[i].sprite);
    }
    free(roam->ground);
    free(roam);
    *scene_ref = NULL;
}

void TestingGrounds_Free(void **scene_ref) { Roam__Free(scene_ref); }

void FreeRoam_Free(void **scene_ref) { Roam__Free(scene_ref); }

void Game_Free(void **scene_ref) { Roam__Free(scene_ref); }

// ---------------------------------------------------------------------------------------------------------------------
// Update

// Single Player: experience for a kill, and what comes with a new level
static void Roam__GainExperienceAmount(SceneRoam *roam, const int base_amount, const int monster_level) {
    if (!roam->game || base_amount <= 0) {
        return;
    }
    // (a quest's reward, monster_level -1, is as it is)
    const int amount = monster_level < 0
                           ? base_amount
                           : (int)Character_KillExperience(roam->character, (uint32_t)base_amount, monster_level);
    // The Experience Shrine: half as much again
    const int boosted = roam->shrine_code == 15 && Roam__Now(roam) < roam->shrine_until
                            ? amount + (amount * roam->shrine_arg0 / 100)
                            : amount;
    const int levels  = Character_AddExperience(roam->character, (uint32_t)boosted);
    if (levels > 0) {
        roam->player.hp = Character_MaxLife(roam->character); // a new level fills you up
        roam->mana      = (float)Character_MaxMana(roam->character);
        roam->stamina   = (float)Character_MaxStamina(roam->character);
        Roam__ApplyCharacter(roam);
        Sounds_Play("cursor_level_up", SOUND_KIND_UI);
        char text[96];
        snprintf(text, sizeof(text), "Level Up! You are now level %d  (C: spend your stat points)",
                 roam->character->level);
        Roam__ShowMessage(roam, text);
        Roam__StoreCharacter(roam);
    }
}

static void Roam__GainExperience(SceneRoam *roam, const Unit *victim) {
    Roam__GainExperienceAmount(roam, victim->experience, victim->level);
    if (roam->game && Roam__IsAlive(&roam->player)) {
        // Life and mana after each kill
        const int life  = Affixes_Total(roam->character, MOD_LIFE_PER_KILL);
        const int mana  = Affixes_Total(roam->character, MOD_MANA_PER_KILL);
        roam->player.hp = roam->player.hp + life < roam->player.max_hp ? roam->player.hp + life : roam->player.max_hp;
        roam->mana      = fminf((float)Character_MaxMana(roam->character), roam->mana + (float)mana);
    }
}

// Multiplayer: a monster we run died; whoever killed it (and their party nearby) gets the experience, through the
// server
static void Roam__SendKilled(SceneRoam *roam, const int index, const int killer) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_I32(writer, Roam__MapKey(roam));
    NetWriter_U16(writer, (uint16_t)index);
    NetWriter_U8(writer, (uint8_t)killer);
    NetClient_Send(MSG_MONSTER_KILLED, writer);
    free(writer);
}

// What a blow that lands does besides the damage (what's worn): Open Wounds, Freezes / Slows Target, Knockback,
// Hit Causes Monster to Flee. Only where the monsters are ours to move. big: a boss or unique (less of it)
static void Roam__HitEffects(SceneRoam *roam, Unit *target, const bool big, const uint64_t now) {
    const Character *c = roam->character;
    if (!Roam__IsAlive(target)) {
        return;
    }
    const int wounds = Affixes_Total(c, MOD_OPEN_WOUNDS);
    if (wounds > 0 && Roam__Roll(1, 100) <= wounds) { // bleeds for 8 seconds, more as you go up
        target->bleed_left = (float)(10 + (c->level * 3));
        target->bleed_rate = target->bleed_left / 8.0f;
    }
    const int freeze = Affixes_Total(c, MOD_FREEZE);
    if (freeze > 0) { // the game's frames: +1 is a little over half a second (half for bosses)
        const uint64_t until = now + (uint64_t)((big ? 300 : 600) + (freeze * (big ? 100 : 200)));
        target->frozen_until = until > target->frozen_until ? until : target->frozen_until;
        target->chill_until  = target->frozen_until + 1000;
    }
    const int slow = Affixes_Total(c, MOD_SLOW);
    if (slow > target->slowed) {
        target->slowed = slow < (big ? 25 : 50) ? slow : (big ? 25 : 50);
    }
    if (big) {
        return; // bosses and uniques don't get pushed or run
    }
    const int flee = Affixes_Total(c, MOD_FLEE);
    if (flee > 0 && Roam__Roll(1, 100) <= flee) {
        target->flee_until = now + 2000;
    }
    if (Affixes_Total(c, MOD_KNOCKBACK) > 0) {
        const float dx = target->x - roam->player.x;
        const float dy = target->y - roam->player.y;
        const float d  = sqrtf((dx * dx) + (dy * dy));
        if (d > 0.01f) {
            float x = target->x;
            float y = target->y;
            Roam__ArrivalSpot(roam->map, target->x + (dx / d * 1.2f), target->y + (dy / d * 1.2f), &x, &y);
            if (Roam__Distance(x, y, target->x, target->y) < 2.0f) { // (not through a wall to somewhere else)
                target->x = x;
                target->y = y;
                Roam__ClearPath(target);
            }
        }
    }
}

static void     Roam__SkillHit(SceneRoam *roam, Unit *unit, const int damage[ELEMENT_COUNT], int cold_frames,
                               int poison_frames, bool ours, uint64_t now);
static Missile *Roam__SpawnEffect(SceneRoam *roam, const char *name, float x, float y, float dx, float dy);

// The Assassin's charge-up attacks: a blow with one that lands adds a charge (three at most, gone after 30 seconds);
// another charge-up skill's blow starts over with its own
static void Roam__AddCharge(SceneRoam *roam, const SkillRow *skill, const int level, const uint64_t now) {
    if (roam->charge_skill != skill || now >= roam->charges_until) {
        roam->charges = 0;
    }
    roam->charge_skill  = skill;
    roam->charge_level  = level;
    roam->charges       = roam->charges < 3 ? roam->charges + 1 : 3;
    roam->charges_until = now + 30000;
    Sounds_Play("assassin_chargeup_tiger_1", SOUND_KIND_SFX);
}

// A finishing move lets them go into its blow: Tiger Strike more damage, Cobra Strike life and mana stolen, Fists of
// Fire / Claws of Thunder / Blades of Ice their element (to the monsters around too from the second charge on),
// Phoenix Strike fire, then lightning, then cold
static void Roam__ReleaseCharges(SceneRoam *roam, Unit *target, int rolled[ELEMENT_COUNT], const uint64_t now) {
    const SkillRow *skill   = roam->charge_skill;
    const int       charges = roam->charges;
    if (skill == NULL || charges <= 0 || now >= roam->charges_until) {
        roam->charges = 0;
        return;
    }
    const int level  = roam->charge_level;
    int       low    = 0;
    int       high   = 0;
    int       frames = 0;
    SkillData_ElementalDamage(roam->character, skill, level, &low, &high, &frames);
    switch (skill->progressive_damage) {
    case 1: // more damage
        rolled[ELEMENT_PHYSICAL] +=
            rolled[ELEMENT_PHYSICAL] * SkillData_Eval(roam->character, skill, level, skill->calc[0]) * charges / 100;
        break;
    case 2: { // life and mana stolen from the blow
        const int pct    = skill->params[0] + (skill->params[1] * (level - 1));
        const int stolen = rolled[ELEMENT_PHYSICAL] * pct * charges / 100 / 3;
        roam->player.hp =
            roam->player.hp + stolen < roam->player.max_hp ? roam->player.hp + stolen : roam->player.max_hp;
        roam->mana = fminf((float)Character_MaxMana(roam->character), roam->mana + (float)stolen);
        break;
    }
    default: { // the element (Phoenix: fire, lightning, cold as the charges go up)
        const Element element = skill->element > ELEMENT_PHYSICAL ? (Element)skill->element : ELEMENT_FIRE;
        int           splash[ELEMENT_COUNT] = {0};
        for (int c = 0; c < charges; c++) {
            const Element e       = skill->progressive_damage == 3
                                        ? (Element)(c == 0 ? ELEMENT_FIRE : (c == 1 ? ELEMENT_LIGHTNING : ELEMENT_COLD))
                                        : element;
            const int     amount  = Roam__Roll(low > 0 ? low : 1 + level, high > low ? high : 4 + (level * 3));
            rolled[e]            += amount;
            splash[e]            += c > 0 ? amount / 2 : 0;
        }
        if (charges >= 2 && Roam__RunsMonsters(roam)) { // (the explosion, nova or shards hit those around)
            for (int i = 0; i < roam->unit_count; i++) {
                Unit *other = &roam->units[i];
                if (other != target && Roam__IsAlive(other) && Roam__IsCombatant(other) && !other->ally &&
                    !other->npc && Roam__Distance(other->x, other->y, target->x, target->y) < 3.0f) {
                    Roam__SkillHit(roam, other, splash, element == ELEMENT_COLD ? frames : 0, 0, true, now);
                }
            }
            Roam__SpawnEffect(roam,
                              element == ELEMENT_COLD
                                  ? "bladesoficeexplode"
                                  : (element == ELEMENT_LIGHTNING ? "lightninghit" : "fistsoffireexplode"),
                              target->x, target->y, 0.0f, 1.0f);
        }
        if (element == ELEMENT_COLD) {
            target->chill_until = now + (uint64_t)((frames > 0 ? frames : 50) * 40);
        }
        break;
    }
    }
    roam->charges = 0;
}

static void Roam__PlayerStrikes(SceneRoam *roam, Unit *target, const uint64_t now);

// Feral Rage's and Maul's charges: each blow that lands adds one (up to the skill's most), for a while
static int Roam__DruidCharge(SceneRoam *roam, const SkillRow *skill, const int level, const uint64_t now) {
    if (roam->charge_skill != skill || now >= roam->charges_until) {
        roam->charges = 0;
    }
    const int most      = SkillData_Eval(roam->character, skill, level, skill->calc[1]);
    roam->charge_skill  = skill;
    roam->charge_level  = level;
    roam->charges       = roam->charges < (most > 1 ? most : 1) ? roam->charges + 1 : (most > 1 ? most : 1);
    const int frames    = SkillData_Eval(roam->character, skill, level, skill->aura_length);
    roam->charges_until = now + (uint64_t)((frames > 0 ? frames : 500) * 40);
    return roam->charges;
}

// Blows with a skill that does more, as the game does them: Conversion turns the monster, Vengeance adds the
// elements; the Druid's shape-shifted blows: Hunger steals life and mana, Feral Rage builds charges that
// steal life (and quicken you), Maul's add damage and stun, Rabies' poison spreads to the monsters around, Fury
// strikes several at once
static void Roam__SkillBlow(SceneRoam *roam, const SkillRow *skill, const int level, Unit *target,
                            int rolled[ELEMENT_COUNT], int *life_steal, int *mana_steal, const uint64_t now) {
    if (skill->do_func == 79 && Roam__RunsMonsters(roam) && !target->boss && target->rank != RANK_UNIQUE &&
        target->rank != RANK_SUPER_UNIQUE && Roam__IsAlive(target) &&
        Roam__Roll(1, 100) <= SkillData_Eval(roam->character, skill, level, skill->calc[0])) {
        // Conversion: the monster fights for you a while
        const int frames   = SkillData_Eval(roam->character, skill, level, skill->aura_length);
        target->ally       = true;
        target->turned     = true;
        target->ally_until = now + ((uint64_t)(frames > 0 ? frames : 400) * 40);
    } else if (skill->start_func == 35) {
        // Vengeance: its percents of the blow's damage added as fire, cold and lightning
        static const Element elements[3] = {ELEMENT_FIRE, ELEMENT_COLD, ELEMENT_LIGHTNING};
        const int            base        = rolled[ELEMENT_PHYSICAL];
        for (int e = 0; e < 3; e++) {
            rolled[elements[e]] += base * SkillData_Eval(roam->character, skill, level, skill->calc[e]) / 100;
        }
    } else if (skill->do_func == 122) { // Hunger
        *life_steal += SkillData_Eval(roam->character, skill, level, skill->calc[1]);
        *mana_steal += SkillData_Eval(roam->character, skill, level, skill->calc[2]);
    } else if (skill->do_func == 120 && strcmp(skill->id, "Maul") == 0) {
        const int charges         = Roam__DruidCharge(roam, skill, level, now);
        rolled[ELEMENT_PHYSICAL] += rolled[ELEMENT_PHYSICAL] * skill->params[2] * charges / 100;
        const int stun            = SkillData_Eval(roam->character, skill, level, "dm56");
        if (Roam__RunsMonsters(roam) && stun > 0) {
            const uint64_t until = now + (uint64_t)(stun * 40);
            target->frozen_until = until > target->frozen_until ? until : target->frozen_until;
        }
    } else if (skill->do_func == 120) { // Feral Rage
        *life_steal += skill->params[1] * Roam__DruidCharge(roam, skill, level, now);
    } else if (skill->do_func == 121 && Roam__RunsMonsters(roam) && target->poison_left > 0.0f) { // Rabies
        for (int i = 0; i < roam->unit_count; i++) {
            Unit *other = &roam->units[i];
            if (other != target && Roam__IsAlive(other) && Roam__IsCombatant(other) && !other->ally && !other->npc &&
                Roam__Distance(other->x, other->y, target->x, target->y) < 4.0f) {
                other->poison_left = fmaxf(other->poison_left, target->poison_left);
                other->poison_rate = fmaxf(other->poison_rate, target->poison_rate);
                other->poison_ours = true;
            }
        }
    } else if (skill->do_func == 13 && strcmp(skill->id, "Fury") == 0) {
        static bool striking = false; // (the others are struck once each, not again from their blows)
        if (striking) {
            return;
        }
        striking = true;
        int more = SkillData_Eval(roam->character, skill, level, skill->calc[0]) - 1;
        for (int i = 0; i < roam->unit_count && more > 0; i++) {
            Unit *other = &roam->units[i];
            if (other != target && Roam__IsAlive(other) && Roam__IsCombatant(other) && !other->ally && !other->npc &&
                !other->is_object &&
                Roam__Distance(other->x, other->y, roam->player.x, roam->player.y) <= ATTACK_RANGE + 0.5f) {
                Roam__PlayerStrikes(roam, other, now);
                more--;
            }
        }
        striking = false;
    }
}

// A curse of that name on the monster, still on: its percent (Iron Maiden: of its blows back on it; Life Tap: of the
// blows on it back as life), else 0
static int Roam__CursePercent(const SceneRoam *roam, const Unit *unit, const char *name, const uint64_t now) {
    if (unit->curse == NULL || now >= unit->curse_until || strcmp(unit->curse->id, name) != 0) {
        return 0;
    }
    return SkillData_Eval(roam->character, unit->curse, unit->curse_level, unit->curse->calc[0]);
}

// Iron Maiden: the cursed monster's blow, that much of it back on it
static void Roam__IronMaiden(SceneRoam *roam, Unit *unit, const int damage, const uint64_t now) {
    const int percent = Roam__CursePercent(roam, unit, "Iron Maiden", now);
    if (percent > 0 && damage > 0 && Roam__IsAlive(unit)) {
        int back[ELEMENT_COUNT] = {0};
        back[ELEMENT_PHYSICAL]  = damage * percent / 100;
        Roam__SkillHit(roam, unit, back, 0, 0, true, now);
    }
}

// The player's blow lands: in the game it can miss (attack rating against defense, levels), and kills give
// experience
static void Roam__PlayerStrikes(SceneRoam *roam, Unit *target, const uint64_t now) {
    const Unit     *player = &roam->player;
    const SkillRow *skill  = roam->game ? roam->melee_skill : NULL; // Bash, Jab, Power Strike, ...
    const int       level  = roam->melee_level;
    int             ar     = player->attack_rating;
    if (skill != NULL) {
        ar += ar * SkillData_ToHit(skill, level) / 100;
    }
    const Character *c   = roam->character;
    const bool       big = target->boss || target->rank == RANK_UNIQUE || target->rank == RANK_SUPER_UNIQUE;
    const bool sure_hit  = roam->game && !big && Affixes_Total(c, MOD_IGNORE_DEFENSE) > 0; // Ignore Target's Defense
    if (roam->game) {
        ar += (target->undead ? Affixes_Total(c, MOD_ATTACK_UNDEAD) : 0) +
              (target->demon ? Affixes_Total(c, MOD_ATTACK_DEMON) : 0);
    }
    if (roam->game && !sure_hit &&
        Roam__Roll(1, 100) >
            Character_HitChance(ar, player->level, Roam__DefenseAgainstUs(roam, target), target->level)) {
        return; // missed
    }
    Roam__Wear(roam, true);
    Roam__ItemCasts(roam, MOD_SKILL_ON_HIT, target, now); // ("chance to cast ... on striking")
    // Each kind of damage against the monster's resistance to it (an immune monster takes none of that kind)
    int rolled[ELEMENT_COUNT];
    Roam__RollPlayerDamage(roam, rolled);
    if (skill != NULL) {
        // The skill's extra damage (Zeal, Fury and Fend keep it in their second calc), and what it adds
        const int percent = SkillData_Eval(roam->character, skill, level, skill->calc[skill->do_func == 13 ? 1 : 0]);
        if (!skill->progressive &&
            skill->start_func != 35) { // (a charging blow is a plain one: its bonus comes with the finisher)
            rolled[ELEMENT_PHYSICAL] += rolled[ELEMENT_PHYSICAL] * percent / 100;
        }
        int low    = 0;
        int high   = 0;
        int frames = 0;
        SkillData_ElementalDamage(roam->character, skill, level, &low, &high, &frames);
        if (skill->progressive) {
            high = 0; // (its element comes with the finisher)
        }
        if (skill->element > ELEMENT_PHYSICAL && high > 0) {
            rolled[skill->element] += Roam__Roll(low, high);
            if (skill->element == ELEMENT_COLD) {
                target->chill_until = now + (uint64_t)(frames * 40);
            } else if (skill->element == ELEMENT_POISON && frames > 0) {
                target->poison_left    = (float)Roam__Resisted(rolled[ELEMENT_POISON], target->resist[ELEMENT_POISON]);
                target->poison_rate    = target->poison_left * 25.0f / (float)frames;
                target->poison_ours    = true;
                rolled[ELEMENT_POISON] = 0; // over time instead
            }
        }
    }
    if (roam->game) {
        // Against undead and demons; Critical Strike and Deadly Strike double it (only once)
        const int against = (target->undead ? Affixes_Total(c, MOD_DAMAGE_UNDEAD) : 0) +
                            (target->demon ? Affixes_Total(c, MOD_DAMAGE_DEMON) : 0);
        rolled[ELEMENT_PHYSICAL] += rolled[ELEMENT_PHYSICAL] * against / 100;
        if (Roam__Roll(1, 100) <= c->bonus[BONUS_CRITICAL] + Affixes_Total(c, MOD_DEADLY_STRIKE)) {
            rolled[ELEMENT_PHYSICAL] *= 2;
        }
        // Crushing Blow: a quarter of what's left of its life (an eighth for bosses and uniques)
        if (Affixes_Total(c, MOD_CRUSHING_BLOW) > 0 && Roam__Roll(1, 100) <= Affixes_Total(c, MOD_CRUSHING_BLOW)) {
            rolled[ELEMENT_PHYSICAL] += target->hp / (big ? 8 : 4);
        }
        if (Roam__RunsMonsters(roam)) {
            Roam__HitEffects(roam, target, big, now);
        }
    }
    int life_steal = 0;
    int mana_steal = 0;
    if (roam->game && skill != NULL) {
        Roam__SkillBlow(roam, skill, level, target, rolled, &life_steal, &mana_steal, now);
    }
    if (roam->game && skill != NULL && skill->finishing) {
        Roam__ReleaseCharges(roam, target, rolled, now); // a finishing move: the charges go into this blow
    } else if (roam->game && skill != NULL && skill->progressive) {
        Roam__AddCharge(roam, skill, level, now);
    }
    if (rolled[ELEMENT_POISON] > 0 && Roam__RunsMonsters(roam)) {
        // Poison from what's worn: over two seconds, like the game's (in someone else's map: all at once)
        target->poison_left    += (float)Roam__Resisted(rolled[ELEMENT_POISON], target->resist[ELEMENT_POISON]);
        target->poison_rate     = target->poison_left * 25.0f / 50.0f;
        target->poison_ours     = true;
        rolled[ELEMENT_POISON]  = 0;
    }
    int damage = 0;
    for (int e = 0; e < ELEMENT_COUNT; e++) {
        damage += Roam__Resisted(rolled[e], target->resist[e]);
    }
    if (roam->game) {
        // Life and mana stolen with each blow that lands (from its physical damage, like the game)
        const int physical = Roam__Resisted(rolled[ELEMENT_PHYSICAL], target->resist[ELEMENT_PHYSICAL]);
        const int life     = physical *
                         (Affixes_Total(roam->character, MOD_LIFE_STEAL) + life_steal +
                          Roam__CursePercent(roam, target, "Life Tap", now)) /
                         100; // (Life Tap on it: some as life)
        const int mana  = physical * (Affixes_Total(roam->character, MOD_MANA_STEAL) + mana_steal) / 100;
        roam->player.hp = roam->player.hp + life < roam->player.max_hp ? roam->player.hp + life : roam->player.max_hp;
        roam->mana      = fminf((float)Character_MaxMana(roam->character), roam->mana + (float)mana);
    }
    if (!Roam__RunsMonsters(roam)) {
        // Multiplayer: the map's owner takes it off (and says if it died: then the experience is ours)
        NetWriter *writer = malloc(sizeof(NetWriter));
        if (writer != NULL) {
            NetWriter_Begin(writer);
            NetWriter_I32(writer, Roam__MapKey(roam));
            NetWriter_U16(writer, (uint16_t)(target - roam->units));
            NetWriter_I32(writer, damage);
            NetClient_Send(MSG_HIT_MONSTER, writer);
            free(writer);
        }
        Roam__UnitSound(roam, target, MONSTER_SOUND_HIT);
        return;
    }
    const bool was_alive = Roam__IsAlive(target);
    Roam__Damage(roam, target, damage, now);
    if (was_alive && !Roam__IsAlive(target)) {
        if (roam->online) {
            Roam__SendKilled(roam, (int)(target - roam->units), NetClient_MyId()); // shared with our party
        } else {
            Roam__GainExperience(roam, target);
        }
    }
}

// Attacking from where you stand (Stand still, Modern movement, the attack key): what's under the mouse if it's in
// reach, else the nearest monster in reach, else a swing at the air towards the mouse
static void Roam__AttackInPlace(SceneRoam *roam, const int hovered, const float x, const float y, const uint64_t now) {
    Unit *player = &roam->player;
    if (player->state == UNIT_ATTACK || player->state == UNIT_HIT || !Roam__IsAlive(player)) {
        return;
    }
    int target = -1;
    if (Roam__Combat(roam)) {
        float nearest = ATTACK_RANGE;
        for (int i = 0; i < roam->unit_count; i++) {
            const Unit *unit = &roam->units[i];
            if (!Roam__IsCombatant(unit) || !Roam__IsAlive(unit) || Roam__IsPeaceful(roam, unit)) {
                continue;
            }
            const float distance = Roam__Distance(player->x, player->y, unit->x, unit->y);
            if (i == hovered && distance <= ATTACK_RANGE) {
                target = i;
                break;
            }
            if (distance <= nearest) {
                nearest = distance;
                target  = i;
            }
        }
    }
    roam->moving          = false;
    roam->use_target      = -1;
    roam->attack_target   = target;
    roam->attack_in_place = true;
    Roam__ClearPath(player);
    Roam__TurnTowards(&player->turn, player->x, player->y, target >= 0 ? roam->units[target].x : x,
                      target >= 0 ? roam->units[target].y : y);
    Roam__SetState(player, UNIT_ATTACK, now);
    Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
}

static Missile *Roam__SpawnEffect(SceneRoam *roam, const char *name, float x, float y, float dx, float dy);
static bool     Roam__InMyParty(const NetPlayer *player);
#include "SceneRoamLog.inc"
#include "SceneRoamPortals.inc"
#include "SceneRoamShrines.inc"
#include "SceneRoamSkills.inc"

// Softcore death, the game's way: some of the experience towards your next level goes (5% on Normal, 10% Nightmare,
// 15% Hell; never below the level you have), some of your gold, and what you wear and carry in your belt stays on your
// corpse where you fell until you walk back to it
static void Roam__DeathPenalty(SceneRoam *roam) {
    Character *c         = roam->character;
    const int  pct       = 5 * (Roam__Difficulty(roam) + 1);
    const int  level     = c->level;
    uint32_t   floor     = Character_ExperienceFor(level);
    uint32_t   next      = Character_ExperienceFor(level + 1);
    uint32_t   lost_exp  = next > floor ? (next - floor) * (uint32_t)pct / 100 : 0;
    lost_exp             = c->experience - floor < lost_exp ? c->experience - floor : lost_exp;
    c->experience       -= lost_exp;
    const int lost_gold  = c->gold * (10 + (Roam__Difficulty(roam) * 5)) / 100;
    c->gold             -= lost_gold;
    bool on_corpse       = false;
    if (!c->has_corpse) { // (a second death before getting back to the first corpse leaves it as it is)
        for (int e = 0; e < EQUIP_COUNT; e++) {
            on_corpse = on_corpse || c->equipped[e].code[0] != '\0';
        }
        for (int b = 0; b < BELT_MAX; b++) {
            on_corpse = on_corpse || c->belt[b].code[0] != '\0';
        }
    }
    if (on_corpse) {
        memcpy(c->corpse_equipped, c->equipped, sizeof(c->equipped));
        memcpy(c->corpse_belt, c->belt, sizeof(c->belt));
        memset(c->equipped, 0, sizeof(c->equipped));
        memset(c->belt, 0, sizeof(c->belt));
        c->has_corpse          = true;
        roam->corpse           = roam->player;
        roam->corpse.state     = UNIT_DEAD;
        roam->corpse_key       = Roam__MapKey(roam);
        roam->corpse.has_looks = true;
        snprintf(roam->corpse.name, sizeof(roam->corpse.name), "%s's corpse", c->name);
        Roam__ApplyCharacter(roam);
    }
    char text[160];
    snprintf(text, sizeof(text), "You lost %u experience and %d gold.%s", lost_exp, lost_gold,
             on_corpse ? " Your gear is on your corpse: go back for it." : "");
    Roam__ShowMessage(roam, text);
    Roam__StoreCharacter(roam);
}

// Walking up to your corpse gives you back what's on it (into the inventory, or at your feet, when you've put other
// things on since)
static void Roam__TakeCorpse(SceneRoam *roam) {
    Character *c = roam->character;
    if (!roam->game || !c->has_corpse || roam->corpse_key == 0 || roam->corpse_key != Roam__MapKey(roam) ||
        !Roam__IsAlive(&roam->player) ||
        Roam__Distance(roam->corpse.x, roam->corpse.y, roam->player.x, roam->player.y) > 1.8f) {
        return;
    }
    for (int s = 0; s < EQUIP_COUNT + BELT_MAX; s++) {
        Item *from = s < EQUIP_COUNT ? &c->corpse_equipped[s] : &c->corpse_belt[s - EQUIP_COUNT];
        Item *to   = s < EQUIP_COUNT ? &c->equipped[s] : &c->belt[s - EQUIP_COUNT];
        if (from->code[0] == '\0') {
            continue;
        }
        if (to->code[0] == '\0') {
            *to = *from;
        } else if (!Inventory_Add(c, from)) {
            Roam__PutOnGround(roam, from, roam->player.x, roam->player.y);
        }
        memset(from, 0, sizeof(Item));
    }
    c->has_corpse    = false;
    roam->corpse_key = 0;
    Roam__ApplyCharacter(roam);
    Roam__ShowMessage(roam, "You took your things back from your corpse.");
    Sounds_Play("item_pickup", SOUND_KIND_SFX);
    Roam__StoreCharacter(roam);
}

static void Roam__UpdatePlayer(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit *player = &roam->player;
    Roam__TakeCorpse(roam);

    if (player->state == UNIT_DEAD) {
        if (roam->game && roam->character->dead) {
            return; // hardcore: no coming back
        }
        if (now >= roam->respawn_at) {
            if (roam->game) {
                // Like the game: you wake up in town with full life
                roam->character->life = Character_MaxLife(roam->character);
                Roam__ResetPlayer(roam);
                Roam__GoToTown(roam, roam->character->act);
            } else {
                Roam__ResetPlayer(roam); // back at the start with full life
            }
            roam->safe_until = now + RESPAWN_GRACE;
            for (int i = 0; i < roam->unit_count; i++) {
                roam->units[i].provoked = false;
            }
        }
        return;
    }

    // Single Player: mana comes back over time; running uses stamina, resting brings it back
    if (roam->game && Roam__IsAlive(player)) {
        const float max_mana    = (float)Character_MaxMana(roam->character);
        const float max_stamina = (float)Character_MaxStamina(roam->character);
        const int   regen       = Character_ManaRegenSeconds(roam->character);
        roam->mana              = fminf(max_mana, roam->mana + (max_mana * seconds / (float)(regen > 0 ? regen : 120)));
        // Replenish Life on what's worn: the game's N/256 a frame, 25 frames a second
        roam->life_carry += (float)Affixes_Total(roam->character, MOD_REPLENISH_LIFE) * 25.0f / 256.0f * seconds;
        // Potions drunk give their life and mana over a few seconds
        if (roam->potion_life > 0.0f) {
            const float healed  = fminf(roam->potion_life, roam->potion_life_rate * seconds);
            roam->potion_life  -= healed;
            roam->life_carry   += healed;
        }
        if (roam->life_carry >= 1.0f) { // whole points of life
            const int whole   = (int)roam->life_carry;
            roam->life_carry -= (float)whole;
            player->hp        = player->hp + whole > player->max_hp ? player->max_hp : player->hp + whole;
        }
        if (roam->potion_mana > 0.0f) {
            const float gained  = fminf(roam->potion_mana, roam->potion_mana_rate * seconds);
            roam->potion_mana  -= gained;
            roam->mana          = fminf(max_mana, roam->mana + gained);
        }
        if (Roam__IsRunning(roam) && !roam->sprinting && !(roam->shrine_code == 14 && now < roam->shrine_until) &&
            now >= roam->stamina_until && (roam->moving || roam->key_dx != 0.0f || roam->key_dy != 0.0f)) {
            // charstats RunDrain is per frame (25 a second), in 256ths
            roam->stamina -= (float)Character_RunDrain(roam->character) * 25.0f / 256.0f * seconds;
            if (roam->stamina <= 0.0f) {
                roam->stamina   = 0.0f;
                roam->exhausted = true;
            }
        } else {
            roam->stamina = fminf(max_stamina, roam->stamina + (max_stamina * seconds / 25.0f));
        }
        if (roam->exhausted && roam->stamina >= max_stamina * 0.1f) {
            roam->exhausted = false;
        }
    }
    Roam__UpdateCast(roam, now);
    if (Roam__Animate(roam, player, now)) {
        if (player->state == UNIT_CAST) {
            if (!roam->cast_fired) {
                Roam__ApplyCast(roam, now);
            }
            roam->cast_skill = NULL;
            Roam__SetState(player, UNIT_IDLE, now);
        } else if (player->state == UNIT_ATTACK) {
            // The blow lands at the end of the swing
            if (roam->attack_target >= 0) {
                Unit *target = &roam->units[roam->attack_target];
                if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE + 0.5f) {
                    target->provoked = true;
                    Roam__PlayerStrikes(roam, target, now);
                }
            }
            const Unit *other = NULL;
            if (roam->attack_other > 0 && Roam__HostileTo(roam, roam->attack_other) &&
                Roam__OtherById(roam, roam->attack_other, &other) &&
                Roam__Distance(player->x, player->y, other->x, other->y) <= ATTACK_RANGE + 0.5f) {
                // PvP: they roll whether it lands, with their defense
                int rolled[ELEMENT_COUNT];
                Roam__RollPlayerDamage(roam, rolled);
                Roam__SendHitPlayer(roam->attack_other, true, player->attack_rating, player->level, rolled);
            }
            if (roam->attack_in_place) {
                roam->attack_target   = -1; // one swing, no chasing
                roam->attack_other    = 0;
                roam->attack_in_place = false;
            }
            if (roam->melee_skill != NULL) {
                roam->melee_skill   = NULL; // one blow with the skill; then plain ones
                roam->attack_target = -1;
            }
            Roam__SetState(player, UNIT_IDLE, now);
        } else if (player->state == UNIT_DYING) {
            Roam__SetState(player, UNIT_DEAD, now);
            roam->respawn_at    = now + RESPAWN_DELAY;
            roam->attack_target = -1;
            if (roam->game && roam->character->hardcore) {
                roam->character->dead = true;
                roam->leave_at        = now + HARDCORE_DEATH_MS;
                Roam__StoreCharacter(roam);
                Roam__ShowMessage(roam, "You have died. Hardcore characters don't come back.");
            } else if (roam->game) {
                Roam__DeathPenalty(roam);
            }
            return;
        } else {
            Roam__SetState(player, UNIT_IDLE, now);
        }
    }
    if (player->state == UNIT_DYING || player->state == UNIT_ATTACK || player->state == UNIT_HIT ||
        player->state == UNIT_CAST) {
        return;
    }

    // Modern / Combined: walking with the movement keys (or the stick), straight, sliding along walls
    const bool key_walking = roam->key_dx != 0.0f || roam->key_dy != 0.0f;
    if (key_walking) {
        roam->use_target    = -1;
        roam->attack_target = -1;
        roam->item_target   = -1;
        roam->attack_other  = 0;
        roam->moving        = false;
        Roam__ClearPath(player);
        const float step = Roam__PlayerSpeed(roam) * seconds *
                           (player->even_speed ? Roam__ScreenSpeedFactor(roam->key_dx, roam->key_dy) : 1.0f);
        const float nx = player->x + (roam->key_dx * step);
        const float ny = player->y + (roam->key_dy * step);
        if (Map_IsWalkable(roam->map, (int)nx, (int)ny)) {
            player->x = nx;
            player->y = ny;
        } else if (Map_IsWalkable(roam->map, (int)nx, (int)player->y)) {
            player->x = nx;
        } else if (Map_IsWalkable(roam->map, (int)player->x, (int)ny)) {
            player->y = ny;
        }
        Roam__TurnTowards(&player->turn, player->x, player->y, player->x + roam->key_dx, player->y + roam->key_dy);
        // Heading somewhere a few steps ahead (entrances take you in when you walk into them)
        player->target_x = player->x + (roam->key_dx * 3.0f);
        player->target_y = player->y + (roam->key_dy * 3.0f);
    }

    // Opening something: walk up to it, then open it
    if (!key_walking && roam->use_target >= 0 && !Roam__TryUse(roam, now)) {
        player->target_x = roam->units[roam->use_target].x;
        player->target_y = roam->units[roam->use_target].y;
        roam->moving     = true;
    }
    // Picking something up: the same
    if (!key_walking && roam->item_target >= 0 && !Roam__TryPickUp(roam)) {
        player->target_x = roam->ground[roam->item_target].x;
        player->target_y = roam->ground[roam->item_target].y;
        roam->moving     = true;
    }

    // Attacking: walk up to the target, then swing
    if (!key_walking && roam->attack_target >= 0) {
        Unit *target = &roam->units[roam->attack_target];
        if (!Roam__Combat(roam) || !Roam__IsAlive(target)) {
            roam->attack_target = -1;
        } else {
            if (Roam__Distance(player->x, player->y, target->x, target->y) <= ATTACK_RANGE) {
                Roam__TurnTowards(&player->turn, player->x, player->y, target->x, target->y);
                roam->moving = false;
                Roam__ClearPath(player);
                Roam__SetState(player, UNIT_ATTACK, now);
                Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
                return;
            }
            player->target_x = target->x;
            player->target_y = target->y;
            roam->moving     = true;
        }
    }

    // PvP: walk up to the other player, then swing
    if (!key_walking && roam->attack_other > 0) {
        const Unit *other = NULL;
        if (!Roam__HostileTo(roam, roam->attack_other) || !Roam__OtherById(roam, roam->attack_other, &other) ||
            !Roam__OtherAlive(other)) {
            roam->attack_other = 0;
        } else if (Roam__Distance(player->x, player->y, other->x, other->y) <= ATTACK_RANGE) {
            Roam__TurnTowards(&player->turn, player->x, player->y, other->x, other->y);
            roam->moving = false;
            Roam__ClearPath(player);
            Roam__SetState(player, UNIT_ATTACK, now);
            Roam__UnitSound(roam, player, MONSTER_SOUND_ATTACK);
            return;
        } else {
            player->target_x = other->x;
            player->target_y = other->y;
            roam->moving     = true;
        }
    }

    if (!key_walking && roam->moving) {
        // Around walls, not through them
        Roam__PlanPath(roam->map, player, player->target_x, player->target_y, now, REPATH_PLAYER_MS);
        roam->moving = Roam__FollowPath(roam->map, player, Roam__PlayerSpeed(roam), seconds);
        // A long walk: the search only looks so far at once, so carry on from where that leg ended
        if (!roam->moving && player->path.partial && roam->long_walk_legs < LONG_WALK_LEGS &&
            Roam__Distance(player->x, player->y, player->target_x, player->target_y) > ARRIVE_DISTANCE * 2.0f) {
            roam->long_walk_legs++;
            Roam__ClearPath(player);
            Roam__PlanPath(roam->map, player, player->target_x, player->target_y, now, REPATH_PLAYER_MS);
            roam->moving = Roam__FollowPath(roam->map, player, Roam__PlayerSpeed(roam), seconds);
        }
        if (!roam->moving) {
            roam->long_walk_legs = 0;
            Roam__ClearPath(player);
            if (roam->use_target >= 0 && !Roam__TryUse(roam, now)) {
                roam->use_target = -1; // got as close as it gets, and it's still out of reach
            }
            if (roam->item_target >= 0 && !Roam__TryPickUp(roam)) {
                roam->item_target = -1;
            }
        }
    }
    Roam__SetState(player, (roam->moving || key_walking) ? UNIT_WALK : UNIT_IDLE, now);

    // A footstep as each foot comes down (the start and the middle of the walk cycle)
    Figure *figure = player->state == UNIT_WALK ? Roam__Figure(roam, player) : NULL;
    if (figure != NULL) {
        const int frames = Figure_GetFramesPerDirection(figure);
        const int frame  = frames > 0 ? player->frame % frames : 0;
        if (frame != roam->step_frame && (frame == 0 || frame == frames / 2)) {
            Roam__Footstep(roam);
        }
        roam->step_frame = frame;
    } else {
        roam->step_frame = -1;
    }
}

// Strolling around home while nobody's fighting: wait a few seconds, walk to a random reachable spot nearby, repeat
static void Roam__Wander(SceneRoam *roam, Unit *unit, const float seconds, const uint64_t now) {
    if (unit->can_walk < 0) {
        // Guards keep their post; everyone else with a walk animation strolls
        const bool guard  = strstr(unit->id, "guard") != NULL;
        unit->can_walk    = !guard && Roam__HasMode(roam, unit, "wl") ? 1 : 0;
        unit->next_wander = now + (uint64_t)Roam__Roll(500, WANDER_PAUSE_MAX_MS); // don't all set off at once
    }
    if (unit->can_walk == 0) {
        Roam__SetState(unit, UNIT_IDLE, now);
        return;
    }

    if (unit->wandering) {
        const float speed = unit->speed < WANDER_SPEED_MAX ? unit->speed : WANDER_SPEED_MAX;
        if (Roam__FollowPath(roam->map, unit, speed, seconds)) {
            Roam__SetState(unit, UNIT_WALK, now);
            return;
        }
        unit->wandering   = false; // arrived (or bumped into something): rest a while
        unit->next_wander = now + (uint64_t)Roam__Roll(WANDER_PAUSE_MIN_MS, WANDER_PAUSE_MAX_MS);
        Roam__ClearPath(unit);
    }
    Roam__SetState(unit, UNIT_IDLE, now);

    if (now < unit->next_wander) {
        return;
    }
    // A minion keeps close to its unique: home is wherever the unique is
    if (unit->leader > 0 && unit->leader <= roam->unit_count) {
        const Unit *leader = &roam->units[unit->leader - 1];
        if (leader->rank == RANK_UNIQUE && Roam__IsAlive(leader)) {
            unit->home_x = leader->x;
            unit->home_y = leader->y;
        }
    }
    // Somewhere walkable near home (a few tries), then plan the way there
    const float radius = unit->npc ? WANDER_NPC_RADIUS : WANDER_MONSTER_RADIUS;
    for (int attempt = 0; attempt < 6; attempt++) {
        const float angle    = (float)(rand() % 628) / 100.0f;
        const float distance = radius * (float)(30 + (rand() % 71)) / 100.0f;
        const float tx       = floorf(unit->home_x + (cosf(angle) * distance)) + 0.5f;
        const float ty       = floorf(unit->home_y + (sinf(angle) * distance)) + 0.5f;
        if (!Map_IsWalkable(roam->map, (int)floorf(tx), (int)floorf(ty)) ||
            Roam__Distance(tx, ty, unit->x, unit->y) < 1.0f) {
            continue;
        }
        Roam__ClearPath(unit);
        if (Map_FindPath(roam->map, unit->x, unit->y, tx, ty, &unit->path)) {
            unit->path_goal_x = tx;
            unit->path_goal_y = ty;
            unit->wandering   = true;
            return;
        }
    }
    unit->next_wander = now + WANDER_PAUSE_MIN_MS; // nowhere to go right now
}

#include "SceneRoamAI.inc"

// Monsters walking don't stand inside each other: two closer than a body's width step a little apart (onto ground they
// can stand on), so a pack goes round each other to get at you instead of piling into one spot
static void Roam__SpreadOut(SceneRoam *roam) {
    const float room = 0.9f; // sub-tiles between two
    for (int i = 0; i < roam->unit_count; i++) {
        Unit *a = &roam->units[i];
        if (a->is_object || a->npc || !Roam__IsAlive(a) || a->state != UNIT_WALK || a->vanished || a->burrowed ||
            a->trap != NULL || Roam__NearestPlayer(roam, a->x, a->y) > ACTIVE_DISTANCE) {
            continue;
        }
        for (int j = 0; j < roam->unit_count; j++) {
            const Unit *b = &roam->units[j];
            if (j == i || b->is_object || !Roam__IsAlive(b) || b->vanished || b->burrowed || b->trap != NULL) {
                continue;
            }
            const float dx = a->x - b->x;
            const float dy = a->y - b->y;
            const float d2 = (dx * dx) + (dy * dy);
            if (d2 >= room * room) {
                continue;
            }
            const float d  = sqrtf(d2);
            const float ux = d > 0.001f ? dx / d : (float)((i % 3) - 1) * 0.7f; // (on the same spot: any way)
            const float uy = d > 0.001f ? dy / d : (float)(((i / 3) % 3) - 1) * 0.7f;
            const float nx = a->x + (ux * (room - d) * 0.5f);
            const float ny = a->y + (uy * (room - d) * 0.5f);
            if (Map_IsWalkable(roam->map, (int)nx, (int)ny)) {
                a->x = nx;
                a->y = ny;
            }
        }
    }
}

static void Roam__UpdateUnits(SceneRoam *roam, const float seconds, const uint64_t now) {
    Unit          *player       = &roam->player;
    const bool     player_alive = Roam__IsAlive(player) && now >= roam->safe_until; // can be fought
    const bool     runs         = Roam__RunsMonsters(roam); // multiplayer: or the map's owner does, and tells us
    const uint64_t real         = SDL_GetTicks64();

    for (int i = 0; i < roam->unit_count; i++) {
        Unit *unit = &roam->units[i];
        // Like the game's rooms: only what's near a player is awake. The rest of the act waits where it is (and picks
        // up again when someone comes near), so a joined-up act of hundreds of monsters costs only what's around.
        if (Roam__NearestPlayer(roam, unit->x, unit->y) > ACTIVE_DISTANCE && unit->state != UNIT_DYING &&
            i != roam->attack_target) {
            continue;
        }
        if (unit->ally) {
            Roam__UpdateAlly(roam, unit, seconds, now); // our summons: fighting for us, wherever the monsters are run
            continue;
        }
        if (unit->remote_owner > 0) {
            // Someone else's summon: where they last said, smoothly; gone when they stop saying
            if (unit->vanished) {
                continue;
            }
            if (real - unit->remote_seen > 2000) {
                unit->vanished = true;
                continue;
            }
            const float step = fminf((float)(real - unit->net_at) / 200.0f, 1.0f);
            unit->x          = unit->net_from_x + ((unit->net_to_x - unit->net_from_x) * step);
            unit->y          = unit->net_from_y + ((unit->net_to_y - unit->net_from_y) * step);
            if (Roam__Animate(roam, unit, now) && unit->state != UNIT_DEAD) {
                Roam__SetState(unit, unit->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
            }
            continue;
        }
        if (runs) {
            Roam__UpdateAilments(roam, unit, seconds, now); // poison working
        }
        if (!runs && !unit->is_object) {
            // Multiplayer, someone else runs this map's monsters: moving smoothly to where they last said, and
            // animating; what happens to them (and what they do to us) comes from the owner
            if (unit->net_at > 0) {
                const float step = fminf((float)(real - unit->net_at) / (1000.0f / NET_STATE_HZ), 1.0f);
                unit->x          = unit->net_from_x + ((unit->net_to_x - unit->net_from_x) * step);
                unit->y          = unit->net_from_y + ((unit->net_to_y - unit->net_from_y) * step);
            }
            if (Roam__Animate(roam, unit, now)) { // a swing, a flinch or a death finished
                Roam__SetState(unit, unit->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
            }
            continue;
        }
        if (Roam__Animate(roam, unit, now)) {
            if (unit->state == UNIT_ATTACK && unit->ranged_swing) {
                unit->ranged_swing = false; // a shot or a spell: what it threw does the damage
            } else if (unit->state == UNIT_ATTACK && Roam__Combat(roam)) {
                if (unit->spectral) { // Spectral Hit: a different element each blow
                    unit->element = ELEMENT_FIRE + (rand() % 3);
                }
                const Unit *other = NULL;
                Unit       *ally  = unit->fight_ally > 0 && unit->fight_ally <= roam->unit_count
                                        ? &roam->units[unit->fight_ally - 1]
                                        : NULL;
                if (ally != NULL) {
                    // One of our summons
                    if (Roam__IsAlive(ally) &&
                        Roam__Distance(unit->x, unit->y, ally->x, ally->y) <= ATTACK_RANGE + 0.5f &&
                        Roam__Roll(1, 100) <=
                            Character_HitChance(unit->attack_rating, unit->level, ally->defense, ally->level)) {
                        const int damage = Roam__Roll(unit->damage_min, unit->damage_max);
                        Roam__Damage(roam, ally, damage, now);
                        Roam__IronMaiden(roam, unit, damage, now);
                    }
                } else if (unit->net_target == 0) {
                    if (player_alive && Roam__Distance(unit->x, unit->y, player->x, player->y) <= ATTACK_RANGE + 0.5f &&
                        (!roam->game || Roam__Roll(1, 100) <= Character_HitChance(unit->attack_rating, unit->level,
                                                                                  player->defense, player->level))) {
                        int damage = Roam__Roll(unit->damage_min, unit->damage_max);
                        if (unit->element > 0 && Roam__Roll(1, 100) <= unit->element_chance) {
                            // Its fire, cold, lightning or poison, against our resistance to it
                            damage += Roam__ElementTaken(roam, (Element)unit->element,
                                                         Roam__Roll(unit->element_min, unit->element_max));
                        }
                        damage = damage * Roam__MonsterDamageFactor(roam, unit, now) / 100; // Weaken
                        if (unit->aura != NULL && strcmp(unit->aura->id, "Might") == 0) {
                            damage += damage / 2; // its Might
                        }
                        // Amplify Damage / Decrepify on you: more of it gets through
                        const int taken = 100 - Roam__PlayerCurse(roam, "damageresist");
                        damage          = damage * (taken > 0 ? taken : 0) / 100;
                        if (roam->game && Roam__Roll(1, 100) <= roam->character->bonus[BONUS_DODGE]) {
                            damage = 0; // dodged
                        } else if (Roam__Blocks(roam, false)) {
                            damage = 0;
                        }
                        damage = Roam__AbsorbDamage(roam, damage); // Bone Armor
                        if (damage > 0) {
                            if (unit->element == ELEMENT_COLD) {
                                Roam__ChillMe(roam, 2000, now); // its cold slows you
                            }
                            if (unit->mana_burn && roam->game) { // Mana Burn: as much mana as life
                                roam->mana = fmaxf(0.0f, roam->mana - (float)damage);
                            }
                            Roam__Damage(roam, player, damage, now);
                            Roam__IronMaiden(roam, unit, damage, now);
                        }
                        const int thorns = roam->game ? Affixes_Total(roam->character, MOD_THORNS) : 0;
                        const int zap    = roam->game ? Affixes_Total(roam->character, MOD_LIGHTNING_THORNS) : 0;
                        if (damage > 0 && roam->game &&
                            (roam->character->bonus[BONUS_THORNS_PCT] > 0 || thorns > 0 || zap > 0)) {
                            // Thorns: some of it back (the skill's share, and what's worn: "Attacker Takes Damage",
                            // "... Lightning Damage")
                            const int back     = (damage * roam->character->bonus[BONUS_THORNS_PCT] / 100) + thorns;
                            const int dealt[5] = {back, 0, 0, zap, 0};
                            Roam__SkillHit(roam, unit, dealt, 0, 0, true, now);
                        }
                    }
                } else if (Roam__OtherById(roam, unit->net_target, &other) && Roam__OtherAlive(other) &&
                           Roam__Distance(unit->x, unit->y, other->x, other->y) <= ATTACK_RANGE + 1.0f) {
                    // Another player: they roll whether it lands, and resist the elemental part themselves
                    int damage[ELEMENT_COUNT] = {0};
                    damage[ELEMENT_PHYSICAL]  = Roam__Roll(unit->damage_min, unit->damage_max);
                    if (unit->element > 0 && unit->element < ELEMENT_COUNT &&
                        Roam__Roll(1, 100) <= unit->element_chance) {
                        damage[unit->element] = Roam__Roll(unit->element_min, unit->element_max);
                    }
                    if (unit->aura != NULL && strcmp(unit->aura->id, "Might") == 0) {
                        damage[ELEMENT_PHYSICAL] += damage[ELEMENT_PHYSICAL] / 2; // its Might
                    }
                    Roam__SendHitPlayer(unit->net_target, false, unit->attack_rating, unit->level, damage);
                    Roam__IronMaiden(roam, unit, damage[ELEMENT_PHYSICAL], now);
                }
            }
            if (unit->state == UNIT_OPERATE) {
                Roam__FinishOperate(roam, unit, now);
            } else {
                Roam__SetState(unit, unit->state == UNIT_DYING ? UNIT_DEAD : UNIT_IDLE, now);
            }
        }
        if (unit->burrowed) {
            Roam__Burrowed(roam, unit, player_alive, now);
            continue;
        }
        if (!Roam__IsAlive(unit) || !Roam__IsCombatant(unit)) {
            continue;
        }
        Roam__MonsterAura(roam, unit, now);
        if (unit->state == UNIT_ATTACK || unit->state == UNIT_HIT) {
            continue;
        }

        float target_x  = 0.0f;
        float target_y  = 0.0f;
        int   target_id = 0;
        bool  target    = Roam__MonsterTarget(roam, unit, player_alive, &target_x, &target_y, &target_id);
        // Our summons are fought too, when they're nearer than the player
        const int nearer_ally =
            Roam__AllyNear(roam, unit, target ? Roam__Distance(unit->x, unit->y, target_x, target_y) : AGGRO_RANGE);
        unit->fight_ally = 0;
        const int foe    = Roam__ConfusedFoe(roam, unit, now);
        if (foe >= 0) { // Confuse: it goes for one of its own instead
            target           = true;
            target_x         = roam->units[foe].x;
            target_y         = roam->units[foe].y;
            target_id        = 0;
            unit->fight_ally = foe + 1;
        } else if (nearer_ally >= 0) {
            target           = true;
            target_x         = roam->units[nearer_ally].x;
            target_y         = roam->units[nearer_ally].y;
            target_id        = 0;
            unit->fight_ally = nearer_ally + 1;
        }
        if (!Roam__Combat(roam) || !target || Roam__IsPeaceful(roam, unit)) {
            unit->provoked = false;
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }

        // Nests, eggs and suicide minions do their own thing (they've no blow of their own)
        if (unit->spawner != SPAWNER_NONE || unit->suicide) {
            const float distance = Roam__Distance(unit->x, unit->y, target_x, target_y);
            if (Roam__MonsterSpawns(roam, unit, distance, now)) {
                continue;
            }
            if (unit->suicide && distance < AGGRO_RANGE * 1.5f) { // runs at you
                Roam__TurnTowards(&unit->turn, unit->x, unit->y, target_x, target_y);
                const bool moved = Roam__Step(roam->map, unit, target_x, target_y,
                                              unit->speed * Roam__UnitSpeedFactor(roam, unit, now), seconds);
                Roam__SetState(unit, moved ? UNIT_WALK : UNIT_IDLE, now);
                continue;
            }
            if (!Roam__CanAttack(roam, unit)) {
                Roam__SetState(unit, UNIT_IDLE, now);
                continue;
            }
        }

        // Only units that can attack (have an attack animation) fight back; the rest carry on strolling
        if (!Roam__CanAttack(roam, unit)) {
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }

        // Combat: everyone nearby that can fight (NPCs included) comes for the nearest player
        const float distance = Roam__Distance(unit->x, unit->y, target_x, target_y);
        if (distance > AGGRO_RANGE && !unit->provoked) {
            // Its idle sound now and then (a growl, a hiss, a chuckle), when you're near enough to hear it
            if (roam->game && distance < AGGRO_RANGE * 2.0f && now >= unit->next_neutral) {
                if (unit->next_neutral != 0) {
                    Roam__UnitSound(roam, unit, MONSTER_SOUND_NEUTRAL);
                }
                unit->next_neutral = now + 6000 + (uint64_t)Roam__Roll(0, 12000);
            }
            Roam__Wander(roam, unit, seconds, now);
            continue;
        }
        unit->wandering = false;
        if (roam->game) {
            if (!unit->provoked && unit->next_neutral != 0) {
                Roam__UnitSound(roam, unit, MONSTER_SOUND_INIT); // (once it's heard you, its call when it sees you)
                unit->next_neutral = now + 6000;
            }
            unit->provoked = true; // it's seen you: its pack comes too
            Roam__CallPack(roam, unit);
            if (unit->fight_ally == 0 &&
                (Roam__MonsterFlees(roam, unit, target_x, target_y, now, seconds) ||
                 Roam__MonsterRaises(roam, unit, now) ||
                 Roam__MonsterSpecial(roam, unit, target_x, target_y, distance, target_id, now) ||
                 Roam__MonsterCasts(roam, unit, target_x, target_y, distance, now) ||
                 Roam__MonsterShoots(roam, unit, target_x, target_y, distance, now, seconds))) {
                unit->net_target = target_id;
                continue;
            }
        }
        if (distance <= ATTACK_RANGE) {
            Roam__TurnTowards(&unit->turn, unit->x, unit->y, target_x, target_y);
            Roam__ClearPath(unit);
            if (now >= unit->next_attack) {
                unit->next_attack = now + ATTACK_COOLDOWN;
                unit->net_target  = target_id;
                Roam__SetState(unit, UNIT_ATTACK, now);
                Roam__UnitSound(roam, unit, MONSTER_SOUND_ATTACK);
            } else {
                Roam__SetState(unit, UNIT_IDLE, now);
            }
        } else {
            // Chase: straight at the player when nothing's in the way, otherwise around the walls. Re-planning is
            // spread over time (each monster at its own moment) so a crowd doesn't all search in the same frame.
            bool moved;
            if (Map_HasClearLine(roam->map, unit->x, unit->y, target_x, target_y)) {
                Roam__ClearPath(unit);
                Roam__TurnTowards(&unit->turn, unit->x, unit->y, target_x, target_y);
                moved = Roam__Step(roam->map, unit, target_x, target_y,
                                   unit->speed * Roam__UnitSpeedFactor(roam, unit, now), seconds);
            } else {
                Roam__PlanPath(roam->map, unit, target_x, target_y, now, REPATH_MONSTER_MS + (uint64_t)((i % 8) * 37));
                moved =
                    Roam__FollowPath(roam->map, unit, unit->speed * Roam__UnitSpeedFactor(roam, unit, now), seconds);
            }
            Roam__SetState(unit, moved ? UNIT_WALK : UNIT_IDLE, now);
        }
    }
    if (runs) {
        Roam__SpreadOut(roam);
    }
}

// The living unit nearest the mouse (within reach), -1 if none
static int Roam__UnitAt(const SceneRoam *roam, const float sub_x, const float sub_y) {
    int   nearest  = -1;
    float distance = HOVER_RANGE;
    for (int i = 0; i < roam->unit_count; i++) {
        const Unit *unit = &roam->units[i];
        if (!Roam__IsAlive(unit) || (unit->is_object && !Roam__CanUse(unit)) || unit->remote_owner > 0) {
            continue; // torches, opened chests, ... (and others' summons) can't be clicked
        }
        // Figures stand on their feet but most of the body is above: aim a little higher up the screen
        const float d = Roam__Distance(unit->x - 0.8f, unit->y - 0.8f, sub_x, sub_y);
        if (d < distance) {
            distance = d;
            nearest  = i;
        }
    }
    return nearest;
}

// ---------------------------------------------------------------------------------------------------------------------
// Single Player: character screen (the game's own panel art) and keys

static const int stat_rows[STAT_COUNT] = {92, 154, 240, 302}; // strength, dexterity, vitality, energy (panel y)

static bool Roam__StatPlusAt(const int stat, const int x, const int y) {
    return x >= 112 && x <= 134 && y >= stat_rows[stat] - 10 && y <= stat_rows[stat] + 10;
}

static void Roam__PanelText(SceneRoam *roam, Label *label, const char *text, const int x, const int y, const uint8_t r,
                            const uint8_t g, const uint8_t b) {
    Label_SetText(label, text);
    Label_SetColor(label, r, g, b, 0xFF);
    Label_SetAlignment(label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_Draw(label, x, y);
    (void)roam;
}

// Where the open right-hand panel's close button is: false when neither the inventory nor the skill tree is open
static bool Roam__PanelClose(const SceneRoam *roam, SDL_Rect *rect) {
    if (!roam->game || (!roam->inventory_panel && !roam->skill_panel)) {
        return false;
    }
    *rect = roam->inventory_panel ? (SDL_Rect){GAME_PANEL_X + 18, 385, 32, 32}
                                  : (SDL_Rect){GAME_PANEL_X + 172, 385, 32, 32};
    return true;
}

static void Roam__DrawPanelClose(const SceneRoam *roam, const int mouse_x, const int mouse_y) {
    SDL_Rect rect;
    if (roam->panel_close == NULL || !Roam__PanelClose(roam, &rect)) {
        return;
    }
    bool left;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const bool pressed = left && SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &rect);
    Sprite_Draw(roam->panel_close, pressed ? 11 : 10, rect.x, rect.y + rect.h);
}

static void Roam__DrawCharacterPanel(SceneRoam *roam) {
    const Character *character = roam->character;
    Sprite_DrawMulti(roam->char_panel_sprite, 0, 0, 0, 2, 2);
    Label *small = roam->panel_label;
    Label *value = roam->panel_value_label;
    char   text[64];

    Roam__PanelText(roam, value, character->name, 96, 17, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, value, Character_ClassName(character->class_id), 252, 17, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, small, "Level", 32, 39, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->level);
    Roam__PanelText(roam, value, text, 32, 57, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Experience", 122, 39, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%lu", (unsigned long)character->experience);
    Roam__PanelText(roam, value, text, 122, 57, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Next Level", 252, 39, 0xC7, 0xB3, 0x77);
    if (character->level < CHARACTER_MAX_LEVEL) {
        snprintf(text, sizeof(text), "%lu", (unsigned long)Character_ExperienceFor(character->level + 1));
    } else {
        snprintf(text, sizeof(text), "-");
    }
    Roam__PanelText(roam, value, text, 252, 57, 0xFF, 0xFF, 0xFF);

    for (int stat = 0; stat < STAT_COUNT; stat++) {
        Roam__PanelText(roam, small, Character_StatName((CharacterStat)stat), 44, stat_rows[stat], 0xC7, 0xB3, 0x77);
        const int  shown   = Character_Stat(character, (CharacterStat)stat); // with what's worn: blue, like the game
        const bool boosted = shown != character->stats[stat];
        snprintf(text, sizeof(text), "%d", shown);
        Roam__PanelText(roam, value, text, 97, stat_rows[stat], boosted ? 0x69 : 0xFF, boosted ? 0x69 : 0xFF, 0xFF);
        if (character->stat_points > 0) {
            // Where the game shows its "+" button
            const SDL_Rect box = {116, stat_rows[stat] - 8, 16, 16};
            SDL_SetRenderDrawColor(sdl_renderer, 60, 50, 20, 255);
            SDL_RenderFillRect(sdl_renderer, &box);
            SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
            SDL_RenderDrawRect(sdl_renderer, &box);
            SDL_RenderDrawLine(sdl_renderer, box.x + 4, box.y + 8, box.x + 11, box.y + 8);
            SDL_RenderDrawLine(sdl_renderer, box.x + 8, box.y + 4, box.x + 8, box.y + 11);
        }
    }

    int damage_min;
    int damage_max;
    Character_Damage(character, &damage_min, &damage_max);
    for (int e = ELEMENT_FIRE; e < ELEMENT_COUNT; e++) { // the elemental damage counts too (before resistances)
        int low  = 0;
        int high = 0;
        Character_ElementDamage(character, (Element)e, &low, &high);
        damage_min += low;
        damage_max += high;
    }
    Roam__PanelText(roam, small, "Damage", 210, 92, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d-%d", damage_min, damage_max);
    Roam__PanelText(roam, value, text, 286, 92, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, Character_WeaponName(character), 210, 115, 0xA0, 0xA0, 0xA0);
    Roam__PanelText(roam, small, "Attack Rating", 215, 154, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", Character_AttackRating(character));
    Roam__PanelText(roam, value, text, 291, 154, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Defense", 215, 177, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", Character_Defense(character));
    Roam__PanelText(roam, value, text, 291, 177, 0xFF, 0xFF, 0xFF);
    Roam__PanelText(roam, small, "Skill Points", 215, 201, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->skill_points);
    Roam__PanelText(roam, value, text, 291, 201, 0xFF, 0xFF, 0xFF);

    const struct {
        const char *name;
        int         y;
        int         current;
        int         maximum;
    } pools[3] = {
        {"Stamina", 240, (int)roam->stamina, Character_MaxStamina(character)},
        {"Life",    263, roam->player.hp,    Character_MaxLife(character)   },
        {"Mana",    302, (int)roam->mana,    Character_MaxMana(character)   },
    };
    for (int p = 0; p < 3; p++) {
        Roam__PanelText(roam, small, pools[p].name, 195, pools[p].y, 0xC7, 0xB3, 0x77);
        snprintf(text, sizeof(text), "%d", pools[p].current);
        Roam__PanelText(roam, value, text, 250, pools[p].y, 0xFF, 0xFF, 0xFF);
        snprintf(text, sizeof(text), "%d", pools[p].maximum);
        Roam__PanelText(roam, value, text, 291, pools[p].y, 0xFF, 0xFF, 0xFF);
    }

    static const char *resists[4] = {"Fire Resist", "Cold Resist", "Lightning Res.", "Poison Resist"};
    for (int r = 0; r < 4; r++) {
        // With the difficulty's penalty; the game shows them red below 0 and blue when boosted
        const int resist = Character_Resist(character, (Element)(ELEMENT_FIRE + r), Roam__ResistPenalty(roam));
        snprintf(text, sizeof(text), "%d%%", resist);
        Roam__PanelText(roam, small, resists[r], 222, 341 + (r * 24), 0xC7, 0xB3, 0x77);
        Roam__PanelText(roam, value, text, 291, 341 + (r * 24), resist < 0 ? 0xFF : (resist > 0 ? 0x69 : 0xFF),
                        resist < 0 ? 0x40 : (resist > 0 ? 0x69 : 0xFF), resist < 0 ? 0x40 : 0xFF);
    }

    Roam__PanelText(roam, small, "Stat Points", 66, 396, 0xC7, 0xB3, 0x77);
    Roam__PanelText(roam, small, "Remaining", 66, 410, 0xC7, 0xB3, 0x77);
    snprintf(text, sizeof(text), "%d", character->stat_points);
    Roam__PanelText(roam, value, text, 145, 404, character->stat_points > 0 ? 0x40 : 0xFF, 0xFF,
                    character->stat_points > 0 ? 0x40 : 0xFF);
}

// Single Player keys, buttons and panels. Returns true when the scene is being left.
// ---------------------------------------------------------------------------------------------------------------------
// Single Player's control panel, the game's: life and mana globes, skills, run/walk, stamina and experience, the mini
// panel (character, inventory, skill tree, party, automap, messages, quests, game menu). Positions at 800x600.

#define CTRL_MINI_COUNT 8
#define CTRL_MINI_STEP  21

// Where everything is: the game's panel (Classic), or the Modern one (Options > Video > Control panel), laid out like
// the remaster's: the globes on their holders; between them the experience bar, the run button and stamina, then the
// mini panel (always shown), the left skill, a spot to drop items on the ground, the right skill, the belt
typedef struct CtrlLayout {
    bool     modern;
    int      run_x; // 16 x 20
    int      run_y;
    SDL_Rect stamina;
    SDL_Rect xp;
    bool     has_menu; // the button that opens the mini panel (Modern: always open)
    int      menu_x;
    int      menu_y;
    int      level_x; // "new stats" (30 x 30)
    int      level_y;
    int      skills_x; // "new skills"
    int      skills_y;
    int      mini_x;
    int      mini_y;
    SDL_Rect drop; // Modern: drop items here
    int      skill_left_x;
    int      skill_right_x;
    int      skill_y;
    int      belt_x;
    int      belt_y;
} CtrlLayout;

static const CtrlLayout ctrl_classic = {
    false,
    255,
    570,
    {273, 572, 102, 19},
    {256, 561, 120, 2 },
    true,
    392,
    562,
    206,
    561,
    562,
    561,
    312,
    532,
    {0,   0,   0,   0 },
    117,
    635,
    552,
    422,
    568
};
static const CtrlLayout ctrl_modern = {
    true,
    123,
    551,
    {143, 554, 150, 13},
    {124, 545, 552, 3 },
    false,
    0,
    0,
    609,
    560,
    643,
    560,
    120,
    551 + 22,
    {352, 556, 60,  40},
    300,
    416,
    552,
    470,
    562
};

static const CtrlLayout *Roam__Ctrl(void) { return AbyssConfiguration_GetModernPanel() ? &ctrl_modern : &ctrl_classic; }

enum { CTRL_RUN = 100, CTRL_MENU, CTRL_LEVEL, CTRL_SKILLS, CTRL_DROP }; // mini panel buttons are 0 to 7

static const char *ctrl_mini_names[CTRL_MINI_COUNT] = {"Character (A)", "Inventory (I)",  "Skill Tree (T)",
                                                       "Party (P)",     "Automap (Tab)",  "Message Log (M)",
                                                       "Quest Log (Q)", "Game Menu (Esc)"};

static bool Roam__In(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// The panel button under the mouse (CTRL_RUN, CTRL_MENU, CTRL_LEVEL, a mini panel slot), -1 = none
static int Roam__CtrlButtonAt(const SceneRoam *roam, const int x, const int y) {
    const CtrlLayout *at = Roam__Ctrl();
    if (Roam__In(x, y, at->run_x, at->run_y, 16, 20)) {
        return CTRL_RUN;
    }
    if (at->has_menu && Roam__In(x, y, at->menu_x, at->menu_y, 15, 24)) {
        return CTRL_MENU;
    }
    if (roam->character->stat_points > 0 && Roam__In(x, y, at->level_x, at->level_y, 30, 30)) {
        return CTRL_LEVEL;
    }
    if (roam->character->skill_points > 0 && Roam__In(x, y, at->skills_x, at->skills_y, 30, 30)) {
        return CTRL_SKILLS;
    }
    if (at->modern && SDL_PointInRect(&(SDL_Point){x, y}, &at->drop)) {
        return CTRL_DROP;
    }
    if (roam->mini_open || at->modern) {
        for (int b = 0; b < CTRL_MINI_COUNT; b++) {
            if (Roam__In(x, y, at->mini_x + 4 + (b * CTRL_MINI_STEP), at->mini_y + 3, 20, 20)) {
                return b;
            }
        }
    }
    return -1;
}

// Whether the mouse is on the panel (clicks there don't walk)
static bool Roam__OverControlPanel(const SceneRoam *roam, const int x, const int y) {
    const CtrlLayout *at = Roam__Ctrl();
    if (at->modern) {
        return y >= 540 || (y >= 496 && (x < 117 || x >= 683));
    }
    return y >= 545 || (y >= 496 && (x < 117 || x >= 683)) ||
           (roam->mini_open && Roam__In(x, y, at->mini_x, at->mini_y, 176, 26));
}

static void Roam__CtrlPress(SceneRoam *roam, const int button) {
    switch (button) {
    case CTRL_RUN:
        roam->running = !roam->running;
        break;
    case CTRL_MENU:
        roam->mini_open = !roam->mini_open;
        break;
    case CTRL_LEVEL:
    case 0: // character
        roam->char_panel = !roam->char_panel;
        break;
    case 1: // inventory
        roam->inventory_panel = !roam->inventory_panel;
        roam->skill_panel     = false;
        break;
    case CTRL_SKILLS:
    case 2: // skill tree
        roam->skill_panel     = !roam->skill_panel;
        roam->inventory_panel = false;
        break;
    case 4: // automap
        roam->show_automap = !roam->show_automap;
        break;
    case 3: // party
        Roam__ToggleParty(roam);
        break;
    case 5: // message log
        roam->log_open  = !roam->log_open;
        roam->help_open = false;
        break;
    case 6: // quest log
        roam->quest_panel = !roam->quest_panel;
        roam->quest_act   = roam->character != NULL ? roam->character->act : 0;
        roam->char_panel  = false;
        break;
    case 7: // game menu
        roam->menu_open = true;
        break;
    case CTRL_DROP:
        if (GameInventory_Holding(roam->inventory_ui)) {
            Roam__DropHeld(roam);
        } else {
            Roam__ShowMessage(roam, "Pick something up in the inventory, then click here (or anywhere in the world) "
                                    "to drop it.");
        }
        break;
    default: {
        if (button < 0 || button >= CTRL_MINI_COUNT) {
            break;
        }
        const char *name  = ctrl_mini_names[button];
        const char *paren = strchr(name, '(');
        char        text[96];
        snprintf(text, sizeof(text), "%.*s isn't in the engine yet.",
                 paren != NULL && paren > name ? (int)(paren - name - 1) : (int)strlen(name), name);
        Roam__ShowMessage(roam, text);
        break;
    }
    }
    Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    Roam__UpdateButtons(roam);
}

// Clicks on the panel; true when the click was the panel's
static bool Roam__UpdateControlPanel(SceneRoam *roam, const int mouse_x, const int mouse_y) {
    bool left;
    InputManager_GetMouseButtons(&left, NULL, NULL);
    const int under = Roam__CtrlButtonAt(roam, mouse_x, mouse_y);
    if (left && roam->ctrl_pressed < 0 && !roam->prev_left && under >= 0) {
        roam->ctrl_pressed = under; // pressed: acts when let go over it, like the game's buttons
    }
    if (!left && roam->ctrl_pressed >= 0) {
        if (under == roam->ctrl_pressed) {
            Roam__CtrlPress(roam, under);
        }
        roam->ctrl_pressed = -1;
        return true;
    }
    return roam->ctrl_pressed >= 0;
}

// A globe's fill, as full as value / max (the top cut away)
#define GLOBE_GLOW 190 // how much of the fill is added again (255: twice as bright)
static void Roam__DrawGlobe(const SceneRoam *roam, const int frame, const int x, const int y, const float value,
                            const float max) {
    const float    part = max > 0.0f ? fminf(fmaxf(value / max, 0.0f), 1.0f) : 0.0f;
    const int      high = (int)(80.0f * part);
    const SDL_Rect cut  = {0, 80 - high, 80, high};
    Sprite_DrawSection(roam->ctrl_globes, frame, x, y, &cut);
    // Brighter, like the game shows them: the fill once more, added (black adds nothing, so the shape and the
    // shading stay; the reds and blues light up)
    Sprite_SetBlendMode(roam->ctrl_globes, SDL_BLENDMODE_ADD);
    Sprite_SetAlphaMod(roam->ctrl_globes, GLOBE_GLOW);
    Sprite_DrawSection(roam->ctrl_globes, frame, x, y, &cut);
    Sprite_SetAlphaMod(roam->ctrl_globes, 255);
    Sprite_SetBlendMode(roam->ctrl_globes, SDL_BLENDMODE_BLEND);
}

// Puts the skill buttons and the belt where the panel in use has them
static void Roam__PlaceCtrl(void) {
    const CtrlLayout *at = Roam__Ctrl();
    GameSkills_SetButtons(at->skill_left_x, at->skill_right_x, at->skill_y);
    GameInventory_SetBeltPosition(at->belt_x, at->belt_y);
}

static void Roam__DrawCtrlBox(const SDL_Rect *box, const bool lit) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, lit ? 60 : 14, lit ? 50 : 12, lit ? 30 : 10, 235);
    SDL_RenderFillRect(sdl_renderer, box);
    SDL_SetRenderDrawColor(sdl_renderer, 120, 100, 60, 255);
    SDL_RenderDrawRect(sdl_renderer, box);
}

static void Roam__DrawControlPanel(SceneRoam *roam, const int mouse_x, const int mouse_y) {
    const Character  *character = roam->character;
    const CtrlLayout *at        = Roam__Ctrl();
    Roam__PlaceCtrl();
    // The panel: left globe holder, the middle pieces, right globe holder (a skill icon's width between)
    static const int pieces[6][2] = {
        {0, 0  },
        {1, 165},
        {2, 293},
        {3, 421},
        {4, 549},
        {5, 683}
    };
    for (int p = 0; p < 6; p++) {
        if (!at->modern || p == 0 || p == 5) { // Modern: the holders only, the middle is its own
            Sprite_Draw(roam->ctrl_panel, (uint8_t)pieces[p][0], pieces[p][1], 600);
        }
    }
    if (at->modern) {
        // A dark band between the holders with a gold edge, the remaster's way
        const SDL_Rect band = {117, 541, 566, 59};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 12, 11, 10, 242);
        SDL_RenderFillRect(sdl_renderer, &band);
        SDL_SetRenderDrawColor(sdl_renderer, 150, 125, 70, 255);
        SDL_RenderDrawLine(sdl_renderer, 117, 541, 682, 541);
        SDL_SetRenderDrawColor(sdl_renderer, 70, 60, 40, 255);
        SDL_RenderDrawLine(sdl_renderer, 117, 542, 682, 542);
        Roam__DrawCtrlBox(&at->stamina, false);
        Roam__DrawCtrlBox(&at->xp, false);
        // Where items go on the ground: an arrow pointing down
        const bool over = SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &at->drop);
        Roam__DrawCtrlBox(&at->drop, over && GameInventory_Holding(roam->inventory_ui));
        const int cx = at->drop.x + (at->drop.w / 2);
        const int cy = at->drop.y + (at->drop.h / 2);
        SDL_SetRenderDrawColor(sdl_renderer, 199, 179, 119, 255);
        SDL_RenderDrawLine(sdl_renderer, cx, cy - 10, cx, cy + 4);
        for (int i = 0; i < 6; i++) {
            SDL_RenderDrawLine(sdl_renderer, cx - 6 + i, cy + i, cx + 6 - i, cy + i);
        }
        SDL_RenderDrawLine(sdl_renderer, cx - 10, cy + 12, cx + 10, cy + 12);
        // Frames for the belt's slots
        for (int b = 0; b < 4; b++) {
            const SDL_Rect slot = {at->belt_x - 1 + (b * 31), at->belt_y - 1, 31, 31};
            Roam__DrawCtrlBox(&slot, false);
        }
    }
    // The globes sit on the holders (the angel and the demon hold them up), the glass over them
    Roam__DrawGlobe(roam, 0, 30, 507, (float)roam->player.hp, (float)roam->player.max_hp);
    Roam__DrawGlobe(roam, 1, 690, 508, roam->mana, (float)Character_MaxMana(character));
    Sprite_Draw(roam->ctrl_overlap, 0, 28, 595);
    Sprite_Draw(roam->ctrl_overlap, 1, 691, 592);
    // The left and right skills, and the belt's bottom row
    GameSkills_DrawButtons(roam->skills_ui, character);
    GameInventory_DrawBelt(roam->inventory_ui, character);

    // Stamina (red when you've run yourself out) and experience through this level
    const int      max_stamina = Character_MaxStamina(character);
    const float    stamina     = max_stamina > 0 ? fminf(roam->stamina / (float)max_stamina, 1.0f) : 0.0f;
    const SDL_Rect stamina_bar = {at->stamina.x, at->stamina.y, (int)((float)at->stamina.w * stamina), at->stamina.h};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    if (roam->exhausted || stamina < 0.25f) {
        SDL_SetRenderDrawColor(sdl_renderer, 0xC8, 0x20, 0x10, 0xC8);
    } else {
        SDL_SetRenderDrawColor(sdl_renderer, 0xE8, 0xC8, 0x58, 0xE0); // the game's yellow
    }
    SDL_RenderFillRect(sdl_renderer, &stamina_bar);
    const uint32_t low  = Character_ExperienceFor(character->level);
    const uint32_t high = Character_ExperienceFor(character->level + 1);
    const float    done = (character->level >= CHARACTER_MAX_LEVEL || high <= low)
                              ? 1.0f
                              : (float)(character->experience - low) / (float)(high - low);
    const SDL_Rect xp   = {at->xp.x, at->xp.y, (int)((float)at->xp.w * fminf(fmaxf(done, 0.0f), 1.0f)), at->xp.h};
    SDL_SetRenderDrawColor(sdl_renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderFillRect(sdl_renderer, &xp);

    // Buttons (drawn pressed while held)
    const int pressed = roam->ctrl_pressed == Roam__CtrlButtonAt(roam, mouse_x, mouse_y) ? roam->ctrl_pressed : -1;
    Sprite_Draw(roam->ctrl_run, (uint8_t)((roam->running ? 2 : 0) + (pressed == CTRL_RUN ? 1 : 0)), at->run_x,
                at->run_y + 20);
    if (at->has_menu) {
        Sprite_Draw(roam->ctrl_menu, (uint8_t)((roam->mini_open ? 2 : 0) + (pressed == CTRL_MENU ? 1 : 0)), at->menu_x,
                    at->menu_y + 24);
    }
    if (character->stat_points > 0) {
        Sprite_Draw(roam->ctrl_level, (uint8_t)(pressed == CTRL_LEVEL ? 1 : 0), at->level_x, at->level_y + 30);
    }
    if (character->skill_points > 0) {
        Sprite_Draw(roam->ctrl_level, (uint8_t)(pressed == CTRL_SKILLS ? 1 : 0), at->skills_x, at->skills_y + 30);
    }
    if (roam->mini_open || at->modern) {
        if (!at->modern) {
            Sprite_Draw(roam->ctrl_mini, 0, at->mini_x, at->mini_y + 26);
        }
        for (int b = 0; b < CTRL_MINI_COUNT; b++) {
            Sprite_Draw(roam->ctrl_mini_buttons, (uint8_t)((b * 2) + (pressed == b ? 1 : 0)),
                        at->mini_x + 4 + (b * CTRL_MINI_STEP), at->mini_y + 23);
        }
    }

    // What's under the mouse: the globes' numbers, the buttons' names
    char      text[64] = "";
    int       tx       = mouse_x;
    int       ty       = mouse_y - 18;
    const int button   = Roam__CtrlButtonAt(roam, mouse_x, mouse_y);
    if (Roam__In(mouse_x, mouse_y, 30, 507, 80, 80)) {
        snprintf(text, sizeof(text), "Life: %d / %d", roam->player.hp, roam->player.max_hp);
        tx = 70;
        ty = 490;
    } else if (Roam__In(mouse_x, mouse_y, 690, 508, 80, 80)) {
        snprintf(text, sizeof(text), "Mana: %d / %d", (int)roam->mana, Character_MaxMana(character));
        tx = 730;
        ty = 490;
    } else if (SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &at->stamina)) {
        snprintf(text, sizeof(text), "Stamina: %d / %d", (int)roam->stamina, max_stamina);
    } else if (Roam__In(mouse_x, mouse_y, at->xp.x, at->xp.y - 3, at->xp.w, at->xp.h + 6)) {
        snprintf(text, sizeof(text), "Experience: %u / %u", character->experience, high);
    } else if (button == CTRL_RUN) {
        snprintf(text, sizeof(text), roam->running ? "Walk (R)" : "Run (R)");
    } else if (button == CTRL_MENU) {
        snprintf(text, sizeof(text), roam->mini_open ? "Close Mini Panel" : "Open Mini Panel");
    } else if (button == CTRL_LEVEL) {
        snprintf(text, sizeof(text), "New Stats");
    } else if (button == CTRL_SKILLS) {
        snprintf(text, sizeof(text), "New Skills");
    } else if (button == CTRL_DROP) {
        snprintf(text, sizeof(text), "Drop on the ground");
    } else if (button >= 0) {
        snprintf(text, sizeof(text), "%s", ctrl_mini_names[button]);
    }
    if (text[0] != '\0') {
        Label_SetText(roam->target_label, text);
        Label_SetColor(roam->target_label, 0xFF, 0xFF, 0xFF, 0xFF);
        int w = 0;
        Label_GetSize(roam->target_label, &w, NULL);
        tx                 = tx - (w / 2) < 4 ? 4 + (w / 2) : (tx + (w / 2) > 796 ? 796 - (w / 2) : tx);
        const SDL_Rect box = {tx - (w / 2) - 4, ty - 18, w + 8, 20};
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 200);
        SDL_RenderFillRect(sdl_renderer, &box);
        Label_Draw(roam->target_label, tx, ty);
    }
}

// Drinking a potion: healing ones over a few seconds, rejuvenation at once
static void Roam__Drink(SceneRoam *roam, const Item *item) {
    const ItemBase *base = Items_Find(item->code);
    if (base == NULL) {
        return;
    }
    const float seconds = (float)(base->seconds > 0 ? base->seconds : 1);
    if (base->life > 0) {
        roam->potion_life      += (float)base->life;
        roam->potion_life_rate  = roam->potion_life / seconds;
    }
    if (base->mana > 0) {
        roam->potion_mana      += (float)base->mana;
        roam->potion_mana_rate  = roam->potion_mana / seconds;
    }
    if (base->rejuvenate_pct > 0) {
        const float max_mana = (float)Character_MaxMana(roam->character);
        roam->player.hp      = roam->player.hp + (roam->player.max_hp * base->rejuvenate_pct / 100);
        roam->player.hp      = roam->player.hp > roam->player.max_hp ? roam->player.max_hp : roam->player.hp;
        roam->mana           = fminf(max_mana, roam->mana + (max_mana * (float)base->rejuvenate_pct / 100.0f));
    }
    // Stamina, antidote and thawing: 30 seconds each (antidote cures poison, thawing warms you up)
    const uint64_t now = Roam__Now(roam);
    switch (Items_SpecialPotion(base)) {
    case 1:
        roam->stamina_until = now + 30000;
        roam->stamina       = (float)Character_MaxStamina(roam->character);
        roam->exhausted     = false;
        break;
    case 2:
        roam->antidote_until     = now + 30000;
        roam->player.poison_left = 0.0f;
        break;
    case 3:
        roam->thawing_until      = now + 30000;
        roam->player.chill_until = 0;
        break;
    default:
        break;
    }
    Sounds_Play(base->use_sound[0] != '\0' ? base->use_sound : "item_potion", SOUND_KIND_SFX);
}

// Single Player's inventory, skill tree, belt and skill buttons. They take their clicks before the world does; true
// when this frame's mouse belongs to them (or an item is on the cursor, which nothing in the world takes yet).
// ---------------------------------------------------------------------------------------------------------------------
// Multiplayer: the party screen (P). Everyone in the game, and a button to invite them, accept their invitation, or
// leave your party. A party shares the experience of kills made near each other, and can't fight each other.

#include "SceneRoamTrade.inc"

#define PARTY_X     70
#define PARTY_Y     80
#define PARTY_W     660
#define PARTY_ROW   26
#define PARTY_ROWS  12
#define PARTY_TOP   (PARTY_Y + 44)
#define PARTY_BTN_W 100

static int Roam__PartyHeight(int rows) { return 44 + (rows * PARTY_ROW) + 46; }

static bool Roam__InMyParty(const NetPlayer *player) {
    return NetClient_MyParty() != 0 && player != NULL && player->party == NetClient_MyParty();
}

#include "SceneRoamHostile.inc"

static SDL_Rect Roam__PartyButton(const int row) {
    return (SDL_Rect){PARTY_X + PARTY_W - PARTY_BTN_W - 12, PARTY_TOP + (row * PARTY_ROW) + 2, PARTY_BTN_W, 21};
}

static SDL_Rect Roam__PartyTradeButton(const int row) {
    const SDL_Rect invite = Roam__PartyButton(row);
    return (SDL_Rect){invite.x - PARTY_BTN_W - 8, invite.y, PARTY_BTN_W, invite.h};
}

// Hostile (PvP games) and Loot (they may take what's on your corpse), left of Trade
static SDL_Rect Roam__PartyHostileButton(const int row) {
    const SDL_Rect trade = Roam__PartyTradeButton(row);
    return (SDL_Rect){trade.x - PARTY_BTN_W - 8, trade.y, PARTY_BTN_W, trade.h};
}

static SDL_Rect Roam__PartyLootButton(const int row) {
    const SDL_Rect hostile = Roam__PartyHostileButton(row);
    return (SDL_Rect){hostile.x - PARTY_BTN_W - 8, hostile.y, PARTY_BTN_W, hostile.h};
}

static SDL_Rect Roam__PartyLeaveButton(const int rows) {
    return (SDL_Rect){PARTY_X + (PARTY_W / 2) - 70, PARTY_Y + Roam__PartyHeight(rows) - 36, 140, 24};
}

static void Roam__SendPartyMessage(const uint8_t type, const int id) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    if (id > 0) {
        NetWriter_U8(writer, (uint8_t)id);
    }
    NetClient_Send(type, writer);
    free(writer);
}

// Clicks on the party screen; true when the mouse is over it (the click was its)
static bool Roam__UpdatePartyPanel(SceneRoam *roam, const int mouse_x, const int mouse_y, const bool click) {
    if (!roam->party_panel || !roam->online) {
        return false;
    }
    NetPlayer      *players = NULL;
    const int       count   = NetClient_Players(&players);
    const int       rows    = count < PARTY_ROWS ? count : PARTY_ROWS;
    const SDL_Point mouse   = {mouse_x, mouse_y};
    const SDL_Rect  panel   = {PARTY_X, PARTY_Y, PARTY_W, Roam__PartyHeight(rows > 0 ? rows : 1)};
    if (!SDL_PointInRect(&mouse, &panel)) {
        return false;
    }
    if (!click) {
        return true;
    }
    for (int r = 0; r < rows; r++) {
        const SDL_Rect hostile = Roam__PartyHostileButton(r);
        const SDL_Rect loot    = Roam__PartyLootButton(r);
        if (Roam__PvPGame() && SDL_PointInRect(&mouse, &hostile)) {
            Roam__DeclareHostile(roam, &players[r]);
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
            return true;
        }
        if (SDL_PointInRect(&mouse, &loot)) {
            Roam__ToggleLoot(roam, &players[r]);
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
            return true;
        }
        const SDL_Rect trade = Roam__PartyTradeButton(r);
        if (SDL_PointInRect(&mouse, &trade)) {
            Roam__TradeButton(roam, &players[r]);
            Sounds_Play("cursor_button_click", SOUND_KIND_UI);
            return true;
        }
        const SDL_Rect button = Roam__PartyButton(r);
        if (!SDL_PointInRect(&mouse, &button) || Roam__InMyParty(&players[r])) {
            continue;
        }
        char text[96];
        if (NetClient_InviteFrom() == players[r].id) {
            Roam__SendPartyMessage(MSG_PARTY_ACCEPT, players[r].id);
            NetClient_ClearInvite();
        } else {
            Roam__SendPartyMessage(MSG_PARTY_INVITE, players[r].id);
            snprintf(text, sizeof(text), "You invited %s to your party.", players[r].name);
            Roam__ShowMessage(roam, text);
        }
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
        return true;
    }
    const SDL_Rect leave = Roam__PartyLeaveButton(rows > 0 ? rows : 1);
    if (NetClient_MyParty() != 0 && SDL_PointInRect(&mouse, &leave)) {
        Roam__SendPartyMessage(MSG_PARTY_LEAVE, 0);
        Roam__ShowMessage(roam, "You left the party.");
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    return true;
}

static void Roam__PartyButtonDraw(SceneRoam *roam, const SDL_Rect *box, const char *text, const bool hover) {
    SDL_SetRenderDrawColor(sdl_renderer, hover ? 70 : 35, hover ? 60 : 30, hover ? 35 : 20, 240);
    SDL_RenderFillRect(sdl_renderer, box);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, box);
    Label_SetText(roam->item_label, text);
    Label_SetColor(roam->item_label, 0xFF, 0xFF, 0xFF, 0xFF);
    int w = 0;
    int h = 0;
    Label_GetSize(roam->item_label, &w, &h);
    Label_Draw(roam->item_label, box->x + ((box->w - w) / 2), box->y + ((box->h - h) / 2));
}

static void Roam__DrawPartyPanel(SceneRoam *roam, const int mouse_x, const int mouse_y) {
    NetPlayer      *players = NULL;
    const int       count   = NetClient_Players(&players);
    const int       rows    = count < PARTY_ROWS ? count : PARTY_ROWS;
    const SDL_Point mouse   = {mouse_x, mouse_y};
    const SDL_Rect  panel   = {PARTY_X, PARTY_Y, PARTY_W, Roam__PartyHeight(rows > 0 ? rows : 1)};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 225);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 0xC7, 0xB3, 0x77, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);
    Label_SetText(roam->item_label, NetClient_MyParty() != 0 ? "Party  (you're in a party)" : "Party");
    Label_SetColor(roam->item_label, 0xC7, 0xB3, 0x77, 0xFF);
    Label_Draw(roam->item_label, PARTY_X + 14, PARTY_Y + 12);
    if (rows == 0) {
        Label_SetText(roam->item_label, "Nobody else is in the game.");
        Label_SetColor(roam->item_label, 0xA0, 0xA0, 0xA0, 0xFF);
        Label_Draw(roam->item_label, PARTY_X + 14, PARTY_TOP + 4);
    }
    for (int r = 0; r < rows; r++) {
        const NetPlayer *player = &players[r];
        uint8_t          cr;
        uint8_t          cg;
        uint8_t          cb;
        NetColor_Get(player->color, &cr, &cg, &cb);
        char text[96];
        snprintf(text, sizeof(text), "%s  (level %d %s)", player->name, player->level,
                 Character_ClassName(player->class_id));
        Label_SetText(roam->item_label, text);
        Label_SetColor(roam->item_label, cr, cg, cb, 0xFF);
        Label_Draw(roam->item_label, PARTY_X + 14, PARTY_TOP + (r * PARTY_ROW) + 4);
        const SDL_Rect trade = Roam__PartyTradeButton(r);
        Roam__PartyButtonDraw(roam, &trade, roam->trade_asked_by == player->id ? "Accept Trade" : "Trade",
                              SDL_PointInRect(&mouse, &trade));
        const SDL_Rect loot = Roam__PartyLootButton(r);
        Roam__PartyButtonDraw(roam, &loot, roam->loot_allowed[player->id] ? "No Loot" : "Loot",
                              SDL_PointInRect(&mouse, &loot));
        if (Roam__PvPGame()) {
            char           label[24];
            const SDL_Rect hostile = Roam__PartyHostileButton(r);
            Roam__HostileLabel(roam, player->id, label, sizeof(label));
            Roam__PartyButtonDraw(roam, &hostile, label, SDL_PointInRect(&mouse, &hostile));
        }
        const SDL_Rect button = Roam__PartyButton(r);
        if (Roam__InMyParty(player)) {
            Label_SetText(roam->item_label, "In your party");
            Label_SetColor(roam->item_label, 0x60, 0xFF, 0x60, 0xFF);
            Label_Draw(roam->item_label, button.x + 6, button.y + 3);
        } else {
            Roam__PartyButtonDraw(roam, &button, NetClient_InviteFrom() == player->id ? "Accept" : "Invite",
                                  SDL_PointInRect(&mouse, &button));
        }
    }
    if (NetClient_MyParty() != 0) {
        const SDL_Rect leave = Roam__PartyLeaveButton(rows > 0 ? rows : 1);
        Roam__PartyButtonDraw(roam, &leave, "Leave Party", SDL_PointInRect(&mouse, &leave));
    }
}

#include "SceneRoamNpc.inc"
#include "SceneRoamQuests.inc"

static bool Roam__UpdateGamePanels(SceneRoam *roam, const int mouse_x, const int mouse_y) {
    Character *character = roam->character;
    bool       left_down;
    bool       right_down;
    InputManager_GetMouseButtons(&left_down, NULL, &right_down);
    const bool left_click  = left_down && !roam->ui_prev_left;
    const bool right_click = right_down && !roam->ui_prev_right;
    roam->ui_prev_left     = left_down;
    roam->ui_prev_right    = right_down;

    if (Roam__UpdatePartyPanel(roam, mouse_x, mouse_y, left_click)) {
        roam->ui_owns_press = left_down || right_down;
        return true; // the party screen's
    }
    if (Roam__UpdateSpeech(roam, left_click)) {
        roam->ui_owns_press = left_down || right_down;
        return true; // the click closed what they were saying
    }
    if (Roam__UpdateNpcMenu(roam, mouse_x, mouse_y, left_click)) {
        roam->ui_owns_press = left_down || right_down;
        return true; // what the town folk do
    }
    if (Roam__UpdateHirelingPortrait(roam, mouse_x, mouse_y, left_click)) {
        roam->ui_owns_press = left_down || right_down;
        return true; // their screen opened or closed
    }
    Roam__KeepShop(roam);
    Roam__KeepHireling(roam);
    Roam__KeepHirelingScreen(roam);
    Roam__CowLevel(roam);
    if (roam->online) {
        Roam__KeepTrade(roam);
    }
    Roam__CheckQuests(roam, SDL_GetTicks64());
    if (Controls_Pressed(ACTION_QUESTS)) {
        roam->quest_panel = !roam->quest_panel;
        if (roam->quest_panel) {
            roam->quest_act  = roam->character->act;
            roam->char_panel = false;
            Roam__CloseShop(roam);
            GameInventory_SetStash(roam->inventory_ui, false);
        }
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    if (roam->char_panel) {
        roam->quest_panel = false;
    }
    if (Roam__UpdateQuestPanel(roam, mouse_x, mouse_y, left_click)) {
        roam->ui_owns_press = left_down || right_down;
        return true;
    }
    if (Controls_Pressed(ACTION_INVENTORY)) {
        roam->inventory_panel = !roam->inventory_panel;
        roam->skill_panel     = false;
        roam->panel           = PANEL_NONE;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    // The skill keys (F1-F8): over a skill in a picker, binds it (again: unbinds); otherwise puts the bound skill on
    // its button, like the game
    for (int h = 0; h < 8; h++) {
        if (!Controls_Pressed((ControlAction)(ACTION_SKILL_1 + h))) {
            continue;
        }
        bool      left   = false;
        const int picked = GameSkills_PickerSkillAt(roam->skills_ui, character, mouse_x, mouse_y, &left);
        char      text[96];
        if (picked != SKILL_NONE_PICKED) {
            const bool same =
                character->hotkey_set[h] && character->hotkey_skill[h] == picked && character->hotkey_left[h] == left;
            for (int other = 0; other < 8; other++) { // (one key per skill and button)
                if (character->hotkey_set[other] && character->hotkey_skill[other] == picked &&
                    character->hotkey_left[other] == left) {
                    character->hotkey_set[other] = false;
                }
            }
            character->hotkey_set[h]   = !same;
            character->hotkey_skill[h] = picked;
            character->hotkey_left[h]  = left;
            const SkillClass *table    = Skills_ForClass(character->class_id);
            snprintf(text, sizeof(text), same ? "Skill %d: none" : "Skill %d: %s", h + 1,
                     picked >= 0 && picked < table->count ? table->skills[picked].name : "Attack");
            Roam__ShowMessage(roam, text);
        } else if (character->hotkey_set[h]) {
            *(character->hotkey_left[h] ? &character->left_skill : &character->right_skill) =
                character->hotkey_skill[h];
            GameSkills_ClosePicker(roam->skills_ui);
        }
    }
    if (Controls_Pressed(ACTION_SKILL_BAR)) {
        GameSkills_ToggleSpeedBar(roam->skills_ui);
    }
    if (Controls_Pressed(ACTION_SKILL_TREE)) {
        roam->skill_panel     = !roam->skill_panel;
        roam->inventory_panel = false;
        roam->panel           = PANEL_NONE;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    if (roam->panel != PANEL_NONE) { // the waypoint list takes the right side
        roam->inventory_panel = false;
        roam->skill_panel     = false;
    }
    // The open panel's close button (the game's square with the "no" sign, bottom corner of the panel)
    SDL_Rect   close_rect;
    const bool closed_panel = Roam__PanelClose(roam, &close_rect) && left_click &&
                              SDL_PointInRect(&(SDL_Point){mouse_x, mouse_y}, &close_rect) &&
                              !GameInventory_Holding(roam->inventory_ui);
    if (closed_panel) {
        roam->inventory_panel = false;
        roam->skill_panel     = false;
        Sounds_Play("cursor_button_click", SOUND_KIND_UI);
    }
    Item drunk;
    for (int b = 0; b < 4; b++) {
        if (Controls_Pressed((ControlAction)(ACTION_BELT_1 + b)) && Roam__IsAlive(&roam->player) &&
            GameInventory_DrinkBelt(character, b, &drunk)) {
            Roam__Drink(roam, &drunk);
        }
    }

    Roam__PlaceCtrl(); // the skill buttons and belt where the panel in use has them
    // The inventory's pages (Options > Gameplay, or the multiplayer game's own setting; never fewer than are used)
    // and how worn gear is picked up
    const GameplaySettings *gameplay = Roam__Gameplay(roam);
    const int               pages    = roam->online ? NetClient_Settings()->inventory_pages : gameplay->inventory_pages;
    const int               used     = Inventory_PagesUsed(character);
    Inventory_SetPages(pages > used ? pages : used);
    GameInventory_SetStashPages(roam->inventory_ui,
                                roam->online ? NetClient_Settings()->stash_pages : gameplay->stash_pages,
                                gameplay->shared_pages);
    if (!GameInventory_StashOpen(roam->inventory_ui)) {
        const int stash_used  = Stash_PagesUsed(character);
        const int stash_pages = roam->online ? NetClient_Settings()->stash_pages : gameplay->stash_pages;
        Stash_SetPages(stash_pages > stash_used ? stash_pages : stash_used); // (for what goes in by itself)
    }
    GameInventory_SetModernPickup(roam->inventory_ui, gameplay->modern_pickup);
    GameInventory_SetPortalAllowed(roam->inventory_ui, !roam->in_town);

    // The stash closes with the inventory, the character screen, or walking away from it
    if (GameInventory_StashOpen(roam->inventory_ui)) {
        const Unit *stash =
            roam->stash_object >= 0 && roam->stash_object < roam->unit_count ? &roam->units[roam->stash_object] : NULL;
        if (!roam->inventory_panel || roam->char_panel || stash == NULL ||
            Roam__DistanceToObject(stash, roam->player.x, roam->player.y) > USE_RANGE + 4.0f) {
            GameInventory_SetStash(roam->inventory_ui, false);
            roam->stash_object = -1;
            if (roam->online && NetClient_ServerKeepsCharacters()) {
                Roam__StoreCharacter(roam); // the server's copy has what's in the stash now
            }
        }
    }
    const bool picker_was_open = GameSkills_PickerOpen(roam->skills_ui);
    const bool over_panels = ((roam->inventory_panel || roam->skill_panel) && GameInventory_IsOver(mouse_x, mouse_y)) ||
                             GameInventory_IsOverStash(roam->inventory_ui, mouse_x, mouse_y) ||
                             GameInventory_IsOverShop(roam->inventory_ui, mouse_x, mouse_y) ||
                             GameInventory_IsOverBelt(character, mouse_x, mouse_y) ||
                             GameSkills_IsOver(roam->skills_ui, roam->skill_panel, mouse_x, mouse_y);
    const char *message = NULL;
    const int   skill_events =
        GameSkills_Update(roam->skills_ui, character, roam->skill_panel, mouse_x, mouse_y, left_click, &message);
    if (message != NULL) {
        Roam__ShowMessage(roam, message);
    }
    if (skill_events & SKILLS_EVENT_RAISED) {
        Sounds_Play("cursor_point_drop", SOUND_KIND_UI);
    }
    const bool skills_took = (skill_events & SKILLS_EVENT_CLICKED) != 0;
    const bool shift       = (SDL_GetModState() & KMOD_SHIFT) != 0;
    // The belt's rows above the bottom one: with Show belt, or while the mouse is over the belt
    GameInventory_SetBeltShown(
        roam->show_belt || GameInventory_IsOverBelt(character, mouse_x, mouse_y) ? Inventory_BeltSize(character) : 4);
    const int item_events = GameInventory_Update(roam->inventory_ui, character, roam->inventory_panel, mouse_x, mouse_y,
                                                 left_click && !skills_took,
                                                 right_click && Roam__IsAlive(&roam->player), shift, &drunk, &message);
    if (message != NULL) {
        Roam__ShowMessage(roam, message);
    }
    if (item_events & INVENTORY_EVENT_EQUIPMENT) {
        Roam__ApplyCharacter(roam); // damage and defense from what's worn now
    }
    if (item_events & INVENTORY_EVENT_DRINK) {
        Roam__Drink(roam, &drunk);
    }
    if (item_events & INVENTORY_EVENT_PORTAL) {
        Roam__OpenTownPortal(roam);
    }
    if ((item_events & INVENTORY_EVENT_SWAP) || Controls_Pressed(ACTION_SWAP_WEAPONS)) {
        Roam__SwapWeapons(roam);
    }
    if ((item_events & INVENTORY_EVENT_GOLD) && GameInventory_StashOpen(roam->inventory_ui)) {
        const int room = Stash_GoldLimit(character) - character->stash_gold;
        const int most = character->gold < room ? character->gold : room;
        if (most <= 0) {
            Roam__ShowMessage(roam, "Your stash can't hold any more gold.");
        } else {
            AmountDialog_Open(roam->amount, "Deposit Gold", "How much gold do you want to put in your stash?", most,
                              most, AMOUNT_FOR_STASH_PUT);
        }
    } else if (item_events & INVENTORY_EVENT_GOLD) {
        AmountDialog_Open(roam->amount, "Drop Gold", "How much gold do you want to drop?", character->gold,
                          character->gold, AMOUNT_FOR_DROP);
    }
    if (item_events & INVENTORY_EVENT_TRADE) {
        Roam__TradeChanged(roam);
    }
    if (item_events & INVENTORY_EVENT_TRADE_OK) {
        Roam__TradeAccept(roam);
    }
    if (item_events & INVENTORY_EVENT_TRADE_END) {
        Roam__TradeClose(roam, true);
        roam->inventory_panel = false;
    }
    if (item_events & INVENTORY_EVENT_TRADE_GOLD) {
        AmountDialog_Open(roam->amount, "Offer Gold", "How much gold do you want to offer?", character->gold,
                          roam->trade != NULL ? roam->trade->my_gold : 0, AMOUNT_FOR_TRADE);
    }
    if (item_events & INVENTORY_EVENT_SHOP_CLOSE) {
        Roam__CloseShop(roam);
        roam->inventory_panel = false;
    }
    if (item_events & INVENTORY_EVENT_HIRE_CLOSE) {
        roam->hire_open = false;
        GameInventory_SetHireling(roam->inventory_ui, NULL);
    }
    if (item_events & INVENTORY_EVENT_HIRELING) {
        const int hireling = Roam__FindHireling(roam);
        if (hireling >= 0) {
            const int lost = roam->units[hireling].max_hp - roam->units[hireling].hp;
            Roam__HirelingStats(roam, &roam->units[hireling]); // what they wear now
            roam->units[hireling].hp =
                roam->units[hireling].max_hp - lost > 1 ? roam->units[hireling].max_hp - lost : 1;
        }
    }
    if (item_events & INVENTORY_EVENT_STASH_GOLD) {
        const int room = Roam__GoldLimit(roam) - character->gold;
        const int most = character->stash_gold < room ? character->stash_gold : room;
        if (most <= 0) {
            Roam__ShowMessage(roam, "You can't carry any more gold.");
        } else {
            AmountDialog_Open(roam->amount, "Withdraw Gold", "How much gold do you want to take out?", most, most,
                              AMOUNT_FOR_STASH_TAKE);
        }
    }
    if ((item_events & INVENTORY_EVENT_MOVED) && GameInventory_Sound(roam->inventory_ui)[0] != '\0') {
        Sounds_Play(GameInventory_Sound(roam->inventory_ui), SOUND_KIND_SFX);
    }
    // A click in the world with an item on the cursor puts it on the ground at your feet
    bool dropped = false;
    if (left_click && GameInventory_Holding(roam->inventory_ui) && !over_panels &&
        item_events == INVENTORY_EVENT_NONE && !Roam__OverControlPanel(roam, mouse_x, mouse_y)) {
        Roam__DropHeld(roam);
        dropped = true; // the click was for that, not for walking
    }
    const bool holding = GameInventory_Holding(roam->inventory_ui);
    const bool took    = closed_panel || skills_took || picker_was_open || item_events != INVENTORY_EVENT_NONE ||
                      holding || dropped || ((left_click || right_click) && over_panels);
    roam->ui_owns_press = (left_down || right_down) && (roam->ui_owns_press || took);
    return took || roam->ui_owns_press;
}

static void Roam__ExitGame(SceneRoam *roam);

static bool Roam__UpdateGame(SceneRoam *roam, const uint64_t now, const int mouse_x, const int mouse_y,
                             const bool clicked) {
    if (roam->leave_at != 0 && now >= roam->leave_at) {
        Roam__ExitGame(roam); // hardcore death
        return true;
    }

    if (Controls_Pressed(ACTION_PARTY)) {
        Roam__ToggleParty(roam);
    }
    if (Controls_Pressed(ACTION_HIRELING)) {
        Roam__ToggleHireling(roam);
    }
    if (Controls_Pressed(ACTION_CHARACTER) || Button_Update(roam->character_button)) {
        roam->char_panel = !roam->char_panel;
        Roam__UpdateButtons(roam);
    }
    if (roam->char_panel && clicked) {
        for (int stat = 0; stat < STAT_COUNT; stat++) {
            if (!Roam__StatPlusAt(stat, mouse_x, mouse_y)) {
                continue;
            }
            // Vitality and energy raise life, mana and stamina by what they add, now
            roam->character->life    = roam->player.hp;
            roam->character->mana    = (int)roam->mana;
            roam->character->stamina = (int)roam->stamina;
            if (Character_SpendStatPoint(roam->character, (CharacterStat)stat)) {
                roam->player.hp = roam->character->life;
                roam->mana      = (float)roam->character->mana;
                roam->stamina   = (float)roam->character->stamina;
                Roam__ApplyCharacter(roam);
                Sounds_Play("cursor_point_drop", SOUND_KIND_UI);
            }
        }
    }

    const bool can_waypoint = roam->in_town || Roam__NearWaypoint(roam) || Roam__DebugTools(roam);
    if (Controls_Pressed(ACTION_WAYPOINT) || Button_Update(roam->places_button)) {
        if (roam->panel == PANEL_PLACES || can_waypoint) {
            Roam__TogglePanel(roam, PANEL_PLACES);
        } else {
            Roam__ShowMessage(roam, "Stand near a waypoint (or go back to town) to travel.");
        }
    }
    if ((Controls_Pressed(ACTION_TOWN) || Button_Update(roam->town_button)) && !roam->in_town &&
        Roam__IsAlive(&roam->player)) {
        Roam__GoToTown(roam, roam->character->act);
        Roam__UpdateButtons(roam);
    }

    if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        if (roam->panel != PANEL_NONE || roam->char_panel || roam->inventory_panel || roam->skill_panel ||
            roam->party_panel || roam->quest_panel || GameSkills_PickerOpen(roam->skills_ui)) {
            roam->quest_panel     = false;
            roam->panel           = PANEL_NONE;
            roam->party_panel     = false;
            roam->char_panel      = false;
            roam->inventory_panel = false;
            roam->skill_panel     = false;
            GameSkills_ClosePicker(roam->skills_ui);
            Roam__UpdateButtons(roam);
        } else {
            roam->menu_open = true; // the game menu, like the game's
        }
    }
    if (Button_Update(roam->back_button)) {
        if (!roam->character->dead) {
            Roam__StoreCharacter(roam);
        }
        Roam__ExitGame(roam);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Multiplayer

// Out of the game: Single Player to the character list; multiplayer leaves the game (stops it, when hosting) and goes
// back to the Multiplayer screen
static void Roam__ExitGame(SceneRoam *roam) {
    if (roam->online) {
        Multiplayer_Leave();
        Scene_Set(SCENE_REF(MultiplayerMenu));
    } else {
        Scene_Set(SCENE_REF(CharacterSelect));
    }
}

static void Roam__AddChat(SceneRoam *roam, const char *text) {
    memmove(roam->chat_lines[0], roam->chat_lines[1], sizeof(roam->chat_lines[0]) * (CHAT_LINES - 1));
    memmove(&roam->chat_times[0], &roam->chat_times[1], sizeof(roam->chat_times[0]) * (CHAT_LINES - 1));
    snprintf(roam->chat_lines[CHAT_LINES - 1], sizeof(roam->chat_lines[CHAT_LINES - 1]), "%s", text);
    roam->chat_times[CHAT_LINES - 1] = SDL_GetTicks64();
    Roam__LogAdd(roam, text, true);
}

// The others in the game, where they are in this area: shown moving smoothly from one position to the next
static void Roam__UpdateOthers(SceneRoam *roam, const uint64_t now) {
    NetPlayer     *players = NULL;
    const int      count   = NetClient_Players(&players);
    const uint64_t real    = SDL_GetTicks64();
    static Unit    kept[NET_MAX_PLAYERS]; // (too big for the stack with 255 players)
    static int     kept_ids[NET_MAX_PLAYERS];
    static int     kept_colors[NET_MAX_PLAYERS];
    int            kept_count = 0;
    for (int p = 0; p < count && kept_count < NET_MAX_PLAYERS; p++) {
        const NetPlayer *player = &players[p];
        if (!player->has_state || player->place != Roam__MapKey(roam)) {
            continue; // somewhere else
        }
        Unit unit;
        int  found = -1;
        for (int o = 0; o < roam->other_count; o++) {
            found = roam->other_ids[o] == player->id ? o : found;
        }
        if (found >= 0) {
            unit = roam->others[found]; // keeps its animation going
        } else {
            memset(&unit, 0, sizeof(Unit));
            snprintf(unit.base, sizeof(unit.base), "chars");
            unit.idle_mode  = "nu";
            unit.frame_time = now;
        }
        snprintf(unit.id, sizeof(unit.id), "%s", Character_ClassToken(player->class_id));
        snprintf(unit.name, sizeof(unit.name), "%s", player->name);
        unit.has_looks   = player->has_looks;
        unit.looks       = player->looks;
        const float step = fminf((float)(real - player->moved_at) / (1000.0f / NET_STATE_HZ), 1.0f);
        unit.x           = player->from_x + ((player->x - player->from_x) * step);
        unit.y           = player->from_y + ((player->y - player->from_y) * step);
        unit.turn        = player->turn;
        unit.level       = player->level;
        const bool moving =
            strcmp(player->mode, "wl") == 0 || strcmp(player->mode, "rn") == 0 || strcmp(player->mode, "tw") == 0;
        if (strcmp(unit.net_mode, player->mode) != 0) {
            snprintf(unit.net_mode, sizeof(unit.net_mode), "%s", player->mode);
            unit.frame      = 0;
            unit.frame_time = now;
        }
        unit.state = moving ? UNIT_WALK : UNIT_IDLE;
        Roam__Animate(roam, &unit, now);
        kept[kept_count]        = unit;
        kept_colors[kept_count] = player->color;
        kept_ids[kept_count++]  = player->id;
    }
    memcpy(roam->others, kept, sizeof(Unit) * (size_t)kept_count);
    memcpy(roam->other_ids, kept_ids, sizeof(int) * (size_t)kept_count);
    memcpy(roam->other_colors, kept_colors, sizeof(int) * (size_t)kept_count);
    roam->other_count = kept_count;
}

// The map's owner, 15 times a second: where the monsters near any player are, how they are, what they're doing
static void Roam__SendMonsters(SceneRoam *roam) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_I32(writer, Roam__MapKey(roam));
    const int count_at = writer->size;
    NetWriter_U16(writer, 0);
    int count = 0;
    for (int i = 0; i < roam->unit_count && count < 2500; i++) {
        const Unit *unit = &roam->units[i];
        if (unit->is_object || unit->ally || unit->remote_owner > 0 ||
            Roam__NearestPlayer(roam, unit->x, unit->y) > MONSTER_SYNC_DISTANCE) {
            continue; // (our summons are ours alone)
        }
        NetWriter_U16(writer, (uint16_t)i);
        NetWriter_F32(writer, unit->x);
        NetWriter_F32(writer, unit->y);
        NetWriter_I32(writer, unit->hp);
        NetWriter_I32(writer, unit->max_hp);
        NetWriter_U8(writer, (uint8_t)unit->state);
        NetWriter_U8(writer, (uint8_t)unit->turn);
        NetWriter_U8(writer, (uint8_t)((unit->vanished ? 1 : 0) | (unit->burrowed ? 2 : 0)));
        NetWriter_U16(writer, unit->aura != NULL ? (uint16_t)unit->aura->row : 0xFFFF); // (a unique's, to be seen)
        count++;
    }
    writer->data[count_at]     = (uint8_t)(count & 0xFF);
    writer->data[count_at + 1] = (uint8_t)(count >> 8);
    if (count > 0) {
        NetClient_Send(MSG_MONSTERS, writer);
    }
    free(writer);
}

// Ours, five times a second: where our summons and hireling are, for the others to see
static void Roam__SendAllies(SceneRoam *roam) {
    NetWriter *writer = malloc(sizeof(NetWriter));
    FAIL_IF_NULL(writer);
    NetWriter_Begin(writer);
    NetWriter_I32(writer, Roam__MapKey(roam));
    const int count_at = writer->size;
    NetWriter_U8(writer, 0);
    int count = 0;
    for (int i = 0; i < roam->unit_count && count < 32; i++) {
        const Unit *unit = &roam->units[i];
        if (!unit->ally || unit->turned || !Roam__IsAlive(unit)) {
            continue;
        }
        NetWriter_U8(writer, (uint8_t)(i & 0xFF)); // (its slot: its place in our list, the same while it lives)
        NetWriter_String(writer, unit->id);
        NetWriter_F32(writer, unit->x);
        NetWriter_F32(writer, unit->y);
        NetWriter_U8(writer, (uint8_t)unit->state);
        NetWriter_U8(writer, (uint8_t)unit->turn);
        count++;
    }
    writer->data[count_at] = (uint8_t)count;
    // Then our buffs and aura (states.txt names), for our figure
    char      states[UNIT_STATES][32];
    const int state_count = Roam__PlayerStates(roam, states, UNIT_STATES);
    NetWriter_U8(writer, (uint8_t)state_count);
    for (int s = 0; s < state_count; s++) {
        NetWriter_String(writer, states[s]);
    }
    // Then what reaches our party near us: our aura, and our warcries (Battle Orders, Shout, Battle Command)
    int             aura_level = 0;
    const SkillRow *aura       = roam->game ? Roam__ActiveAura(roam, &aura_level) : NULL;
    if (aura == NULL && roam->game) {
        Roam__SpiritAura(roam, &aura, &aura_level); // (the Druid's spirit's, then)
    }
    NetWriter_U16(writer, aura != NULL ? (uint16_t)aura->row : 0xFFFF);
    NetWriter_U8(writer, (uint8_t)(aura_level < 255 ? aura_level : 255));
    const int count_cries = writer->size;
    NetWriter_U8(writer, 0);
    int            cries   = 0;
    const uint64_t now_cry = Roam__Now(roam);
    for (int b = 0; b < roam->buff_count && cries < 4; b++) {
        const RoamBuff *buff = &roam->buffs[b];
        if (buff->skill != NULL && Roam__PartySkill(buff->skill) && buff->until > now_cry) {
            NetWriter_U16(writer, (uint16_t)buff->skill->row);
            NetWriter_U8(writer, (uint8_t)(buff->level < 255 ? buff->level : 255));
            NetWriter_U16(writer,
                          (uint16_t)((buff->until - now_cry) / 100 < 65535 ? (buff->until - now_cry) / 100 : 65535));
            cries++;
        }
    }
    writer->data[count_cries] = (uint8_t)cries;
    NetClient_Send(MSG_ALLIES, writer); // (none: theirs go)
    free(writer);
}

// Someone's summons: each kept as a unit only to be seen, moving smoothly to where they say; the ones they no longer
// list are gone
static void Roam__ApplyAllies(SceneRoam *roam, const int from, NetReader *reader, const uint64_t now) {
    const uint64_t real  = SDL_GetTicks64();
    const int      count = NetReader_U8(reader);
    static bool    seen[MAX_UNITS];
    memset(seen, 0, sizeof(seen));
    for (int n = 0; n < count && !reader->failed; n++) {
        const int slot = NetReader_U8(reader);
        char      id[40];
        NetReader_String(reader, id, sizeof(id));
        const float x     = NetReader_F32(reader);
        const float y     = NetReader_F32(reader);
        const int   state = NetReader_U8(reader);
        const int   turn  = NetReader_U8(reader);
        if (reader->failed) {
            break;
        }
        Unit *unit = NULL;
        for (int i = 0; i < roam->unit_count && unit == NULL; i++) {
            Unit *u = &roam->units[i];
            if (u->remote_owner == from && u->remote_slot == slot && !u->vanished && strcmp(u->id, id) == 0) {
                unit = u;
            }
        }
        if (unit == NULL) {
            // New: in a spent one's place if there is one
            for (int i = 0; i < roam->unit_count && unit == NULL; i++) {
                Unit *u = &roam->units[i];
                if (u->remote_owner > 0 && u->vanished && Roam__InitUnit(u, "monsters", id, x, y)) {
                    unit = u;
                }
            }
            unit = unit != NULL ? unit : Roam__AddUnit(roam, "monsters", id, x, y);
            if (unit == NULL) {
                continue;
            }
            unit->remote_owner = from;
            unit->remote_slot  = slot;
            unit->max_hp       = 0; // (not to be fought or clicked)
            unit->hp           = 1;
            unit->npc          = false;
            unit->x            = x;
            unit->y            = y;
        }
        unit->net_from_x  = unit->x;
        unit->net_from_y  = unit->y;
        unit->net_to_x    = x;
        unit->net_to_y    = y;
        unit->net_at      = real;
        unit->remote_seen = real;
        unit->turn        = turn;
        if (state != (int)unit->state && state <= UNIT_DEAD) {
            Roam__SetState(unit, (UnitState)state, now);
        }
        seen[unit - roam->units] = true;
    }
    for (int i = 0; i < roam->unit_count && !reader->failed; i++) {
        Unit *u = &roam->units[i];
        if (u->remote_owner == from && !seen[i] && !u->vanished) {
            u->vanished = true; // (dismissed, run out, killed)
            Roam__SetState(u, UNIT_DEAD, now);
        }
    }
    const int states = NetReader_U8(reader);
    if (from >= 0 && from <= NET_MAX_PLAYERS && !reader->failed) {
        roam->other_state_count[from] = 0;
        for (int s = 0; s < states && !reader->failed; s++) {
            char name[32];
            NetReader_String(reader, name, sizeof(name));
            if (s < UNIT_STATES && !reader->failed) {
                snprintf(roam->other_states[from][roam->other_state_count[from]++], 32, "%s", name);
            }
        }
        // Their aura and warcries, for us when we're in their party and near them
        const int       row          = NetReader_U16(reader);
        const int       level        = NetReader_U8(reader);
        const int       cries        = NetReader_U8(reader);
        const SkillRow *aura         = row != 0xFFFF ? SkillData_ByRow(row) : NULL;
        roam->party_aura[from]       = aura != NULL && aura->kind == SKILL_KIND_AURA && !reader->failed ? aura : NULL;
        roam->party_aura_level[from] = level < 1 ? 1 : (level > 60 ? 60 : level);
        roam->party_cry_count[from]  = 0;
        for (int c = 0; c < cries && !reader->failed; c++) {
            const SkillRow *cry  = SkillData_ByRow(NetReader_U16(reader));
            const int       lvl  = NetReader_U8(reader);
            const uint64_t  left = (uint64_t)NetReader_U16(reader) * 100;
            if (cry != NULL && Roam__PartySkill(cry) && c < 4 && !reader->failed) {
                RoamBuff *buff = &roam->party_cries[from][roam->party_cry_count[from]++];
                buff->skill    = cry;
                buff->level    = lvl < 1 ? 1 : (lvl > 60 ? 60 : lvl);
                buff->until    = Roam__Now(roam) + (left < 600000 ? left : 600000);
            }
        }
    }
}

// What the owner says about the monsters: moved, hurt, attacking, dying
static void Roam__ApplyMonsters(SceneRoam *roam, NetReader *reader, const uint64_t now) {
    const uint64_t real  = SDL_GetTicks64();
    const int      count = NetReader_U16(reader);
    for (int n = 0; n < count && !reader->failed; n++) {
        const int       index    = NetReader_U16(reader);
        const float     x        = NetReader_F32(reader);
        const float     y        = NetReader_F32(reader);
        const int       hp       = NetReader_I32(reader);
        const int       max_hp   = NetReader_I32(reader);
        const UnitState state    = (UnitState)NetReader_U8(reader);
        const int       turn     = NetReader_U8(reader);
        const int       flags    = NetReader_U8(reader);
        const int       aura     = NetReader_U16(reader);
        const bool      vanished = (flags & 1) != 0;
        if (reader->failed || index >= roam->unit_count || roam->units[index].is_object || state > UNIT_OPERATE) {
            continue;
        }
        Unit      *unit   = &roam->units[index];
        const bool jumped = unit->net_at == 0 || Roam__Distance(unit->x, unit->y, x, y) > 8.0f;
        unit->net_from_x  = jumped ? x : unit->x;
        unit->net_from_y  = jumped ? y : unit->y;
        unit->net_to_x    = x;
        unit->net_to_y    = y;
        unit->net_at      = real;
        unit->turn        = turn;
        unit->max_hp      = max_hp;
        if (hp < unit->hp && hp > 0 && unit->state != UNIT_HIT) {
            Roam__UnitSound(roam, unit, MONSTER_SOUND_HIT);
        }
        unit->hp = hp;
        if ((int)state != unit->net_state) { // something new: show it from the start
            const bool was_alive = Roam__IsAlive(unit);
            unit->net_state      = (int)state;
            Roam__SetState(unit, state, now);
            if (state == UNIT_ATTACK) {
                Roam__UnitSound(roam, unit, MONSTER_SOUND_ATTACK);
            } else if (was_alive && !Roam__IsAlive(unit)) {
                Roam__UnitSound(roam, unit, MONSTER_SOUND_DEATH);
            }
        }
        unit->vanished = vanished;
        unit->burrowed = (flags & 2) != 0;
        if (aura == 0xFFFF) {
            unit->aura = NULL;
        } else if (unit->aura == NULL || unit->aura->row != aura) {
            unit->aura = SkillData_ByRow(aura); // (looked up when it changes, not 15 times a second)
        }
    }
}

// The items, monsters and blows other games send (through the server)
static void Roam__HandleGameMessage(SceneRoam *roam, const uint8_t type, NetReader *reader, const uint64_t now) {
    const int key = Roam__MapKey(roam);
    switch (type) {
    case MSG_ITEM_ADD: {
        const uint32_t id      = NetReader_U32(reader);
        const int      dropper = NetReader_U8(reader);
        const int      map     = NetReader_I32(reader);
        const float    x       = NetReader_F32(reader);
        const float    y       = NetReader_F32(reader);
        Item           item;
        NetItem_Read(reader, &item);
        if (!reader->failed && Items_Find(item.code) != NULL) {
            Roam__AddGroundItem(roam, &item, map, x, y, dropper != 0, id);
        }
        break;
    }
    case MSG_ITEM_GONE: {
        const uint32_t id     = NetReader_U32(reader);
        const int      picker = NetReader_U8(reader);
        for (int g = 0; g < roam->ground_count; g++) {
            if (roam->ground[g].net_id != id) {
                continue;
            }
            const Item item = roam->ground[g].item;
            Roam__RemoveGroundItem(roam, g);
            if (picker == NetClient_MyId()) { // ours
                if (!Roam__GiveItem(roam, &item)) {
                    if (!GameInventory_Holding(roam->inventory_ui)) {
                        GameInventory_Hold(roam->inventory_ui, &item); // filled up meanwhile: on the cursor
                    }
                }
                Sounds_Play(strcmp(item.code, "gld") == 0 ? "item_gold" : "item_pickup", SOUND_KIND_SFX);
                if (NetClient_ServerKeepsCharacters()) {
                    Roam__StoreCharacter(roam);
                }
            }
            break;
        }
        break;
    }
    case MSG_MONSTERS_FROM: {
        NetReader_U8(reader); // from (the map's owner)
        if (NetReader_I32(reader) == key && !Roam__RunsMonsters(roam)) {
            Roam__ApplyMonsters(roam, reader, now);
        }
        break;
    }
    case MSG_TRADE_FROM:
        Roam__TradeMessage(roam, reader);
        break;
    case MSG_HOSTILE_FROM:
        Roam__HostileMessage(roam, reader);
        break;
    case MSG_CORPSE_FROM:
        Roam__CorpseMessage(roam, reader, now);
        break;
    case MSG_LOOT_FROM:
        Roam__LootMessage(roam, reader);
        break;
    case MSG_PORTAL_FROM:
        Roam__ReceivePortal(roam, reader);
        break;
    case MSG_ALLIES_FROM: {
        const int from = NetReader_U8(reader);
        const int map  = NetReader_I32(reader);
        if (map == key) {
            Roam__ApplyAllies(roam, from, reader, now);
        } else {
            for (int i = 0; i < roam->unit_count; i++) { // (they've gone elsewhere: so have their summons)
                if (roam->units[i].remote_owner == from) {
                    roam->units[i].vanished = true;
                }
            }
        }
        break;
    }
    case MSG_CAST_FROM: {
        NetReader_U8(reader); // who (seen through their missiles)
        const int   map   = NetReader_I32(reader);
        const int   row   = NetReader_U16(reader);
        const int   level = NetReader_U8(reader);
        const float x     = NetReader_F32(reader);
        const float y     = NetReader_F32(reader);
        const float tx    = NetReader_F32(reader);
        const float ty    = NetReader_F32(reader);
        if (map == key && !reader->failed && row == 0xFFFE) {
            Roam__ShowShot(roam, reader, x, y, tx, ty); // a monster's or a hireling's missile
        } else if (map == key && !reader->failed) {
            Roam__ShowCast(roam, row, level, x, y, tx, ty);
        }
        break;
    }
    case MSG_HIT_MONSTER_FROM: {
        const int from   = NetReader_U8(reader);
        const int map    = NetReader_I32(reader);
        const int index  = NetReader_U16(reader);
        const int damage = NetReader_I32(reader);
        if (map != key || !Roam__RunsMonsters(roam) || index >= roam->unit_count || reader->failed) {
            break;
        }
        Unit      *unit      = &roam->units[index];
        const bool was_alive = Roam__IsAlive(unit);
        if (!was_alive || unit->is_object) {
            break;
        }
        unit->provoked   = true;
        unit->net_target = from;
        Roam__Damage(roam, unit, damage, now);
        if (!Roam__IsAlive(unit)) { // theirs: they (and their party) get the experience
            Roam__SendKilled(roam, index, from);
        }
        break;
    }
    case MSG_MONSTER_KILLED_FROM: {
        NetReader_U8(reader); // from (the owner)
        const int map   = NetReader_I32(reader);
        const int index = NetReader_U16(reader);
        NetReader_U8(reader); // the killer
        const int sharing = NetReader_U8(reader);
        if (map == key && index < roam->unit_count && !reader->failed && sharing > 0) {
            // A party shares it: the game's bonus of 35% for each member after the first, split evenly
            const int whole = roam->units[index].experience * (100 + (35 * (sharing - 1))) / 100;
            Roam__GainExperienceAmount(roam, whole / sharing, roam->units[index].level);
        }
        break;
    }
    case MSG_OBJECT_USED: {
        const int from  = NetReader_U8(reader);
        const int map   = NetReader_I32(reader);
        const int index = NetReader_U16(reader);
        if (reader->failed) {
            break;
        }
        if (roam->used_count < NET_USED_OBJECTS_MAX) {
            roam->used_maps[roam->used_count]      = map;
            roam->used_objects[roam->used_count++] = index;
        }
        if (map != key || index >= roam->unit_count || !roam->units[index].is_object) {
            break; // somewhere else: when we get there
        }
        Unit *unit = &roam->units[index];
        if (from == 0) {
            unit->used = true; // open from before we came
            Roam__FinishOperate(roam, unit, now);
        } else {
            roam->use_is_ours = from == NetClient_MyId(); // ours: its treasure is ours to drop
            Roam__Operate(roam, unit, now);
            roam->use_is_ours = false;
        }
        break;
    }
    case MSG_AFFLICTED: {
        NetReader_U8(reader); // (from)
        const int target = NetReader_U8(reader);
        const int row    = NetReader_U16(reader);
        const int level  = NetReader_U8(reader);
        const int ms     = NetReader_U16(reader);
        Unit     *player = &roam->player;
        if (reader->failed || target != NetClient_MyId() || !Roam__IsAlive(player) || now < roam->safe_until) {
            break;
        }
        // Only what monsters do (another game could send anything): a chill of a few seconds, or one of the curses
        // monsters cast, at a monster's level, for as long as theirs last
        const SkillRow *curse = row != 0xFFFF ? SkillData_ByRow(row) : NULL;
        if (row == 0xFFFF) {
            Roam__ChillMe(roam, ms < 3000 ? ms : 3000, Roam__Now(roam));
        } else if (curse != NULL && (strcmp(curse->id, "Amplify Damage") == 0 || strcmp(curse->id, "Weaken") == 0 ||
                                     strcmp(curse->id, "Decrepify") == 0)) {
            player->curse       = curse;
            player->curse_level = level < 1 ? 1 : (level > 20 ? 20 : level);
            player->curse_until = Roam__Now(roam) + (uint64_t)(ms < 30000 ? ms : 30000);
        }
        break;
    }
    case MSG_HIT_YOU: {
        const int  from          = NetReader_U8(reader);
        const int  target        = NetReader_U8(reader);
        const bool pvp           = NetReader_U8(reader) != 0;
        const int  attack_rating = NetReader_I32(reader);
        const int  level         = NetReader_I32(reader);
        int        damage        = 0;
        bool       cold          = false;
        for (int e = 0; e < ELEMENT_COUNT; e++) { // physical as it is; the rest against our resistances
            const int part  = NetReader_I32(reader);
            damage         += e == ELEMENT_PHYSICAL ? part : Roam__ElementTaken(roam, (Element)e, part);
            cold            = cold || (e == ELEMENT_COLD && part > 0);
        }
        Unit *player = &roam->player;
        if (reader->failed || target != NetClient_MyId() || !Roam__IsAlive(player) || now < roam->safe_until ||
            (pvp && !Roam__HostileTo(roam, from))) {
            break; // (another player's blow only when one of us went hostile, out of town)
        }
        if (Roam__Roll(1, 100) > Character_HitChance(attack_rating, level, player->defense, player->level)) {
            break; // missed
        }
        if (cold) {
            Roam__ChillMe(roam, 2000, now); // cold slows (a monster's blow, a cold spell)
        }
        Roam__Damage(roam, player, damage > 0 ? damage : 1, now);
        if (pvp && !Roam__IsAlive(player)) {
            const NetPlayer *killer = NetClient_Player(from);
            char             text[96];
            snprintf(text, sizeof(text), "%s killed you.", killer != NULL ? killer->name : "Someone");
            Roam__ShowMessage(roam, text);
        }
        break;
    }
    default:
        break;
    }
}

// Every frame in a multiplayer game: where we are (15 times a second), what's said, whether we're still connected.
// Returns true when the game ended (the scene changed).
// What we wear, to the others when it changes
static void Roam__SendLooks(SceneRoam *roam) {
    char text[CHARACTER_LOOKS_TEXT];
    Character_LooksToText(&roam->player.looks, text, sizeof(text));
    if (!roam->player.has_looks || strcmp(text, roam->sent_looks) == 0) {
        return;
    }
    NetWriter *writer = malloc(sizeof(NetWriter));
    if (writer == NULL) {
        return;
    }
    snprintf(roam->sent_looks, sizeof(roam->sent_looks), "%s", text);
    NetWriter_Begin(writer);
    NetWriter_String(writer, text);
    NetClient_Send(MSG_LOOKS, writer);
    free(writer);
}

static bool Roam__ChatCommand(SceneRoam *roam, const char *text);

static bool Roam__UpdateOnline(SceneRoam *roam, const uint64_t now) {
    if (NetClient_Stage() != NETC_IN_GAME) {
        // Lost the game (or the host closed it): keep the character as it is, then the Multiplayer screen says why
        if (!roam->character->dead && !NetClient_ServerKeepsCharacters()) {
            Roam__StoreCharacter(roam);
        }
        Scene_Set(SCENE_REF(MultiplayerMenu));
        return true;
    }
    const uint64_t real = SDL_GetTicks64();
    if (real - roam->last_sent >= 1000 / NET_STATE_HZ) {
        roam->last_sent = real;
        Roam__ForgetGonePortals(roam); // (theirs who've left)
        Roam__KeepCorpseSeen(roam, real);
        Roam__LootNearCorpse(roam, real);
        NetClient_SendState(Roam__MapKey(roam), roam->player.x, roam->player.y, roam->player.turn,
                            Roam__Mode(roam, &roam->player),
                            roam->player.max_hp > 0 ? roam->player.hp * 100 / roam->player.max_hp : 100,
                            roam->character != NULL ? roam->character->level : 1);
    }
    char line[NET_CHAT_MAX + 24];
    while (NetClient_TakeChat(line, sizeof(line))) {
        Roam__AddChat(roam, line);
    }
    // Arriving somewhere: everyone comes in at the same spot, so step aside by our player id (a ring around it)
    if (roam->spread_place != roam->place) {
        roam->spread_place = roam->place;
        const int   id     = NetClient_MyId();
        const float angle  = (float)id * 2.39996f; // the golden angle: ids spread evenly round the circle
        const float far    = 2.0f + (float)(id % 4);
        const float x      = roam->player.x + (cosf(angle) * far);
        const float y      = roam->player.y + (sinf(angle) * far);
        if (id > 1 && Map_IsWalkable(roam->map, (int)floorf(x), (int)floorf(y))) {
            roam->player.x = x;
            roam->player.y = y;
        }
    }
    Roam__UpdateOthers(roam, now);
    uint8_t   type;
    NetReader reader;
    while (NetClient_TakeGameMessage(&type, &reader)) {
        Roam__HandleGameMessage(roam, type, &reader, now);
    }
    // Running this map's monsters with others here: tell them
    if (roam->other_count > 0 && Roam__RunsMonsters(roam) && real - roam->last_monsters >= 1000 / NET_STATE_HZ) {
        roam->last_monsters = real;
        Roam__SendMonsters(roam);
    }
    if (roam->other_count > 0 && roam->game && real - roam->last_allies >= 200) {
        roam->last_allies = real;
        Roam__SendAllies(roam);
    }
    // Chat: Enter opens the line, Enter sends it, Esc closes it
    if (TextBox_IsFocused(roam->chat_box)) {
        TextBox_Update(roam->chat_box);
        if (InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN) || InputManager_ConsumeKeyPress(SDL_SCANCODE_KP_ENTER)) {
            if (!Roam__ChatCommand(roam, TextBox_GetText(roam->chat_box))) {
                NetClient_SendChat(TextBox_GetText(roam->chat_box));
            }
            TextBox_Clear(roam->chat_box);
            TextBox_Blur(roam->chat_box);
        } else if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
            TextBox_Clear(roam->chat_box);
            TextBox_Blur(roam->chat_box);
        }
        InputManager_ClearPresses(); // typing isn't playing
    }
    return false;
}

// A command typed on the chat line: /players N (Single Player). True when it was one (it isn't sent as chat)
static bool Roam__ChatCommand(SceneRoam *roam, const char *text) {
    if (strncmp(text, "/players", 8) != 0 || (text[8] != ' ' && text[8] != '\0')) {
        return false;
    }
    if (roam->online) {
        Roam__ShowMessage(roam, "/players is for Single Player games.");
        return true;
    }
    const int count = atoi(text + 8);
    if (count < 1 || count > 8) {
        char now[48];
        snprintf(now, sizeof(now), "Players: %d (/players 1-8 changes it)", init_players);
        Roam__ShowMessage(roam, now);
        return true;
    }
    init_players = count;
    Treasure_SetPlayers(count);
    char said[40];
    snprintf(said, sizeof(said), "Players set to %d", count);
    Roam__ShowMessage(roam, said);
    return true;
}

// The game's clock: real time, sped up by the multiplayer game's speed (Diablo's Fast, Faster, Fastest)
static uint64_t Roam__Clock(SceneRoam *roam) {
    const uint64_t real = SDL_GetTicks64();
    if (!roam->online) {
        return real;
    }
    if (roam->clock_real == 0) {
        roam->clock      = real;
        roam->clock_real = real;
    }
    roam->clock += (uint64_t)((float)(real - roam->clock_real) * NetSettings_SpeedScale(NetClient_Settings()->speed));
    roam->clock_real = real;
    return roam->clock;
}

// Leaving from the game menu: Single Player saves and goes back to the character list; Free Roam to where it came from
static void Roam__Leave(SceneRoam *roam) {
    if (roam->game) {
        if (!roam->character->dead) {
            Roam__StoreCharacter(roam);
        }
        Roam__ExitGame(roam);
    } else {
        Scene_Set(roam->borrowed ? SCENE_REF(MapEditor) : SCENE_REF(MainMenu));
    }
}

enum { ROAM_ASK_LEAVE, ROAM_ASK_NEW_SEED, ROAM_ASK_ENTER };

// Free Roam and Testing Grounds ask before leaving (Back, or Leave in the menu)
static void Roam__AskLeave(SceneRoam *roam) {
    roam->question = ROAM_ASK_LEAVE;
    if (roam->borrowed) {
        ConfirmDialog_Open(roam->confirm, "Back to the Map Editor", "Stop play-testing and go back to the editor?",
                           "Yes", "No", NULL);
    } else {
        ConfirmDialog_Open(roam->confirm, "Main Menu", "Are you sure you wish to return to the main menu?", "Yes", "No",
                           NULL);
    }
}

// Gold out of the inventory onto the ground at your feet
static void Roam__DropGold(SceneRoam *roam, int amount) {
    amount = amount > roam->character->gold ? roam->character->gold : amount;
    if (amount <= 0) {
        return;
    }
    if (roam->ground_count >= MAX_GROUND_ITEMS) {
        Roam__ShowMessage(roam, "There are too many items on the ground already.");
        return;
    }
    Item gold              = Inventory_NewItem("gld");
    gold.quantity          = amount;
    roam->character->gold -= amount;
    float x;
    float y;
    Roam__FreeSpot(roam, &x, &y);
    Roam__PutOnGround(roam, &gold, x, y);
    if (roam->online && NetClient_ServerKeepsCharacters()) {
        Roam__StoreCharacter(roam);
    }
}

static void Roam__AmountChosen(SceneRoam *roam, const int amount) {
    switch (AmountDialog_Tag(roam->amount)) {
    case AMOUNT_FOR_DROP:
        Roam__DropGold(roam, amount);
        break;
    case AMOUNT_FOR_TRADE:
        if (roam->trade != NULL && roam->trade_state == TRADE_STATE_OPEN) {
            roam->trade->my_gold = amount < roam->character->gold ? amount : roam->character->gold;
            Roam__TradeChanged(roam);
        }
        break;
    case AMOUNT_FOR_STASH_PUT: {
        Character *c     = roam->character;
        const int  room  = Stash_GoldLimit(c) - c->stash_gold;
        const int  move  = amount < c->gold ? (amount < room ? amount : room) : (c->gold < room ? c->gold : room);
        c->gold         -= move > 0 ? move : 0;
        c->stash_gold   += move > 0 ? move : 0;
        Sounds_Play("item_gold", SOUND_KIND_SFX);
        break;
    }
    case AMOUNT_FOR_STASH_TAKE: {
        Character *c     = roam->character;
        const int  room  = Roam__GoldLimit(roam) - c->gold;
        int        move  = amount < c->stash_gold ? amount : c->stash_gold;
        move             = move < room ? move : room;
        c->gold         += move > 0 ? move : 0;
        c->stash_gold   -= move > 0 ? move : 0;
        Sounds_Play("item_gold", SOUND_KIND_SFX);
        break;
    }
    default:
        break;
    }
}

// The game menu and Options over the world, which waits meanwhile. Returns true while either is open.
static bool Roam__UpdateMenu(SceneRoam *roam) {
    if (AmountDialog_IsOpen(roam->amount)) {
        if (AmountDialog_Update(roam->amount) == AMOUNT_OK) {
            Roam__AmountChosen(roam, AmountDialog_Value(roam->amount));
        }
        return true;
    }
    if (ConfirmDialog_IsOpen(roam->confirm)) {
        if (ConfirmDialog_Update(roam->confirm) == CONFIRM_FIRST) {
            switch (roam->question) {
            case ROAM_ASK_NEW_SEED:
                Roam__NewWorld(roam, 0); // the same area in a new random world
                break;
            case ROAM_ASK_ENTER:
                Roam__Travel(roam, roam->question_level);
                break;
            default:
                roam->menu_open = false;
                Roam__Leave(roam);
                break;
            }
        }
        return true;
    }
    if (roam->options_panel != NULL) {
        if (!OptionsPanel_Update(roam->options_panel)) {
            OptionsPanel_Destroy(&roam->options_panel);
        }
        return true;
    }
    if (!roam->menu_open) {
        return false;
    }
    InputManager_ClearPresses();
    if (Button_Update(roam->menu_options_button)) {
        roam->options_panel = OptionsPanel_Create();
    } else if (Button_Update(roam->menu_leave_button)) {
        if (roam->game) {
            roam->menu_open = false;
            Roam__Leave(roam);
        } else {
            Roam__AskLeave(roam);
        }
    } else if (Button_Update(roam->menu_return_button) || InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        roam->menu_open = false;
    }
    return true;
}

static void Roam__Update(SceneRoam *roam) {
    const uint64_t now = Roam__Clock(roam);
    Roam__AmbientEvents(roam, now);
    if (roam->game && roam->character != NULL) {
        Character_Looks(roam->character, &roam->player.looks); // drawn in what's worn
        roam->player.has_looks = true;
        for (int b = 0; b < roam->buff_count; b++) { // ... or in the Druid's shape
            if (roam->buffs[b].skill->do_func == 116) {
                roam->player.looks.shape = strcmp(roam->buffs[b].skill->id, "Wearbear") == 0 ? 'b' : 'w';
            }
        }
    }
    if (roam->online) {
        // The other players go on whatever we're doing (menus included)
        if (Roam__UpdateOnline(roam, now)) {
            return;
        }
        Roam__SendLooks(roam);
        if (!TextBox_IsFocused(roam->chat_box) && roam->options_panel == NULL && !roam->menu_open &&
            Controls_Pressed(ACTION_CHAT)) {
            TextBox_Focus(roam->chat_box);
            InputManager_ClearPresses();
        }
    } else if (roam->game && roam->chat_box != NULL) {
        // Single Player: Enter opens the command line (/players), Enter runs it, Esc closes it
        if (TextBox_IsFocused(roam->chat_box)) {
            TextBox_Update(roam->chat_box);
            if (InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN) ||
                InputManager_ConsumeKeyPress(SDL_SCANCODE_KP_ENTER)) {
                const char *text = TextBox_GetText(roam->chat_box);
                if (text[0] != '\0' && !Roam__ChatCommand(roam, text)) {
                    Roam__ShowMessage(roam, "Commands: /players 1-8");
                }
                TextBox_Clear(roam->chat_box);
                TextBox_Blur(roam->chat_box);
            } else if (InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
                TextBox_Clear(roam->chat_box);
                TextBox_Blur(roam->chat_box);
            }
            InputManager_ClearPresses(); // typing isn't playing
        } else if (roam->options_panel == NULL && !roam->menu_open && Controls_Pressed(ACTION_CHAT)) {
            TextBox_Focus(roam->chat_box);
            InputManager_ClearPresses();
        }
    }
    if (Roam__UpdateMenu(roam)) {
        roam->last_tick = now; // the world waits
        return;
    }
    const float seconds = fminf((float)(now - roam->last_tick) / 1000.0f, 0.1f);
    Roam__MeasureSpeed(roam, now);
    roam->last_tick     = now;
    const SDL_Rect view = {0, 0, VIEW_W, roam->game ? 600 : VIEW_H}; // Single Player: under the control panel

    // Controls: the class's own (or all classes'), the actions of this mode; nothing while typing a seed
    Controls_SetProfile(Roam__Profile(roam));
    Controls_SetContext(roam->game ? CONTROL_CONTEXT_GAME : CONTROL_CONTEXT_DEBUG);
    Controls_AllowAnywhere(ACTION_COLLISION, Settings_GetDebug()->collision_in_game); // Options › Gameplay › Debug
    Controls_AllowAnywhere(ACTION_SPRINT, Roam__DebugTools(roam));
    if (roam->game && roam->sprinting && !Roam__DebugTools(roam)) {
        roam->sprinting = false; // turned off in Options while playing
    }
    if (TextBox_IsFocused(roam->seed_box)) {
        InputManager_ClearPresses();
    }
    Controls_Update();
    const GameplaySettings *gameplay = Roam__Gameplay(roam);
    roam->player.even_speed          = gameplay->even_screen_speed;

    int  mouse_x;
    int  mouse_y;
    bool left  = Controls_Held(ACTION_MOVE_ATTACK);
    bool right = Controls_Held(ACTION_RIGHT_SKILL);
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    const bool over_panel =
        (roam->panel != PANEL_NONE && mouse_x >= PANEL_X) ||
        (roam->game && roam->char_panel && mouse_x < CHAR_PANEL_W && mouse_y < CHAR_PANEL_H) ||
        (roam->game && (roam->inventory_panel || roam->skill_panel) && GameInventory_IsOver(mouse_x, mouse_y));
    if (roam->game && Roam__UpdateGamePanels(roam, mouse_x, mouse_y)) {
        left  = false; // the panels' click, not the world's
        right = false;
    }
    const bool on_ctrl   = roam->game && Roam__OverControlPanel(roam, mouse_x, mouse_y);
    const bool over_view = (roam->game ? mouse_y < 600 && !on_ctrl : mouse_y < VIEW_H) && !over_panel;
    if (roam->game && (Roam__UpdateControlPanel(roam, mouse_x, mouse_y) || (on_ctrl && left))) {
        left = false; // a click on the panel isn't a click in the world
    }

    if (roam->game && Roam__UpdateGame(roam, now, mouse_x, mouse_y, left && !roam->prev_left)) {
        return;
    }

    if (over_view && (Controls_Pressed(ACTION_ZOOM_IN) || Controls_Pressed(ACTION_ZOOM_OUT))) {
        roam->camera.zoom = Controls_Pressed(ACTION_ZOOM_IN) ? fminf(roam->camera.zoom * 2.0f, 2.0f)
                                                             : fmaxf(roam->camera.zoom / 2.0f, 0.25f);
    }
    InputManager_ConsumeMouseWheel(); // the wheel is bound through the controls
    if (Controls_Pressed(ACTION_RUN_TOGGLE)) {
        roam->running = !roam->running;
        Roam__UpdateButtons(roam);
    }
    if (Controls_Pressed(ACTION_COMBAT)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (Controls_Pressed(ACTION_COLLISION)) {
        roam->draw_flags ^= MAP_DRAW_BLOCKED;
    }
    if (Controls_Pressed(ACTION_LIGHT)) {
        light_level = (light_level + 1) % LIGHT_LEVEL_COUNT;
    }
    if (Controls_Pressed(ACTION_PREVIEW)) {
        roam->preview = !roam->preview;
        Roam__UpdateButtons(roam);
    }
    if (roam->free_roam && Controls_Pressed(ACTION_AUTOMAP)) {
        roam->show_automap = !roam->show_automap; // the automap, like the game's
    }
    if (Controls_Pressed(ACTION_SCREENSHOT)) {
        roam->screenshot = true;
    }
    Roam__ScreenKeys(roam); // the message log, help, clearing messages, portraits
    if (Controls_Pressed(ACTION_CLEAR_SCREEN)) {
        roam->panel           = PANEL_NONE;
        roam->char_panel      = false;
        roam->inventory_panel = false;
        roam->skill_panel     = false;
        roam->show_automap    = false;
        Roam__UpdateButtons(roam);
    }
    if (!roam->game && roam->free_roam && Controls_Pressed(ACTION_WAYPOINT)) {
        Roam__TogglePanel(roam, PANEL_PLACES);
    }
    if (roam->free_roam && Controls_Pressed(ACTION_NEW_WORLD)) {
        if (Settings_GetDebug()->confirm_new_seed) {
            roam->question = ROAM_ASK_NEW_SEED;
            ConfirmDialog_Open(roam->confirm, "New World",
                               "Do you wish to generate a new seed? This area and every random one are built again.",
                               "Yes", "No", NULL);
        } else {
            Roam__NewWorld(roam, 0); // the same area in a new random world
        }
        return;
    }

    // Modern / Combined: the movement keys (and the left stick) walk
    roam->key_dx = 0.0f;
    roam->key_dy = 0.0f;
    if (gameplay->movement != MOVEMENT_CLASSIC) {
        // On screen, up is towards lower x and y in sub-tiles, right towards higher x and lower y
        // As far as each is pushed: keys all the way, a stick (or anything analog bound there) part way
        const float sx     = Controls_Amount(ACTION_MOVE_RIGHT) - Controls_Amount(ACTION_MOVE_LEFT);
        const float sy     = Controls_Amount(ACTION_MOVE_DOWN) - Controls_Amount(ACTION_MOVE_UP);
        const float pushed = fminf(sqrtf((sx * sx) + (sy * sy)), 1.0f);
        // Screen directions to sub-tiles (the screen is squashed 2:1, so a step right is half a step in y)
        const float dx     = sx + (sy * 2.0f);
        const float dy     = (sy * 2.0f) - sx;
        const float length = sqrtf((dx * dx) + (dy * dy));
        if (length > 0.01f && pushed > 0.05f) {
            // A stick pushed part way walks slower (down to a third of the speed)
            const float speed = 0.35f + (0.65f * pushed);
            roam->key_dx      = speed * dx / length;
            roam->key_dy      = speed * dy / length;
        }
    }

    float world_x;
    float world_y;
    float sub_x;
    float sub_y;
    MapView_ScreenToWorld(&roam->camera, &view, mouse_x, mouse_y, &world_x, &world_y);
    MapView_WorldToSubtile(world_x, world_y, &sub_x, &sub_y);
    roam->hovered = over_view ? Roam__UnitAt(roam, sub_x, sub_y) : -1;
    // An item on the ground under the mouse (its picture, or its name while names are shown) comes first
    roam->hover_item = roam->game && over_view ? Roam__GroundItemAt(roam, mouse_x, mouse_y) : -1;
    if (roam->hover_item >= 0) {
        roam->hovered = -1;
    }
    // PvP: another player under the mouse (when no monster or item is)
    roam->hover_other = over_view && roam->hover_item < 0 && roam->hovered < 0 && Roam__AnyHostile(roam)
                            ? Roam__OtherAt(roam, sub_x, sub_y)
                            : -1;
    if (roam->hover_other >= 0 && !Roam__HostileTo(roam, roam->other_ids[roam->hover_other])) {
        roam->hover_other = -1; // (only those we're hostile with)
    }
    // An entrance under the mouse (cave mouth, tomb door, stairs): its name is shown, like the game's
    roam->hover_exit = -1;
    for (int e = 0; over_view && roam->hovered < 0 && e < roam->map->exit_count; e++) {
        const MapExit *exit = &roam->map->exits[e];
        if (exit->entrance && sub_x >= (float)exit->x0 - 4.0f && sub_x <= (float)exit->x1 + 4.0f &&
            sub_y >= (float)exit->y0 - 4.0f && sub_y <= (float)exit->y1 + 4.0f) {
            roam->hover_exit = e;
            break;
        }
    }
    roam->hover_valid = over_view;
    roam->hover_x     = floorf(sub_x) + 0.5f;
    roam->hover_y     = floorf(sub_y) + 0.5f;

    // Modern / Combined: the attack key hits what's under the mouse, or the nearest monster in reach
    if (gameplay->movement != MOVEMENT_CLASSIC && Controls_Pressed(ACTION_ATTACK) && Roam__IsAlive(&roam->player)) {
        Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
    }
    // Modern: the mouse attacks and opens things within reach, but doesn't walk
    if (gameplay->movement == MOVEMENT_MODERN && over_view && left && !roam->prev_left &&
        Roam__IsAlive(&roam->player)) {
        if (roam->hover_other >= 0) {
            const Unit *other = &roam->others[roam->hover_other];
            if (Roam__Distance(roam->player.x, roam->player.y, other->x, other->y) <= ATTACK_RANGE) {
                roam->attack_other    = roam->other_ids[roam->hover_other]; // one swing at them
                roam->attack_in_place = true;
            }
        } else if (roam->hover_item >= 0) {
            roam->item_target = roam->hover_item;
            if (!Roam__TryPickUp(roam)) {
                roam->item_target = -1; // out of reach
                Roam__ShowMessage(roam, "Too far away: walk up to it to pick it up.");
            }
        } else if (roam->hovered >= 0 && (Roam__CanUse(&roam->units[roam->hovered]) ||
                                          (roam->game && Roam__CanTalk(roam, &roam->units[roam->hovered])))) {
            roam->use_target = roam->hovered;
            if (!Roam__TryUse(roam, now)) {
                roam->use_target = -1; // out of reach
            }
        } else {
            Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
        }
    }
    // Stand still (Shift): clicks attack where you stand instead of walking
    const bool stand_still = Controls_Held(ACTION_STAND_STILL);

    // Single Player: the right skill (held down: again and again), and the left one at a monster or with Shift
    bool used_skill = false;
    if (roam->game && over_view && Roam__IsAlive(&roam->player) && roam->player.state != UNIT_CAST &&
        now >= roam->next_cast) {
        const bool at_monster = roam->hovered >= 0 && Roam__IsCombatant(&roam->units[roam->hovered]) &&
                                !Roam__IsPeaceful(roam, &roam->units[roam->hovered]) &&
                                !roam->units[roam->hovered].ally;
        if (right && !roam->ui_owns_press) {
            used_skill =
                Roam__UseSkill(roam, roam->character->right_skill, sub_x, sub_y, at_monster ? roam->hovered : -1, now);
        } else if (left && (at_monster || stand_still) && roam->hover_item < 0 && roam->hover_other < 0 &&
                   (roam->character->left_skill != SKILL_ATTACK || Roam__WeaponMissile(roam) != NULL)) {
            used_skill =
                Roam__UseSkill(roam, roam->character->left_skill, sub_x, sub_y, at_monster ? roam->hovered : -1, now);
        }
        if (used_skill) {
            roam->next_cast = now + SKILL_HOLD_MS;
        }
    }
    if (used_skill) {
        left = false; // the click was the skill's
    }
    if (gameplay->movement != MOVEMENT_MODERN && stand_still && over_view && left && !roam->prev_left &&
        Roam__IsAlive(&roam->player)) {
        Roam__AttackInPlace(roam, roam->hovered, sub_x, sub_y, now);
    }

    // Left: attack what's under the mouse (combat on), otherwise walk there (hold to keep following the mouse)
    if (gameplay->movement != MOVEMENT_MODERN && !stand_still && over_view && left && Roam__IsAlive(&roam->player)) {
        if (!roam->prev_left && roam->hover_other >= 0) {
            roam->attack_other    = roam->other_ids[roam->hover_other]; // PvP: go for them
            roam->attack_in_place = false;
            roam->attack_target   = -1;
            roam->use_target      = -1;
            roam->item_target     = -1;
            Roam__ClearPath(&roam->player);
        } else if (!roam->prev_left && roam->hover_item >= 0) {
            roam->attack_other  = 0;
            roam->item_target   = roam->hover_item; // walk over and pick it up
            roam->use_target    = -1;
            roam->attack_target = -1;
            Roam__ClearPath(&roam->player);
        } else if (!roam->prev_left && roam->hovered >= 0 &&
                   (Roam__CanUse(&roam->units[roam->hovered]) ||
                    (roam->game && Roam__CanTalk(roam, &roam->units[roam->hovered])))) {
            roam->use_target    = roam->hovered; // walk over and open it
            roam->attack_target = -1;
            roam->item_target   = -1;
            Roam__ClearPath(&roam->player);
        } else if (!roam->prev_left && Roam__Combat(roam) && roam->hovered >= 0 &&
                   Roam__IsCombatant(&roam->units[roam->hovered]) &&
                   !Roam__IsPeaceful(roam, &roam->units[roam->hovered])) {
            roam->attack_target   = roam->hovered;
            roam->attack_in_place = false;
            roam->use_target      = -1;
            roam->item_target     = -1;
            roam->attack_other    = 0;
        } else if ((roam->use_target >= 0 || roam->item_target >= 0 || roam->attack_other > 0) && roam->prev_left) {
            // holding the button after clicking an object, item or player: keep going to it
        } else if ((roam->attack_target < 0 && roam->attack_other == 0) || roam->prev_left == false) {
            roam->use_target      = -1;
            roam->attack_target   = -1;
            roam->item_target     = -1;
            roam->attack_other    = 0;
            roam->player.target_x = sub_x;
            roam->player.target_y = sub_y;
            roam->moving          = true;
            if (!roam->prev_left) {
                Roam__ClearPath(&roam->player); // a new click: plan from scratch
            }
        }
    }
    // Right: drop the selected monster; Shift+Right removes the nearest one
    if (over_view && right && !roam->prev_right && !roam->game) {
        if ((InputManager_GetMods() & KMOD_SHIFT) != 0) {
            if (roam->hovered >= 0) {
                roam->units[roam->hovered] = roam->units[--roam->unit_count];
                roam->attack_target        = -1;
                roam->use_target           = -1;
                roam->hovered              = -1;
                Roam__UpdateBlocking(roam);
            }
        } else if (roam->panel != PANEL_PLACES) {
            CatalogCharacterInfo info;
            if (CatalogCharacters_GetInfo(MapSession_GetBestiary(), roam->selected, &info)) {
                Unit *unit = Roam__AddUnit(roam, info.spec->base, info.id, roam->hover_x, roam->hover_y);
                if (unit != NULL) {
                    unit->dropped = true;
                    Roam__UpdateBlocking(roam);
                }
            }
        }
    }
    roam->prev_left  = left;
    roam->prev_right = right;

    Roam__UpdateSkillBonuses(roam, now);
    Roam__UpdatePlayer(roam, seconds, now);
    Roam__UpdateUnits(roam, seconds, now);
    Roam__UpdateMissiles(roam, seconds, now);
    Roam__SkillPulse(roam, now);
    if (roam->game) {
        Roam__UpdateGroundItems(roam, SDL_GetTicks64()); // (real time, like their drawing)
    }

    // Which area of a joined-up map you're in
    if (roam->free_roam && roam->map->area_count > 0) {
        const MapArea *area = Map_AreaAt(roam->map, roam->player.x, roam->player.y);
        if (area != NULL) {
            Roam__EnteredArea(roam, area->level_id, true);
        }
    }

    // Walking off the edge into the next area, or onto an entrance
    if (roam->free_roam && Roam__IsAlive(&roam->player)) {
        const MapExit *exit = Map_ExitAt(roam->map, roam->player.x, roam->player.y);
        // Entrances only take you in when you were heading for them, not when you walk across one
        const bool meant = exit != NULL && (!exit->entrance || (roam->player.target_x >= (float)exit->x0 - 3.0f &&
                                                                roam->player.target_x <= (float)exit->x1 + 3.0f &&
                                                                roam->player.target_y >= (float)exit->y0 - 3.0f &&
                                                                roam->player.target_y <= (float)exit->y1 + 3.0f));
        if (exit == NULL) {
            roam->exits_armed = true;
        } else if (roam->exits_armed && meant) {
            roam->moving = false;
            Roam__ClearPath(&roam->player);
            if (exit->entrance && Roam__Gameplay(roam)->confirm_entrances) {
                // Options › Gameplay: asked first (stepping off and back on asks again)
                roam->exits_armed    = false;
                roam->question       = ROAM_ASK_ENTER;
                roam->question_level = exit->target_level;
                char name[96];
                char text[160];
                World_GetWarpText(exit->target_level, name, sizeof(name));
                snprintf(text, sizeof(text), "%s?", name[0] != '\0' ? name : "Go in");
                ConfirmDialog_Open(roam->confirm, "Entrance", text, "Go in", "Stay", NULL);
                return;
            }
            Roam__Travel(roam, exit->target_level);
            return;
        }
    }

    // Camera follows the player (keeping the body, not the feet, in the middle)
    MapView_SubtileToWorld(roam->player.x, roam->player.y, &roam->camera.x, &roam->camera.y);
    roam->camera.y -= 40.0f;

    // Side panel
    if (roam->panel == PANEL_MONSTERS &&
        CatalogCharacters_UpdateCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W)) {
        Roam__RefreshList(roam);
    }
    if (roam->panel == PANEL_MONSTERS && Button_Update(roam->preview_button)) {
        roam->preview = !roam->preview;
        Roam__UpdateButtons(roam);
    }
    if (roam->panel == PANEL_PLACES && roam->seed_box != NULL) {
        TextBox_Update(roam->seed_box);
        const bool enter = TextBox_IsFocused(roam->seed_box) && InputManager_ConsumeKeyPress(SDL_SCANCODE_RETURN);
        if (Button_Update(roam->seed_use_button) || enter) {
            const unsigned long typed = strtoul(TextBox_GetText(roam->seed_box), NULL, 10);
            TextBox_Blur(roam->seed_box);
            if (typed > 0 && typed <= 0xFFFFFFFFul) {
                Roam__NewWorld(roam, (uint32_t)typed);
                return;
            }
            Roam__ShowMessage(roam, "A seed is a whole number, 1 or more.");
        }
        if (Button_Update(roam->seed_random_button)) {
            TextBox_Blur(roam->seed_box);
            TextBox_Clear(roam->seed_box);
            Roam__NewWorld(roam, 0);
            return;
        }
    }
    if (roam->panel != PANEL_NONE && ListBox_Update(roam->list)) {
        roam->selected  = ListBox_GetSelectedIndex(roam->list);
        const int place = roam->panel == PANEL_PLACES && roam->selected >= 0 && roam->selected < roam->list_count
                              ? roam->list_places[roam->selected]
                              : -1;
        if (place >= 0 && place != roam->place) {
            Roam__Teleport(roam, place);
            if (roam->game && !Roam__DebugTools(roam)) {
                Roam__ToWaypoint(roam); // by waypoint: you come out at the one there
            }
        }
    }

    if (roam->class_button != NULL && Button_Update(roam->class_button)) {
        roam->player_class = (roam->player_class + 1) % PLAYER_CLASS_COUNT;
        const float x      = roam->player.x;
        const float y      = roam->player.y;
        Roam__ResetPlayer(roam); // new character, same spot
        roam->player.x = x;
        roam->player.y = y;
        Roam__UpdateButtons(roam);
    }
    if (Button_Update(roam->run_button)) {
        // Free Roam: walk, run, sprint, walk...
        if (roam->sprinting) {
            roam->sprinting = false;
            roam->running   = false;
        } else if (roam->running && roam->free_roam && (!roam->game || Roam__DebugTools(roam))) {
            roam->sprinting = true;
        } else {
            roam->running = !roam->running;
        }
        Roam__UpdateButtons(roam);
    }
    if ((!roam->game || Roam__DebugTools(roam)) && Controls_Pressed(ACTION_SPRINT)) {
        roam->sprinting = !roam->sprinting;
        Roam__ShowMessage(roam, roam->sprinting ? "Sprint on" : "Sprint off");
        Roam__UpdateButtons(roam);
    }
    if (roam->monsters_button != NULL && Button_Update(roam->monsters_button)) {
        Roam__TogglePanel(roam, PANEL_MONSTERS);
    }
    if (!roam->game && roam->places_button != NULL && Button_Update(roam->places_button)) {
        Roam__TogglePanel(roam, PANEL_PLACES);
    }
    if (roam->clear_button != NULL && Button_Update(roam->clear_button)) {
        int kept = 0;
        for (int i = 0; i < roam->unit_count; i++) {
            if (!roam->units[i].dropped) {
                roam->units[kept++] = roam->units[i];
            }
        }
        roam->unit_count    = kept;
        roam->attack_target = -1;
        roam->use_target    = -1;
        Roam__UpdateBlocking(roam);
    }
    if (roam->combat_button != NULL && Button_Update(roam->combat_button)) {
        combat_enabled = !combat_enabled;
        Roam__UpdateButtons(roam);
    }
    if (!roam->game && InputManager_ConsumeKeyPress(SDL_SCANCODE_ESCAPE)) {
        roam->menu_open = true;
    }
    if (!roam->game && Button_Update(roam->back_button)) {
        // Play-testing goes back to the editor (which takes its map back), otherwise to the main menu: asked first
        Roam__AskLeave(roam);
    }
}

void TestingGrounds_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

void Game_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

void FreeRoam_Update(void *scene_ref, const uint64_t delta) {
    (void)delta;
    Roam__Update(scene_ref);
}

// ---------------------------------------------------------------------------------------------------------------------
// Render

// A figure's states' overlays (behind it, or in front): animated as time goes
static void Roam__DrawOverlays(const DrawContext *draw, const int screen_x, const int screen_y, const float zoom,
                               const bool behind) {
    for (int s = 0; s < draw->state_count; s++) {
        StateOverlay overlays[STATE_OVERLAYS_MAX];
        const int    count = StateOverlays_For(draw->states[s], overlays, STATE_OVERLAYS_MAX);
        for (int o = 0; o < count; o++) {
            if (overlays[o].behind != behind) {
                continue;
            }
            char path[72];
            snprintf(path, sizeof(path), "/data/global/overlays/%s.dcc", overlays[o].file);
            MissileSprite *sprite = MissileSprite_Get(path, draw->palette, overlays[o].glow);
            if (sprite == NULL || MissileSprite_Frames(sprite) <= 0) {
                continue;
            }
            const int frame = (int)((SDL_GetTicks64() / 40) % (uint64_t)MissileSprite_Frames(sprite));
            MissileSprite_Draw(sprite, 0, frame, screen_x + (int)((float)overlays[o].x * zoom),
                               screen_y + (int)((float)overlays[o].y * zoom), zoom);
        }
    }
}

static void Roam__DrawFigure(void *context, const int screen_x, const int screen_y, const float zoom) {
    const DrawContext *draw = context;
    Roam__DrawOverlays(draw, screen_x, screen_y, zoom, true);
    if (draw->alpha != 0) {
        Figure_SetAlpha(draw->figure, draw->alpha);
    }
    Figure_Draw(draw->figure, draw->direction, draw->frame, screen_x, screen_y, zoom);
    if (draw->alpha != 0) {
        Figure_SetAlpha(draw->figure, 255);
    }
    Roam__DrawOverlays(draw, screen_x, screen_y, zoom, false);
}

// An item on the ground, in its flip or lying still; remembers where it went (for the mouse)
static void Roam__DrawGroundItem(void *context, const int screen_x, const int screen_y, const float zoom) {
    GroundItem *ground = context;
    ground->on_screen  = true;
    ground->screen_x   = screen_x;
    ground->screen_y   = screen_y;
    if (ground->sprite == NULL) {
        ground->rect = (SDL_Rect){screen_x - 8, screen_y - 8, 16, 12};
        return;
    }
    int w;
    int h;
    int ox;
    int oy;
    Sprite_GetFrameSize(ground->sprite, ground->frame, &w, &h);
    Sprite_GetFrameOffset(ground->sprite, ground->frame, &ox, &oy);
    const int x = screen_x + (int)((float)ox * zoom);
    const int y = screen_y + (int)((float)(oy - h) * zoom);
    Sprite_DrawScaled(ground->sprite, ground->frame, x, y, zoom);
    ground->rect = (SDL_Rect){x, y, (int)((float)w * zoom), (int)((float)h * zoom)};
}

static bool Roam__PrepareGroundItem(SceneRoam *roam, GroundItem *ground, const uint64_t now, MapDrawable *drawable) {
    ground->on_screen = false;
    if (ground->map_key != Roam__MapKey(roam)) {
        return false;
    }
    ground->sprite = Roam__ItemSprite(roam, &ground->item);
    if (ground->sprite != NULL) {
        const int frames = (int)Sprite_GetFrameCount(ground->sprite);
        const int frame  = (int)((now - ground->dropped_at) / ITEM_FLIP_MS);
        ground->frame    = frames > 0 ? (frame < frames ? frame : frames - 1) : 0;
    }
    drawable->x        = ground->x;
    drawable->y        = ground->y;
    drawable->draw     = Roam__DrawGroundItem;
    drawable->context  = ground;
    drawable->on_floor = true;
    return true;
}

// The names of the items on the ground: the one under the mouse, or all of them while Show items (Alt) is held. Like
// the game's: white on a dark box, blue under the mouse; boxes that would overlap move up.
static void Roam__DrawItemLabels(SceneRoam *roam) {
    const bool all = Controls_Held(ACTION_SHOW_ITEMS);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int g = 0; g < roam->ground_count; g++) {
        roam->ground[g].label_shown = false;
    }
    for (int g = 0; g < roam->ground_count; g++) {
        GroundItem *ground = &roam->ground[g];
        if (!ground->on_screen || (!all && g != roam->hover_item)) {
            continue;
        }
        const ItemBase *base = Items_Find(ground->item.code);
        char            text[80];
        if (strcmp(ground->item.code, "gld") == 0) {
            snprintf(text, sizeof(text), "%d Gold", ground->item.quantity);
        } else if (base != NULL && base->stackable && ground->item.quantity > 1) {
            snprintf(text, sizeof(text), "%s (%d)", base->name, ground->item.quantity);
        } else {
            char line2[80];
            Affixes_Name(&ground->item, text, sizeof(text), line2, sizeof(line2)); // magic: its full name
        }
        Label_SetText(roam->item_label, text);
        int w = 0;
        int h = 0;
        Label_GetSize(roam->item_label, &w, &h);
        SDL_Rect box = {ground->screen_x - (w / 2) - 3, ground->rect.y - h - 6, w + 6, h + 2};
        for (int tries = 0; tries < 40; tries++) { // up, out of the way of the names already shown
            bool overlaps = false;
            for (int o = 0; o < g && !overlaps; o++) {
                overlaps = roam->ground[o].label_shown && SDL_HasIntersection(&box, &roam->ground[o].label);
            }
            if (!overlaps) {
                break;
            }
            box.y -= h + 2;
        }
        ground->label       = box;
        ground->label_shown = true;
        const bool hovered  = g == roam->hover_item;
        SDL_SetRenderDrawColor(sdl_renderer, hovered ? 24 : 0, hovered ? 36 : 0, hovered ? 120 : 0,
                               hovered ? 220 : 170);
        SDL_RenderFillRect(sdl_renderer, &box);
        uint8_t cr;
        uint8_t cg;
        uint8_t cb;
        Affixes_QualityColor(&ground->item, &cr, &cg, &cb); // magic blue, rare yellow, unique gold
        Label_SetColor(roam->item_label, cr, cg, cb, 0xFF);
        Label_Draw(roam->item_label, box.x + 3, box.y + 1);
    }
}

// Whether word (lower case) is in text, whatever its case
static bool Roam__HasWord(const char *text, const char *word) {
    const size_t length = strlen(word);
    for (const char *at = text; *at != '\0'; at++) {
        size_t k = 0;
        while (k < length && at[k] != '\0' && tolower((unsigned char)at[k]) == word[k]) {
            k++;
        }
        if (k == length) {
            return true;
        }
    }
    return false;
}

// Torches, braziers, fires, candles: objects that light up the dark around them (by their name)
static bool Roam__GivesLight(const Unit *unit) {
    static const char *const words[] = {"torch", "fire", "brazier", "candle", "lamp", "flame", "bonfire", "lantern"};
    for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
        if (Roam__HasWord(unit->name, words[w]) || Roam__HasWord(unit->id, words[w])) {
            return true;
        }
    }
    return false;
}

static bool Roam__PrepareDraw(SceneRoam *roam, Unit *unit, MapDrawable *drawable) {
    if ((unit->state == UNIT_DEAD && unit->vanished) || unit->burrowed) {
        return false; // no corpse graphics: it's gone (or it's under the ground)
    }
    Figure *figure = Roam__Figure(roam, unit);
    if (figure == NULL) {
        return false;
    }
    int       first      = 0;
    const int frames     = Roam__FrameRange(unit, figure, &first);
    unit->draw.figure    = figure;
    unit->draw.direction = MapUnits_Direction(figure, unit->turn);
    unit->draw.frame     = frames > 0 ? first + (unit->frame % frames) : 0;
    if (unit->state == UNIT_DEAD) {
        unit->draw.frame = frames > 0 ? first + frames - 1 : 0;
    }
    if (unit->is_object && unit->rest_frame >= 0 && unit->state == UNIT_IDLE) {
        unit->draw.frame = unit->rest_frame;
    }
    // Its states, to be seen: ours (buffs, the aura), another player's (as they said), a monster's curse
    unit->draw.palette     = Roam__Palette(roam);
    unit->draw.state_count = 0;
    if (unit == &roam->player && roam->game) {
        unit->draw.state_count = Roam__PlayerStates(roam, unit->draw.states, UNIT_STATES);
    } else if (unit >= roam->others && unit < roam->others + roam->other_count) {
        const int id = roam->other_ids[unit - roam->others];
        for (int s = 0; id >= 0 && id <= NET_MAX_PLAYERS && s < roam->other_state_count[id]; s++) {
            snprintf(unit->draw.states[unit->draw.state_count++], 32, "%s", roam->other_states[id][s]);
        }
    } else {
        if (unit->aura != NULL && unit->aura->state[0] != '\0' && Roam__IsAlive(unit)) {
            snprintf(unit->draw.states[unit->draw.state_count++], 32, "%s", unit->aura->state); // a unique's aura
        }
        if (unit->curse != NULL && Roam__Now(roam) < unit->curse_until && Roam__IsAlive(unit) &&
            unit->curse->target_state[0] != '\0') {
            snprintf(unit->draw.states[unit->draw.state_count++], 32, "%s", unit->curse->target_state);
        }
    }
    drawable->x        = unit->x;
    drawable->y        = unit->y;
    drawable->draw     = Roam__DrawFigure;
    drawable->context  = &unit->draw;
    drawable->on_floor = false;
    return true;
}

// Name and life bar over a unit
static void Roam__DrawUnitInfo(SceneRoam *roam, const SDL_Rect *view, const Unit *unit, const bool show_bar) {
    float wx;
    float wy;
    MapView_SubtileToWorld(unit->x, unit->y, &wx, &wy);
    int sx;
    int sy;
    MapView_WorldToScreen(&roam->camera, view, wx, wy, &sx, &sy);
    int top = sy - (int)(90.0f * roam->camera.zoom);
    if (unit->draw.figure != NULL) {
        int xmin;
        int ymin;
        int xmax;
        int ymax;
        Figure_GetBounds(unit->draw.figure, &xmin, &ymin, &xmax, &ymax);
        top = sy + (int)((float)ymin * roam->camera.zoom) - 4;
    }

    Label_SetText(roam->target_label, unit->name);
    if (unit->rank == RANK_CHAMPION) {
        Label_SetColor(roam->target_label, 0x69, 0x69, 0xFF, 0xFF); // the game's blue
    } else if (unit->rank == RANK_UNIQUE || unit->rank == RANK_SUPER_UNIQUE) {
        Label_SetColor(roam->target_label, 0xC7, 0xB3, 0x77, 0xFF); // gold
    } else {
        Label_SetColor(roam->target_label, unit->npc ? 0xC0 : 0xFF, unit->npc ? 0xC0 : 0xE8, unit->npc ? 0xFF : 0xA0,
                       0xFF);
    }
    Label_Draw(roam->target_label, sx, top - (show_bar ? 8 : 0));
    if (show_bar && unit->max_hp > 0) {
        const SDL_Rect back = {sx - 25, top - 6, 50, 5};
        const SDL_Rect fill = {sx - 25, top - 6, 50 * unit->hp / unit->max_hp, 5};
        SDL_SetRenderDrawColor(sdl_renderer, 40, 0, 0, 220);
        SDL_RenderFillRect(sdl_renderer, &back);
        SDL_SetRenderDrawColor(sdl_renderer, 200, 20, 20, 255);
        SDL_RenderFillRect(sdl_renderer, &fill);
    }
    // What it's immune to (resistance 100 or more), and a unique's aura, under its life, like the game
    if (show_bar && roam->game && !unit->npc && !unit->is_object && roam->log_label != NULL) {
        static const char *const elements[ELEMENT_COUNT] = {"Physical", "Fire", "Cold", "Lightning", "Poison"};
        char                     text[128]               = "";
        if (unit->rank == RANK_CHAMPION && unit->champion != CHAMPION_PLAIN) {
            static const char *const kinds[] = {"", "Fanatic", "Berserker", "Ghostly", "Possessed"};
            snprintf(text, sizeof(text), "%s", kinds[unit->champion < 5 ? unit->champion : 0]);
        } else if ((unit->rank == RANK_UNIQUE || unit->rank == RANK_SUPER_UNIQUE) && unit->power[0] != '\0') {
            snprintf(text, sizeof(text), "%s", unit->power);
        }
        for (int e = 0; e < ELEMENT_COUNT; e++) {
            if (unit->resist[e] >= 100) {
                const size_t used = strlen(text);
                snprintf(text + used, sizeof(text) - used, "%s%s",
                         used == 0 ? "Immune to " : (strstr(text, "Immune") == NULL ? " - Immune to " : ", "),
                         elements[e]);
            }
        }
        if (unit->aura != NULL) {
            const size_t used = strlen(text);
            snprintf(text + used, sizeof(text) - used, "%s%s Aura", used == 0 ? "" : " - ", unit->aura->id);
        }
        if (text[0] != '\0') {
            Label_SetAlignment(roam->log_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
            Label_SetText(roam->log_label, text);
            Label_SetColor(roam->log_label, 0xFF, 0x80, 0x60, 0xFF);
            Label_Draw(roam->log_label, sx, top + 6);
            Label_SetAlignment(roam->log_label, LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
        }
    }
}

static void Roam__DrawPreview(SceneRoam *roam, const SDL_Rect *view) {
    CatalogCharacterInfo info;
    if (!CatalogCharacters_GetInfo(MapSession_GetBestiary(), roam->selected, &info)) {
        return;
    }
    float wx;
    float wy;
    MapView_SubtileToWorld(roam->hover_x, roam->hover_y, &wx, &wy);
    int sx;
    int sy;
    MapView_WorldToScreen(&roam->camera, view, wx, wy, &sx, &sy);

    // The sub-tile it lands on: green if it can stand there, red if not
    const bool walkable   = Map_IsWalkable(roam->map, (int)floorf(roam->hover_x), (int)floorf(roam->hover_y));
    const int  hw         = (int)(16.0f * roam->camera.zoom);
    const int  hh         = (int)(8.0f * roam->camera.zoom);
    SDL_Point  diamond[5] = {
        {sx,      sy - hh},
        {sx + hw, sy     },
        {sx,      sy + hh},
        {sx - hw, sy     },
        {sx,      sy - hh}
    };
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, walkable ? 80 : 255, walkable ? 255 : 60, 80, 220);
    SDL_RenderDrawLines(sdl_renderer, diamond, 5);

    Figure *figure = MapUnits_GetFigure(roam->units_cache, info.spec->base, info.id, info.idle_mode);
    if (figure != NULL) {
        int turn = 0;
        Roam__TurnTowards(&turn, roam->hover_x, roam->hover_y, roam->player.x, roam->player.y);
        const int frames = Figure_GetFramesPerDirection(figure);
        const int ms     = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
        const int frame  = frames > 0 ? (int)((SDL_GetTicks64() / (uint64_t)ms) % (uint64_t)frames) : 0;
        Figure_SetAlpha(figure, 140);
        Figure_Draw(figure, MapUnits_Direction(figure, turn), frame, sx, sy, roam->camera.zoom);
        Figure_SetAlpha(figure, 255);
    }

    Label_SetText(roam->target_label, info.name);
    Label_SetColor(roam->target_label, 0xA0, 0xFF, 0xA0, 0xFF);
    Label_Draw(roam->target_label, sx, sy + hh + 18);
}

// Single Player: mana and stamina next to life, the experience bar, messages
// Free Roam / Testing Grounds: the help line and the life bar over the button bar
static void Roam__DrawDebugBars(SceneRoam *roam) {
    const SDL_Rect help_bar = {0, VIEW_H - 14, VIEW_W, 14};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &help_bar);
    Roam__UpdateHelp(roam);
    Label_Draw(roam->help_label, 4, VIEW_H - 12);

    const Unit    *player = &roam->player;
    const SDL_Rect life   = {8, VIEW_H - 34, 120, 10};
    const SDL_Rect fill   = {8, VIEW_H - 34, player->max_hp > 0 ? 120 * player->hp / player->max_hp : 0, 10};
    SDL_SetRenderDrawColor(sdl_renderer, 40, 0, 0, 220);
    SDL_RenderFillRect(sdl_renderer, &life);
    SDL_SetRenderDrawColor(sdl_renderer, 200, 20, 20, 255);
    SDL_RenderFillRect(sdl_renderer, &fill);
    char life_text[64];
    if (player->state == UNIT_DEAD && roam->game && roam->character->dead) {
        snprintf(life_text, sizeof(life_text), "You died");
    } else if (player->state == UNIT_DEAD) {
        const uint64_t now = SDL_GetTicks64();
        snprintf(life_text, sizeof(life_text),
                 roam->game ? "You died - back in town in %d..." : "You died - back in %d...",
                 (int)((roam->respawn_at > now ? roam->respawn_at - now + 999 : 0) / 1000));
    } else {
        snprintf(life_text, sizeof(life_text), "Life %d / %d", player->hp, player->max_hp);
    }
    Label_SetText(roam->life_label, life_text);
    Label_Draw(roam->life_label, 134, VIEW_H - 37);
}

// Collision view (B): where each unit stands (the sub-tile clicks and fights go by); purple = monsters and the
// player, light blue = town folk
static void Roam__DrawUnitFootprints(SceneRoam *roam, const SDL_Rect *view) {
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    for (int i = -1; i < roam->unit_count; i++) {
        const Unit *unit = i < 0 ? &roam->player : &roam->units[i];
        if (unit->is_object || !Roam__IsAlive(unit)) {
            continue;
        }
        const float x       = floorf(unit->x);
        const float y       = floorf(unit->y);
        float       c[4][2] = {
            {x,        y       },
            {x + 1.0f, y       },
            {x + 1.0f, y + 1.0f},
            {x,        y + 1.0f}
        };
        SDL_Point points[5];
        for (int k = 0; k < 4; k++) {
            float wx;
            float wy;
            MapView_SubtileToWorld(c[k][0], c[k][1], &wx, &wy);
            MapView_WorldToScreen(&roam->camera, view, wx, wy, &points[k].x, &points[k].y);
        }
        points[4] = points[0];
        if (unit->npc) {
            SDL_SetRenderDrawColor(sdl_renderer, 140, 220, 255, 255);
        } else {
            SDL_SetRenderDrawColor(sdl_renderer, 230, 80, 255, 255);
        }
        SDL_RenderDrawLines(sdl_renderer, points, 5);
    }
}

// What the collision view's colours mean
static void Roam__DrawCollisionLegend(SceneRoam *roam) {
    static const struct {
        uint8_t     r, g, b;
        const char *text;
    } entries[] = {
        {255, 40,  40,  "Walls, cliffs, water (the map)"},
        {60,  140, 255, "Objects (chests, doors, ...)"  },
        {60,  230, 90,  "Way to the next area"          },
        {255, 220, 40,  "Entrance (click to go in)"     },
        {230, 80,  255, "Monsters and you"              },
        {140, 220, 255, "Town folk"                     },
    };
    const int      count = (int)(sizeof(entries) / sizeof(entries[0]));
    const SDL_Rect box   = {4, 18, 210, (count * 13) + 6};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &box);
    for (int i = 0; i < count; i++) {
        const SDL_Rect swatch = {box.x + 5, box.y + 5 + (i * 13), 9, 9};
        SDL_SetRenderDrawColor(sdl_renderer, entries[i].r, entries[i].g, entries[i].b, 255);
        SDL_RenderFillRect(sdl_renderer, &swatch);
        Label_SetText(roam->status_label, entries[i].text);
        Label_Draw(roam->status_label, box.x + 19, swatch.y - 1);
    }
}

// Multiplayer: the other players' circles (under their feet) and names, in each one's colour (Options › Multiplayer)
static void Roam__DrawOtherPlayers(SceneRoam *roam, const SDL_Rect *view) {
    bool names;
    bool circles;
    AbyssConfiguration_GetPlayerMarkers(&names, &circles);
    for (int o = 0; o < roam->other_count; o++) {
        const Unit *unit = &roam->others[o];
        uint8_t     r;
        uint8_t     g;
        uint8_t     b;
        NetColor_Get(roam->other_colors[o], &r, &g, &b);
        float wx;
        float wy;
        MapView_SubtileToWorld(unit->x, unit->y, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(&roam->camera, view, wx, wy, &sx, &sy);
        if (circles) {
            // A flat ellipse on the ground (the isometric view: twice as wide as it is tall)
            const float rx = 22.0f * roam->camera.zoom;
            const float ry = 11.0f * roam->camera.zoom;
            SDL_Point   ring[33];
            for (int i = 0; i <= 32; i++) {
                const float angle = (float)i * (6.2831853f / 32.0f);
                ring[i].x         = sx + (int)lroundf(cosf(angle) * rx);
                ring[i].y         = sy + (int)lroundf(sinf(angle) * ry);
            }
            SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl_renderer, r, g, b, 200);
            SDL_RenderDrawLines(sdl_renderer, ring, 33);
            for (int i = 0; i <= 32; i++) {
                ring[i].y += 1; // two pixels thick
            }
            SDL_RenderDrawLines(sdl_renderer, ring, 33);
        }
        if (names || o == roam->hover_other) {
            int top = sy - (int)(90.0f * roam->camera.zoom);
            if (unit->draw.figure != NULL) {
                int xmin;
                int ymin;
                int xmax;
                int ymax;
                Figure_GetBounds(unit->draw.figure, &xmin, &ymin, &xmax, &ymax);
                top = sy + (int)((float)ymin * roam->camera.zoom) - 6;
            }
            Label_SetText(roam->target_label, unit->name);
            Label_SetColor(roam->target_label, r, g, b, 0xFF);
            if (o == roam->hover_other) {
                Label_SetColor(roam->target_label, 0xFF, 0x40, 0x40, 0xFF); // PvP: a click attacks them
            }
            Label_Draw(roam->target_label, sx, top);
        }
    }
}

// Multiplayer: what was said lately (top left, like the game's), and the line being typed
static bool Roam__PortraitsShown(SceneRoam *roam);

static void Roam__DrawChat(SceneRoam *roam) {
    const uint64_t now = SDL_GetTicks64();
    int            y   = 30;
    const int      x   = Roam__PortraitsShown(roam) ? 140 : 12; // (beside the portraits)
    for (int l = 0; l < CHAT_LINES; l++) {
        if (roam->chat_lines[l][0] == '\0' || now - roam->chat_times[l] > CHAT_MS) {
            continue;
        }
        const bool said = strchr(roam->chat_lines[l], ':') != NULL; // "Name: ..." (the server's news has no name)
        Label_SetText(roam->chat_label, roam->chat_lines[l]);
        Label_SetColor(roam->chat_label, said ? 0xFF : 0xFF, said ? 0xFF : 0xE8, said ? 0xFF : 0x60, 0xFF);
        Label_Draw(roam->chat_label, x, y);
        y += 18;
    }
    if (TextBox_IsFocused(roam->chat_box)) {
        const SDL_Rect back = {110, 494, 580, 34};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
        SDL_RenderFillRect(sdl_renderer, &back);
        TextBox_Draw(roam->chat_box);
    }
}

static void Roam__Render(SceneRoam *roam) {
    const SDL_Rect view = {0, 0, VIEW_W, roam->game ? 600 : VIEW_H}; // Single Player: under the control panel

    SDL_SetRenderDrawColor(sdl_renderer, 10, 10, 14, 255);
    SDL_RenderClear(sdl_renderer);

    static MapDrawable drawables[MAX_UNITS + 2 + NET_MAX_PLAYERS + MAX_GROUND_ITEMS + MAX_MISSILES];
    int                count = 0;
    const uint64_t     now   = SDL_GetTicks64();
    for (int g = 0; g < roam->ground_count; g++) { // items on the ground first: under whoever stands there
        if (Roam__PrepareGroundItem(roam, &roam->ground[g], now, &drawables[count])) {
            count++;
        }
    }
    for (int i = 0; i < roam->unit_count; i++) {
        if (Roam__PrepareDraw(roam, &roam->units[i], &drawables[count])) {
            count++;
        }
    }
    for (int o = 0; o < roam->other_count; o++) { // the other players here (multiplayer)
        if (Roam__PrepareDraw(roam, &roam->others[o], &drawables[count])) {
            count++;
        }
    }
    if (roam->corpse_key != 0 && roam->corpse_key == Roam__MapKey(roam) &&
        Roam__PrepareDraw(roam, &roam->corpse, &drawables[count])) {
        count++; // where you fell
    }
    for (int i = 0; i < REMOTE_CORPSES && roam->online; i++) { // where the others fell
        if (roam->remote_corpses[i].id != 0 &&
            Roam__PrepareDraw(roam, &roam->remote_corpses[i].unit, &drawables[count])) {
            count++;
        }
    }
    if (Roam__PrepareDraw(roam, &roam->player, &drawables[count])) {
        count++;
    }
    count += Roam__PrepareMissiles(roam, &drawables[count], MAX_MISSILES);
    MapRenderer_SetViewer(roam->renderer, roam->player.x, roam->player.y); // roofs fade over the player
    MapRenderer_SetSeeThrough(roam->renderer, Roam__Gameplay(roam)->see_through);
    MapRenderer_Draw(roam->renderer, roam->map, &roam->camera, &view, drawables, count, roam->draw_flags);
    if (roam->draw_flags & MAP_DRAW_BLOCKED) {
        Roam__DrawUnitFootprints(roam, &view);
    }
    if (roam->free_roam) {
        Automap_Reveal(roam->automap, roam->player.x, roam->player.y);
        if (roam->show_automap) {
            AutomapMark marks[MAX_AUTOMAP_MARKS];
            int         mark_count = 0;
            for (int i = 0; i < roam->unit_count && mark_count < MAX_AUTOMAP_MARKS; i++) {
                const Unit *unit = &roam->units[i];
                if (!unit->is_object) {
                    continue;
                }
                const int cel = unit->object.operate_fn == 23 && unit->object.automap == 0 ? AUTOMAP_WAYPOINT_CEL
                                                                                           : unit->object.automap;
                if (cel > 0) {
                    marks[mark_count++] = (AutomapMark){unit->x, unit->y, cel, 0, NULL};
                }
            }
            // Multiplayer: your party on this map, in their colours (Party on automap)
            for (int o = 0; o < roam->other_count && mark_count < MAX_AUTOMAP_MARKS && !roam->automap_no_party; o++) {
                const NetPlayer *player = NetClient_Player(roam->other_ids[o]);
                if (!Roam__InMyParty(player)) {
                    continue;
                }
                uint8_t r;
                uint8_t g;
                uint8_t b;
                NetColor_Get(roam->other_colors[o], &r, &g, &b);
                marks[mark_count++] = (AutomapMark){roam->others[o].x, roam->others[o].y, -1,
                                                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b, roam->others[o].name};
            }
            Automap_SetStyle(roam->automap, roam->automap_fade, !roam->automap_no_names);
            const bool corner =
                roam->automap_size == 0 ? Roam__Gameplay(roam)->corner_automap : roam->automap_size == 2;
            if (corner) {
                // Small in the top right corner (Options > Gameplay > Automap), on a dark box
                const SDL_Rect box = {view.x + view.w - 214, view.y + 34, 204, 154};
                SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
                SDL_RenderFillRect(sdl_renderer, &box);
                SDL_SetRenderDrawColor(sdl_renderer, 0x80, 0x70, 0x48, 0xFF);
                SDL_RenderDrawRect(sdl_renderer, &box);
                const SDL_Rect inside = {box.x + 2, box.y + 2, box.w - 4, box.h - 4};
                Automap_DrawScaled(roam->automap, &inside, roam->player.x, roam->player.y, marks, mark_count, 2);
            } else {
                Automap_Draw(roam->automap, &view, roam->player.x, roam->player.y, marks, mark_count);
            }
        }
    }
    // Indoors it's dark but for the light around you (the game's light radius) and the torches and fires; outside
    // ("auto") the day goes round: day, dusk, night, dawn
    const bool dark_inside = light_level == 0 && roam->atmosphere.inside;
    int        darkness    = dark_inside ? 150 : light_levels[light_level].darkness;
    float      radius      = dark_inside ? 15.0f : light_levels[light_level].radius;
    if (light_level == 0 && !roam->atmosphere.inside) {
        Roam__TimeOfDay(&darkness, &radius);
    }
    if (roam->game && roam->character != NULL) {
        radius += (float)Affixes_Total(roam->character, MOD_LIGHT_RADIUS); // "+N to Light Radius" (or -N)
        radius  = radius < 4.0f ? 4.0f : radius;
    }
    if (darkness > 0) {
        MapLight lights[64];
        int      lit = 0;
        MapView_SubtileToWorld(roam->player.x, roam->player.y, &lights[0].world_x, &lights[0].world_y);
        lights[0].world_y    -= 30.0f;
        lights[lit++].radius  = radius;
        for (int o = 0; o < roam->other_count && lit < 64; o++) { // the others carry their light too
            MapView_SubtileToWorld(roam->others[o].x, roam->others[o].y, &lights[lit].world_x, &lights[lit].world_y);
            lights[lit].world_y  -= 30.0f;
            lights[lit++].radius  = 12.0f;
        }
        for (int i = 0; i < roam->unit_count && lit < 64; i++) {
            const Unit *unit = &roam->units[i];
            if (unit->is_object && !unit->vanished && Roam__GivesLight(unit) &&
                Roam__Distance(unit->x, unit->y, roam->player.x, roam->player.y) < 60.0f) {
                MapView_SubtileToWorld(unit->x, unit->y, &lights[lit].world_x, &lights[lit].world_y);
                lights[lit].world_y  -= 20.0f;
                lights[lit++].radius  = 8.0f;
            }
        }
        MapRenderer_DrawLights(roam->renderer, &roam->camera, &view, lights, lit, (uint8_t)darkness);
    }
    Roam__DrawWeather(roam, &view);

    Roam__DrawItemLabels(roam);

    // Walk target marker
    if (roam->moving && roam->attack_target < 0) {
        float wx;
        float wy;
        MapView_SubtileToWorld(roam->player.target_x, roam->player.target_y, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(&roam->camera, &view, wx, wy, &sx, &sy);
        SDL_SetRenderDrawColor(sdl_renderer, 255, 230, 120, 200);
        SDL_RenderDrawLine(sdl_renderer, sx - 6, sy, sx + 6, sy);
        SDL_RenderDrawLine(sdl_renderer, sx, sy - 3, sx, sy + 3);
    }

    // Placement preview: the selected monster, see-through, where right click would drop it
    if (roam->panel == PANEL_MONSTERS && roam->preview && roam->hover_valid) {
        Roam__DrawPreview(roam, &view);
    }

    // Who's under the mouse / being attacked; the other players' names (multiplayer)
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    Roam__DrawOtherPlayers(roam, &view);
    if (roam->attack_target >= 0 && roam->attack_target != roam->hovered) {
        Roam__DrawUnitInfo(roam, &view, &roam->units[roam->attack_target], true);
    }
    if (roam->hovered >= 0) {
        Roam__DrawUnitInfo(roam, &view, &roam->units[roam->hovered],
                           Roam__Combat(roam) && !Roam__IsPeaceful(roam, &roam->units[roam->hovered]));
    } else if (roam->hover_exit >= 0 && roam->hover_exit < roam->map->exit_count) {
        // The entrance's name ("To The Den of Evil") over it
        const MapExit *exit = &roam->map->exits[roam->hover_exit];
        char           name[96];
        World_GetWarpText(exit->target_level, name, sizeof(name));
        float wx;
        float wy;
        MapView_SubtileToWorld((float)(exit->x0 + exit->x1) / 2.0f, (float)(exit->y0 + exit->y1) / 2.0f, &wx, &wy);
        int sx;
        int sy;
        MapView_WorldToScreen(&roam->camera, &view, wx, wy, &sx, &sy);
        Label_SetText(roam->target_label, name);
        Label_SetColor(roam->target_label, 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(roam->target_label, sx, sy - (int)(70.0f * roam->camera.zoom));
    }

    // Status bar
    const SDL_Rect bar = {0, 0, VIEW_W, 14};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &bar);
    int npcs  = 0;
    int alive = 0;
    for (int i = 0; i < roam->unit_count; i++) {
        npcs  += roam->units[i].npc ? 1 : 0;
        alive += Roam__IsAlive(&roam->units[i]) ? 1 : 0;
    }
    char status[200];
    if (roam->game) {
        static const char *difficulty_names[3] = {"", "   Nightmare", "   Hell"};
        snprintf(status, sizeof(status), "%s%s   %s, level %d %s%s   light: %s", Roam__AreaName(roam),
                 difficulty_names[Roam__Difficulty(roam)], roam->character->name, roam->character->level,
                 Character_ClassName(roam->character->class_id), roam->character->hardcore ? " (hardcore)" : "",
                 light_levels[light_level].name);
    } else {
        char seed[32] = "";
        if (roam->free_roam) {
            snprintf(seed, sizeof(seed), "   seed %u", World_GetSeed());
        }
        snprintf(status, sizeof(status),
                 "%s%s   %s   %d units (%d NPCs, %d alive)   combat %s   light: %s%s   speed %.1f/s (%.0f px/s)",
                 Roam__AreaName(roam), roam->borrowed ? " (play-test)" : "", player_classes[roam->player_class][0],
                 roam->unit_count, npcs, alive, combat_enabled ? "ON" : "off", light_levels[light_level].name, seed,
                 (double)roam->speed, (double)roam->screen_speed);
    }
    Label_SetText(roam->status_label, status);
    Label_Draw(roam->status_label, 4, 2);
    if (roam->draw_flags & MAP_DRAW_BLOCKED) {
        Roam__DrawCollisionLegend(roam);
    }
    if (!roam->game) {
        Roam__DrawDebugBars(roam);
    }

    if (roam->panel != PANEL_NONE) {

        const SDL_Rect panel = {PANEL_X, 14, 800 - PANEL_X, VIEW_H - 28};
        SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 235);
        SDL_RenderFillRect(sdl_renderer, &panel);
        SDL_SetRenderDrawColor(sdl_renderer, 100, 85, 55, 255);
        SDL_RenderDrawRect(sdl_renderer, &panel);
        Label_Draw(roam->title_label, PANEL_X + ((800 - PANEL_X) / 2), 30);
        if (roam->panel == PANEL_MONSTERS) {
            CatalogCharacters_DrawCategories(MapSession_GetBestiary(), PANEL_INNER_X, CATEGORIES_Y, PANEL_INNER_W);
        }
        ListBox_Draw(roam->list);
        if (roam->panel == PANEL_MONSTERS) {
            Button_Draw(roam->preview_button);
        }
        if (roam->panel == PANEL_PLACES && roam->seed_box != NULL) {
            char text[64];
            snprintf(text, sizeof(text), "World seed: %u%s", World_GetSeed(),
                     fixed_seed != 0 ? " (fixed)" : " (random)");
            Label_SetText(roam->seed_label, text);
            Label_Draw(roam->seed_label, PANEL_INNER_X, SEED_LABEL_Y);
            TextBox_Draw(roam->seed_box);
            Button_Draw(roam->seed_use_button);
            Button_Draw(roam->seed_random_button);
        }
    }

    if (roam->online || TextBox_IsFocused(roam->chat_box)) {
        Roam__DrawChat(roam);
    }
    Roam__DrawLog(roam);
    Roam__DrawHelp(roam);
    if (roam->game && roam->char_panel) {
        Roam__DrawCharacterPanel(roam);
    }
    int ui_mouse_x;
    int ui_mouse_y;
    InputManager_GetMousePosition(&ui_mouse_x, &ui_mouse_y);
    if (roam->game && roam->inventory_panel) {
        GameInventory_Draw(roam->inventory_ui, roam->character, ui_mouse_x, ui_mouse_y);
    } else if (roam->game && roam->skill_panel) {
        GameSkills_DrawTree(roam->skills_ui, roam->character, ui_mouse_x, ui_mouse_y);
    }
    Roam__DrawPanelClose(roam, ui_mouse_x, ui_mouse_y);
    if (roam->party_panel) {
        Roam__DrawPartyPanel(roam, ui_mouse_x, ui_mouse_y);
    }
    Roam__DrawQuestPanel(roam);
    Roam__DrawNpcMenu(roam, ui_mouse_x, ui_mouse_y);
    Roam__DrawSpeech(roam);
    Roam__DrawHireling(roam);
    if (now < roam->caption_until) {
        const uint64_t left  = roam->caption_until - now;
        const uint8_t  alpha = left >= 1000 ? 255 : (uint8_t)((left * 255) / 1000);
        Label_SetColor(roam->caption_label, 0xFF, 0x58, 0x48, alpha); // the game's red
        Label_Draw(roam->caption_label, VIEW_W / 2, 80);
    }
    if (now < roam->message_until) {
        Label_Draw(roam->message_label, VIEW_W / 2, 125);
    }

    if (roam->game) {
        int mouse_x;
        int mouse_y;
        InputManager_GetMousePosition(&mouse_x, &mouse_y);
        Roam__DrawControlPanel(roam, mouse_x, mouse_y);
        // On top: the skill pickers, the item on the cursor, what's under the mouse
        GameSkills_DrawOverlay(roam->skills_ui, roam->character, roam->skill_panel, mouse_x, mouse_y);
        GameInventory_DrawOverlay(roam->inventory_ui, roam->character, roam->inventory_panel, mouse_x, mouse_y);
    } else {
        const SDL_Rect bottom = {0, VIEW_H, 800, 600 - VIEW_H};
        SDL_SetRenderDrawColor(sdl_renderer, 22, 20, 18, 255);
        SDL_RenderFillRect(sdl_renderer, &bottom);
        for (int i = 0; i < 6; i++) {
            Button_Draw(roam->buttons[i]);
        }
    }
    if (roam->options_panel != NULL) {
        // The game stays dimly behind it, without the bars and labels showing through
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 248);
        SDL_RenderFillRect(sdl_renderer, NULL);
        OptionsPanel_Draw(roam->options_panel);
    } else if (roam->menu_open && !ConfirmDialog_IsOpen(roam->confirm)) {
        const SDL_Rect screen = {0, 0, 800, 600};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(sdl_renderer, &screen);
        Button_Draw(roam->menu_options_button);
        Button_Draw(roam->menu_leave_button);
        Button_Draw(roam->menu_return_button);
    }
    ConfirmDialog_Draw(roam->confirm);
    AmountDialog_Draw(roam->amount);
    if (roam->screenshot) {
        roam->screenshot = false;
        char path[4300];
        if (Screenshot_Save(path, sizeof(path))) {
            const char *name = strrchr(path, '/');
            char        text[160];
            snprintf(text, sizeof(text), "Screenshot saved: %.120s", name != NULL ? name + 1 : path);
            Roam__ShowMessage(roam, text);
        }
    }
}

void TestingGrounds_Render(void *scene_ref) { Roam__Render(scene_ref); }

void FreeRoam_Render(void *scene_ref) { Roam__Render(scene_ref); }

void Game_Render(void *scene_ref) { Roam__Render(scene_ref); }

#include "SceneRoamText.inc"
