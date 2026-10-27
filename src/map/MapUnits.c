#include "MapUnits.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Figure.h"
#include "MapSession.h"
#include "Objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNITS_MAX_DRAW 2048

static const int turn_order_8[8]   = {4, 0, 5, 1, 6, 2, 7, 3};
static const int turn_order_16[16] = {4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15};

typedef struct UnitFigure {
    char    key[160]; // base|id|mode, or a spec (MapUnits_GetSpecFigure)
    Figure *figure;   // NULL: tried, no graphics
} UnitFigure;

typedef struct UnitIdle {
    char        key[64]; // base|id
    const char *mode;    // points into the Bestiary's static mode tables
} UnitIdle;

typedef struct UnitDraw {
    Figure *figure;
    int     direction;
    int     frame;
} UnitDraw;

struct MapUnits {
    UnitIdle   *idle;
    int         idle_count;
    int         idle_capacity;
    UnitFigure *figures;
    int         figure_count;
    int         figure_capacity;
    UnitDraw    draws[UNITS_MAX_DRAW]; // contexts for this frame's drawables
};

MapUnits *MapUnits_Create(void) {
    MapUnits *units = calloc(1, sizeof(MapUnits));
    FAIL_IF_NULL(units);
    return units;
}

void MapUnits_Destroy(MapUnits **units) {
    if (units == NULL || *units == NULL) {
        return;
    }
    for (int i = 0; i < (*units)->figure_count; i++) {
        Figure_Destroy(&(*units)->figures[i].figure);
    }
    free((*units)->figures);
    free((*units)->idle);
    free(*units);
    *units = NULL;
}

const char *MapUnits_IdleMode(MapUnits *units, const char *base, const char *id) {
    // Looking an entry up in the Bestiary is a linear search: remember the answer
    char key[64];
    snprintf(key, sizeof(key), "%s|%s", base, id);
    for (int i = 0; i < units->idle_count; i++) {
        if (strcmp(units->idle[i].key, key) == 0) {
            return units->idle[i].mode;
        }
    }

    CatalogCharacterInfo info;
    const char *mode = CatalogCharacters_Find(MapSession_GetBestiary(), base, id, &info) ? info.idle_mode : NULL;
    if (units->idle_count == units->idle_capacity) {
        units->idle_capacity = units->idle_capacity > 0 ? units->idle_capacity * 2 : 32;
        UnitIdle *grown      = realloc(units->idle, sizeof(UnitIdle) * (size_t)units->idle_capacity);
        FAIL_IF_NULL(grown);
        units->idle = grown;
    }
    snprintf(units->idle[units->idle_count].key, sizeof(units->idle[0].key), "%s", key);
    units->idle[units->idle_count].mode = mode;
    units->idle_count++;
    return mode;
}

Figure *MapUnits_GetFigure(MapUnits *units, const char *base, const char *id, const char *mode) {
    if (mode == NULL) {
        return NULL;
    }
    char key[64];
    snprintf(key, sizeof(key), "%s|%s|%s", base, id, mode);
    for (int i = 0; i < units->figure_count; i++) {
        if (strcmp(units->figures[i].key, key) == 0) {
            return units->figures[i].figure;
        }
    }

    Figure              *figure = NULL;
    CatalogCharacterInfo info;
    if (CatalogCharacters_Find(MapSession_GetBestiary(), base, id, &info)) {
        figure = Figure_Create(info.spec, mode, PALETTE_ACT_1);
    }

    if (units->figure_count == units->figure_capacity) {
        units->figure_capacity = units->figure_capacity > 0 ? units->figure_capacity * 2 : 32;
        UnitFigure *grown      = realloc(units->figures, sizeof(UnitFigure) * (size_t)units->figure_capacity);
        FAIL_IF_NULL(grown);
        units->figures = grown;
    }
    UnitFigure *entry = &units->figures[units->figure_count++];
    snprintf(entry->key, sizeof(entry->key), "%s", key);
    entry->figure = figure;
    return figure;
}

