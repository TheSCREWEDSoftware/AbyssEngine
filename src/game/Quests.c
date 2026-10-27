#include "Quests.h"
#include "../data/StringTable.h"
#include <stdio.h>

// (levels: Levels.txt Ids. The Den of Evil 8, Burial Grounds 17, Tower Cellar Level 5 25, Barracks 28, Tristram 38,
// Catacombs Level 4 37, Sewers Level 3 49, Arcane Sanctuary 74, Tal Rasha's Chamber 73, Ruined Temple 94, Durance of
// Hate Level 3 102, Plains of Despair 105, River of Flame 107, Chaos Sanctum 108, Bloody Foothills 110, Frozen River
// 114, Halls of Vaught 124, Worldstone Chamber 132)
static const Quest quests[QUEST_COUNT] = {
    // Act 1
    {0, 1, QUEST_CLEAR, 8,   NULL,            NULL,                 false, "akara",     REWARD_SKILL_POINTS, 1,  NULL,         "qstsa1q12", "qstsa1q15" },
    {0, 2, QUEST_KILL,  17,  "bloodraven",    NULL,                 true,  "kashya",    REWARD_FREE_HIRE,    0,  NULL,         "qstsa1q22", "qstsa1q23" },
    {0, 3, QUEST_KILL,  28,  "smith",         NULL,                 false, "charsi",    REWARD_IMBUE,        0,  NULL,         "qstsa1q31", "qstsa1q32b"},
    {0, 4, QUEST_REACH, 38,  NULL,            NULL,                 false, "akara",     REWARD_RING,         0,  NULL,         "qstsa1q43", "qstsa1q46b"},
    {0, 5, QUEST_KILL,  25,  "corruptrogue3", "The Countess",       true,  NULL,        REWARD_TREASURE,     0,  "Countess",   "qstsa1q52",
     NULL                                                                                                                                               },
    {0, 6, QUEST_KILL,  37,  "andariel",      NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa1q62", NULL        },
    // Act 2
    {1, 1, QUEST_KILL,  49,  "radament",      NULL,                 false, "atma",      REWARD_SKILL_POINTS, 1,  NULL,         "qstsa2q12", "qstsa2q13" },
    {1, 2, QUEST_ITEMS, 0,   NULL,            NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa2q22", NULL        },
    {1, 3, QUEST_ITEMS, 0,   NULL,            NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa2q32", NULL        },
    {1, 4, QUEST_REACH, 74,  NULL,            NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa2q41", NULL        },
    {1, 5, QUEST_KILL,  74,  "summoner",      NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa2q52", NULL        },
    {1, 6, QUEST_KILL,  73,  "duriel",        NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa2q62", NULL        },
    // Act 3 (the log's order: the Golden Bird, the Blade, Khalim's Will, Lam Esen's Tome, the Temple, the Guardian)
    {2, 4, QUEST_ITEMS, 0,   NULL,            NULL,                 false, "alkor",     REWARD_LIFE,         20, NULL,         "qstsa3q41", "qstsa3q45" },
    {2, 3, QUEST_ITEMS, 0,   NULL,            NULL,                 false, "ormus",     REWARD_RING,         0,  NULL,         "qstsa3q31", "qstsa3q33" },
    {2, 2, QUEST_ITEMS, 0,   NULL,            NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa3q21", NULL        },
    {2, 1, QUEST_REACH, 94,  NULL,            NULL,                 false, "alkor",     REWARD_STAT_POINTS,  5,  NULL,         "qstsa3q11", "qstsa3q12" },
    {2, 5, QUEST_ITEMS, 0,   NULL,            NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa3q52", NULL        },
    {2, 6, QUEST_KILL,  102, "mephisto",      NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa3q63", NULL        },
    // Act 4
    {3, 1, QUEST_KILL,  105, "izual",         NULL,                 true,  "tyrael2",   REWARD_SKILL_POINTS, 2,  NULL,         "qstsa4q12", "qstsa4q13" },
    {3, 3, QUEST_KILL,  107, "hephasto",      NULL,                 true,  NULL,        REWARD_TREASURE,     0,  "Hell Forge", "qstsa4q31", NULL        },
    {3, 2, QUEST_KILL,  108, "diablo",        NULL,                 true,  NULL,        REWARD_NONE,         0,  NULL,         "qstsa4q22", NULL        },
    // Act 5
    {4, 1, QUEST_KILL,  110, "overseer1",     "Shenk the Overseer", true,  "qual-kehk", REWARD_SOCKET,       0,  NULL,         NULL,        NULL        },
    {4, 2, QUEST_ITEMS, 0,   NULL,            NULL,                 false, "qual-kehk", REWARD_FREE_HIRE,    0,  NULL,         "qstsa5q21", "qstsa5q23" },
    {4, 3, QUEST_REACH, 114, NULL,            NULL,                 false, "malah",     REWARD_RESIST,       10, NULL,         "qstsa5q31", "qstsa5q34" },
    {4, 4, QUEST_KILL,  124, "nihlathakboss", NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa5q42", NULL        },
    {4, 5, QUEST_ITEMS, 0,   NULL,            NULL,                 false, NULL,        REWARD_EXPERIENCE,   0,  NULL,         "qstsa5q51", NULL        },
    {4, 6, QUEST_KILL,  132, "baalcrab",      NULL,                 false, NULL,        REWARD_NONE,         0,  NULL,         "qstsa5q62", NULL        },
};

const Quest *Quests_Get(const int index) { return index >= 0 && index < QUEST_COUNT ? &quests[index] : NULL; }

const char *Quests_Name(const int index) {
    static char  text[64];
    const Quest *q = Quests_Get(index);
    if (q == NULL) {
        return "";
    }
    char key[24];
    snprintf(key, sizeof(key), "qstsa%dq%d", q->act + 1, q->number);
    const char *found = StringTable_Find(key);
    snprintf(text, sizeof(text), "%s", found != NULL ? found : key);
    return text;
}

const char *Quests_Step(const int index, const int state) {
    const Quest *q = Quests_Get(index);
    if (q == NULL) {
        return "";
    }
    if (q->goal == QUEST_NOT_YET) {
        return "Not in this engine yet.";
    }
    if (state == QUEST_COMPLETE) {
        return "Quest completed.";
    }
    const char *key = state == QUEST_REWARD ? q->done_key : q->todo_key;
    if (state == QUEST_REWARD && key == NULL) {
        return "Quest completed.";
    }
    const char *found = key != NULL ? StringTable_Find(key) : NULL;
    if (found != NULL && found[0] != '\0') {
        return found;
    }
    switch (q->goal) {
    case QUEST_KILL:
        return state == QUEST_REWARD ? "Return to town for your reward." : "Find and kill the boss.";
    case QUEST_REACH:
        return "Find the place.";
    default:
        return "";
    }
}
