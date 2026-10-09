#ifndef ABYSS_STRING_TABLE_H
#define ABYSS_STRING_TABLE_H

// Localized game strings from /data/local/lng/<language>/*.tbl.
// patchstring.tbl overrides expansionstring.tbl, which overrides string.tbl.
void        StringTable_InitializeSingleton(const char *language);
void        StringTable_DestroySingleton(void);
const char *StringTable_Find(const char *key); // NULL when the key is unknown
const char *StringTable_Get(const char *key);  // the key itself when it is unknown

#endif // ABYSS_STRING_TABLE_H
