#include "MapSession.h"
#include "../drawing/LoadingScreen.h"
#include "LevelBuilder.h"
#include "MonsterStats.h"
#include "Objects.h"
#include "Places.h"
#include "World.h"
#include <stddef.h>

static CatalogCharacters *session_bestiary = NULL;
static Map               *session_map      = NULL;
static bool               session_playtest = false;
static Map               *session_try      = NULL; // Try in area: the built area to play instead

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

void MapSession_SetTryMap(Map *map) {
    if (session_try != NULL && session_try != map) {
        Map_Destroy(&session_try);
    }
    session_try = map;
}

Map *MapSession_TakeTryMap(void) {
    Map *map    = session_try;
    session_try = NULL;
    return map;
}

void MapSession_Release(void) {
    Map_Destroy(&session_map);
    Map_Destroy(&session_try);
    session_playtest = false;
    CatalogCharacters_Destroy(&session_bestiary);
    MonsterStats_Release();
    Objects_Release();
    Places_Release();
    LevelBuilder_Release();
    World_Release();
}
