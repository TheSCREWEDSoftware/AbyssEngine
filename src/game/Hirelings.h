#ifndef ABYSS_HIRELINGS_H
#define ABYSS_HIRELINGS_H

#include <stdbool.h>

// Hirelings, from the game's hireling.txt: who sells them (Kashya, Greiz, Asheara, Qual-Kehk), what kind (a Rogue
// Scout's fire or cold arrows, a Desert Mercenary's spear, ...), what they cost and how strong they are at a level
// (the table's rows at levels 3, 36, 67 and so on, with so much more for each level above).

#define HIRE_OFFERS 5

typedef struct HireOffer {
    int  row; // hireling.txt
    int  level;
    int  name; // which of the act's names (string table key index)
    int  cost;
    char kind[40]; // "Fire - Normal"
} HireOffer;

typedef struct HireStats {
    int  hp;
    int  defense;
    int  attack_rating;
    int  damage_min;
    int  damage_max;
    int  resist;
    bool ranged;  // bow (Rogue Scouts) or spells (Eastern Sorcerers): attacks from afar
    int  element; // the kind's element (Element), 0 = none
} HireStats;

bool        Hirelings_Sells(const char *npc_id);
int         Hirelings_Offers(const char *npc_id, int difficulty, int character_level, HireOffer *offers, int most);
bool        Hirelings_Stats(int row, int level, HireStats *stats);
const char *Hirelings_MonsterId(int row); // the monstats id to make them as ("roguehire")
void        Hirelings_Name(int row, int name, char *out, int size);
const char *Hirelings_Title(int row); // "Rogue Scout"
int         Hirelings_ReviveCost(int level);
int         Hirelings_Experience(int row); // their Exp/Lvl
void        Hirelings_Release(void);

#endif // ABYSS_HIRELINGS_H