Figure *MapUnits_GetSpecFigure(MapUnits *units, const FigureSpec *spec, const char *mode) {
    if (mode == NULL) {
        return NULL;
    }
    char key[160];
    int  at = snprintf(key, sizeof(key), "@%s|%s|%s|%s", spec->base, spec->token, spec->weapon_class, mode);
    for (int c = 0; c < COF_COMPONENT_COUNT && at > 0 && at < (int)sizeof(key); c++) {
        at += snprintf(key + at, sizeof(key) - (size_t)at, "|%s", spec->armor[c]);
    }
    for (int i = 0; i < units->figure_count; i++) {
        if (strcmp(units->figures[i].key, key) == 0) {
            return units->figures[i].figure;
        }
    }
    Figure *figure = Figure_Create(spec, mode, PALETTE_ACT_1);
    if (units->figure_count == units->figure_capacity) {
        units->figure_capacity = units->figure_capacity > 0 ? units->figure_capacity * 2 : 32;
        UnitFigure *grown      = realloc(units->figures, sizeof(UnitFigure) * (size_t)units->figure_capacity);
        FAIL_IF_NULL(grown);
        units->figures = grown;
    }
    UnitFigure *entry = &units->figures[units->figure_count++];
    snprintf(entry->key, sizeof(entry->key), "%s", key);
    entry->figure = figure;
    return figure;
}

int MapUnits_Direction(const Figure *figure, const int turn) {
    const int directions = Figure_GetDirectionCount(figure);
    const int t          = ((turn % 16) + 16) % 16;
    if (directions == 16) {
        return turn_order_16[t];
    }
    if (directions == 8) {
        return turn_order_8[(t + 1) / 2 % 8];
    }
    return directions > 0 ? (t * directions / 16) % directions : 0;
}

int MapUnits_TurnTowards(const float screen_dx, const float screen_dy) {
    // 0 = south (down the screen), increasing clockwise on screen: west, north, east
    float angle = atan2f(-screen_dx, screen_dy) * (180.0f / 3.14159265f);
    if (angle < 0) {
        angle += 360.0f;
    }
    return (int)lroundf(angle / 22.5f) % 16;
}

static void MapUnits__Draw(void *context, const int screen_x, const int screen_y, const float zoom) {
    UnitDraw *draw = context;
    Figure_Draw(draw->figure, draw->direction, draw->frame, screen_x, screen_y, zoom);
}

int MapUnits_BuildDrawables(MapUnits *units, const Map *map, const uint64_t time_ms, MapDrawable *drawables,
                            const int max) {
    int count = 0;
    for (int e = 0; e < map->entity_count && count < max && count < UNITS_MAX_DRAW; e++) {
        const MapEntity *entity = &map->entities[e];
        // Objects can say which animation they show (lit torches); otherwise the usual idle one
        Figure *figure =
            entity->mode[0] != '\0' ? MapUnits_GetFigure(units, entity->base, entity->id, entity->mode) : NULL;
        if (figure == NULL) {
            figure =
                MapUnits_GetFigure(units, entity->base, entity->id, MapUnits_IdleMode(units, entity->base, entity->id));
        }
        if (figure == NULL) {
            continue;
        }
        int frames = Figure_GetFramesPerDirection(figure);
        int first  = 0;
        if (entity->object_row >= 0) {
            // An object's files often hold every state's frames (closed, opening..., open): objects.txt says which
            // ones the resting animation shows
            ObjectInfo info;
            if (Objects_Get(entity->object_row, &info)) {
                Objects_FrameRange(&info, Objects_ModeIndex(entity->mode[0] != '\0' ? entity->mode : "nu"), frames,
                                   &first, &frames);
            }
        }
        const int frame_ms = Figure_GetFrameMilliseconds(figure) > 0 ? Figure_GetFrameMilliseconds(figure) : 40;
        // Offset each entity's animation so a group doesn't move in lockstep
        const uint64_t offset = (uint64_t)e * 7919u;

        UnitDraw *draw  = &units->draws[count];
        draw->figure    = figure;
        draw->direction = MapUnits_Direction(figure, entity->direction);
        draw->frame     = frames > 0 ? first + (int)(((time_ms + offset) / (uint64_t)frame_ms) % (uint64_t)frames) : 0;

        drawables[count].x        = entity->x;
        drawables[count].y        = entity->y;
        drawables[count].draw     = MapUnits__Draw;
        drawables[count].context  = draw;
        drawables[count].on_floor = false;
        count++;
    }
    return count;
}
