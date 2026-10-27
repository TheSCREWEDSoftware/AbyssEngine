#ifndef ABYSS_QUESTS_H
#define ABYSS_QUESTS_H

#include <stdbool.h>

// The game's quests, act by act, in the quest log's order (names and steps from the string tables: "qstsa1q1" Den of
// Evil, "qstsa1q12" Kill all the monsters in the Den, ...). Each has what finishes it (killing a boss, clearing an
// area, getting somewhere), who rewards it and with what. A character keeps each quest's state for each difficulty.

#define QUEST_COUNT 27

typedef enum QuestGoal {
    QUEST_NOT_YET, // not in this engine yet (the log says so)
    QUEST_KILL,    // the monster (spawned if the area doesn't have it)
    QUEST_CLEAR,   // every monster in the area
    QUEST_REACH,   // get to the area
} QuestGoal;

typedef enum QuestReward {
    REWARD_NONE,
    REWARD_SKILL_POINTS,
    REWARD_STAT_POINTS,
    REWARD_RESIST,    // to all resistances, for good
    REWARD_LIFE,      // to life, for good
    REWARD_IMBUE,     // a plain weapon or armor made rare (Charsi)
    REWARD_SOCKET,    // sockets in a plain weapon or armor (Larzuk's way; here Qual-Kehk)
    REWARD_RING,      // a magic ring (Akara, for Cain)
    REWARD_FREE_HIRE, // a hireling for nothing (Kashya)
    REWARD_TREASURE,  // a treasure class dropped where the boss died (the Countess's runes, Hephasto's gems)
} QuestReward;

typedef enum QuestState {
    QUEST_OPEN     = 0, // to do
    QUEST_REWARD   = 2, // done: go back for the reward
    QUEST_COMPLETE = 3,
} QuestState;

typedef struct Quest {
    int         act;    // 0..4
    int         number; // in the act's string keys (qstsa<act+1>q<number>)
    QuestGoal   goal;
    int         level;     // Levels.txt Id of the area
    const char *monster;   // QUEST_KILL: monstats id
    const char *boss_name; // spawned as this (the Countess, Shenk): NULL keeps the monster's name
    bool        spawn;     // the area's presets don't place the boss: put it there
    const char *giver;     // who rewards it (monstats id), NULL: on the spot
    QuestReward reward;
    int         amount;
    const char *treasure; // REWARD_TREASURE
    const char *todo_key; // the step shown while it's open
    const char *done_key; // ... once it's done ("Return to Akara for a reward")
} Quest;

const Quest *Quests_Get(int index);
const char  *Quests_Name(int index); // from the string tables
const char  *Quests_Step(int index, int state);

#endif // ABYSS_QUESTS_H
