#ifndef ABYSS_MODS_H
#define ABYSS_MODS_H

#include <stdbool.h>
#include <stdint.h>

// Mods, the way Diablo's community engines do them: each a folder in mods/ (next to abyss.ini) holding the game's
// files it changes, in the MPQs' own paths (data/global/excel/weapons.txt, lower case), with a mod.ini saying what it
// is:
//
//   [Mod]
//   Name = Better Drops
//   Author = Someone
//   Version = 1.0
//   Description = Uniques drop five times as often.
//
// Turned on and put in order in Options > Mods (kept in mods/load_order.txt: a line per mod, "+folder" on, "-folder"
// off, the first line first). A file comes from the first mod on that has it, then the DataFolder (abyss.ini), then
// the MPQs. Changes take effect the next time the game starts (the tables are read once).

#define MODS_MAX 64

typedef struct Mod {
    char folder[64]; // its folder's name
    char name[64];
    char author[64];
    char version[24];
    char description[256];
    bool enabled;
} Mod;

void       Mods_Load(void); // looks through mods/ and reads the load order (at startup)
int        Mods_Count(void);
const Mod *Mods_Get(int index);
void       Mods_SetEnabled(int index, bool enabled);
void       Mods_Move(int index, int step); // up (-1) or down (+1) the order
void       Mods_Save(void);                // the load order, to mods/load_order.txt
bool       Mods_Changed(void);             // since the game started (a restart takes them in)
// The file in the first mod on that has it (path: the MPQ path, lower case with '/'), false if none
bool Mods_FindFile(const char *path, char *out, int size);
// The mods on, in order (folder and version each): multiplayer games need everyone's to match. "" for none.
void        Mods_Signature(char *out, int size);
const char *Mods_Folder(void); // <config>/mods

#endif // ABYSS_MODS_H
