#include "MapSession.h"
#include "../drawing/LoadingScreen.h"
#include "MonsterStats.h"
#include "Places.h"
#include <stddef.h>

static CatalogCharacters *session_bestiary = NULL;
static Map               *session_map      = NULL;
static bool               session_playtest = false;

CatalogCharacters *MapSession_GetBestiary(void) {
    if (session_bestiary == NULL) {
        LoadingScreen_Begin("Loading monsters and NPCs...");
        session_bestiary = CatalogCharacters_Create();
        LoadingScreen_End();
    }
    return session_bestiary;
}

void MapSession_ParkMap(Map *map, const bool playtest) {
    if (session_map != NULL && session_map != map) {
        Map_Destroy(&session_map);
    }
    session_map      = map;
    session_playtest = playtest;
}

Map *MapSession_TakeMap(void) {
    Map *map         = session_map;
    session_map      = NULL;
    session_playtest = false;
    return map;
}

const Map *MapSession_PeekMap(void) { return session_map; }

bool MapSession_IsPlaytest(void) { return session_playtest; }

void MapSession_Release(void) {
    Map_Destroy(&session_map);
    session_playtest = false;
    CatalogCharacters_Destroy(&session_bestiary);
    MonsterStats_Release();
    Places_Release();
}
