#ifndef ABYSS_MAP_UNITS_H
#define ABYSS_MAP_UNITS_H

#include "../scenes/CatalogCharacters.h"
#include "Map.h"
#include "MapRenderer.h"

// Animated figures for the monsters, NPCs and objects placed on a map, using the Bestiary for their graphics.
// Figures are shared between entities of the same kind.

typedef struct MapUnits MapUnits;

MapUnits *MapUnits_Create(void); // uses the session's Bestiary, built the first time a figure is needed
void      MapUnits_Destroy(MapUnits **units);
// The figure for a character in one animation (cached). NULL if it has no graphics.
struct Figure *MapUnits_GetFigure(MapUnits *units, const char *base, const char *id, const char *mode);
// A figure made to measure (a character in what they wear): kept by what the spec says
struct Figure *MapUnits_GetSpecFigure(MapUnits *units, const struct FigureSpec *spec, const char *mode);
const char    *MapUnits_IdleMode(MapUnits *units, const char *base, const char *id);
// Game direction for a turn index (0 = facing the viewer, clockwise, 16 steps) on a figure with n directions.
int MapUnits_Direction(const struct Figure *figure, int turn);
// Turn index (0..15) for facing along a screen-space vector.
int MapUnits_TurnTowards(float screen_dx, float screen_dy);
// Fills drawables for all entities of the map (idle animation); returns the count (at most max).
int MapUnits_BuildDrawables(MapUnits *units, const Map *map, uint64_t time_ms, MapDrawable *drawables, int max);

#endif // ABYSS_MAP_UNITS_H
