#include "Pathfinding.h"
#include "../common/Logging.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PATH_MARGIN         48     // sub-tiles searched beyond the box around start and goal
#define PATH_MAX_EXPANSIONS 160000 // give up (and go to the closest point found) after this many
#define PATH_LINE_STEP      0.25f
#define SQRT2               1.41421356f

typedef struct HeapNode {
    float f;
    int   index;
} HeapNode;

typedef struct Search {
    int       x0; // window origin and size, in sub-tiles
    int       y0;
    int       w;
    int       h;
    float    *g;
    int      *parent;
    uint8_t  *state; // 0 = new, 1 = open, 2 = closed
    HeapNode *heap;
    int       heap_count;
    int       heap_capacity;
} Search;

static bool Path__Walkable(const Map *map, const int x, const int y) { return Map_IsWalkable(map, x, y); }

bool Map_HasClearLine(const Map *map, const float from_x, const float from_y, const float to_x, const float to_y) {
    const float dx       = to_x - from_x;
    const float dy       = to_y - from_y;
    const float distance = sqrtf((dx * dx) + (dy * dy));
    const int   steps    = (int)(distance / PATH_LINE_STEP) + 1;
    for (int i = 1; i <= steps; i++) {
        const float t = (float)i / (float)steps;
        if (!Path__Walkable(map, (int)floorf(from_x + (dx * t)), (int)floorf(from_y + (dy * t)))) {
            return false;
        }
    }
    return true;
}

static void Path__Push(Search *search, const float f, const int index) {
    if (search->heap_count == search->heap_capacity) {
        search->heap_capacity = search->heap_capacity > 0 ? search->heap_capacity * 2 : 1024;
        HeapNode *grown       = realloc(search->heap, sizeof(HeapNode) * (size_t)search->heap_capacity);
        FAIL_IF_NULL(grown);
        search->heap = grown;
    }
    int i = search->heap_count++;
    while (i > 0) {
        const int parent = (i - 1) / 2;
        if (search->heap[parent].f <= f) {
            break;
        }
        search->heap[i] = search->heap[parent];
        i               = parent;
    }
    search->heap[i] = (HeapNode){f, index};
}

static HeapNode Path__Pop(Search *search) {
    const HeapNode top  = search->heap[0];
    const HeapNode last = search->heap[--search->heap_count];
    int            i    = 0;
    for (;;) {
        int child = (i * 2) + 1;
        if (child >= search->heap_count) {
            break;
        }
        if (child + 1 < search->heap_count && search->heap[child + 1].f < search->heap[child].f) {
            child++;
        }
        if (search->heap[child].f >= last.f) {
            break;
        }
        search->heap[i] = search->heap[child];
        i               = child;
    }
    if (search->heap_count > 0) {
        search->heap[i] = last;
    }
    return top;
}

static float Path__Heuristic(const int x, const int y, const int gx, const int gy) {
    const int dx = abs(x - gx);
    const int dy = abs(y - gy);
    return (float)(dx > dy ? dx : dy) + ((SQRT2 - 1.0f) * (float)(dx < dy ? dx : dy)); // octile distance
}

// Straightens the cell-by-cell route into as few legs as possible
static void Path__Smooth(const Map *map, const float from_x, const float from_y, const int *cells_x, const int *cells_y,
                         const int cell_count, const float end_x, const float end_y, MapPath *path) {
    float x = from_x;
    float y = from_y;
    int   i = 0;
    while (i < cell_count && path->count < PATH_MAX_POINTS) {
        int furthest = i;
        for (int j = cell_count - 1; j > i; j--) {
            const float cx = j == cell_count - 1 ? end_x : (float)cells_x[j] + 0.5f;
            const float cy = j == cell_count - 1 ? end_y : (float)cells_y[j] + 0.5f;
            if (Map_HasClearLine(map, x, y, cx, cy)) {
                furthest = j;
                break;
            }
        }
        x                    = furthest == cell_count - 1 ? end_x : (float)cells_x[furthest] + 0.5f;
        y                    = furthest == cell_count - 1 ? end_y : (float)cells_y[furthest] + 0.5f;
        path->x[path->count] = x;
        path->y[path->count] = y;
        path->count++;
        i = furthest + 1;
    }
}

bool Map_FindPath(const Map *map, const float from_x, const float from_y, const float to_x, const float to_y,
                  MapPath *path) {
    path->count   = 0;
    path->next    = 0;
    path->partial = false;

    const int map_w = map->width * MAP_SUBTILES;
    const int map_h = map->height * MAP_SUBTILES;
    const int sx    = (int)floorf(from_x);
    const int sy    = (int)floorf(from_y);
    int       gx    = (int)floorf(to_x);
    int       gy    = (int)floorf(to_y);
    gx              = gx < 0 ? 0 : (gx >= map_w ? map_w - 1 : gx);
    gy              = gy < 0 ? 0 : (gy >= map_h ? map_h - 1 : gy);

    // Nothing in the way: walk straight there
    if (Path__Walkable(map, gx, gy) && Map_HasClearLine(map, from_x, from_y, to_x, to_y)) {
        path->x[0]  = to_x;
        path->y[0]  = to_y;
        path->count = 1;
        return true;
    }

    Search search;
    memset(&search, 0, sizeof(search));
    search.x0 = (sx < gx ? sx : gx) - PATH_MARGIN;
    search.y0 = (sy < gy ? sy : gy) - PATH_MARGIN;
    int x1    = (sx > gx ? sx : gx) + PATH_MARGIN;
    int y1    = (sy > gy ? sy : gy) + PATH_MARGIN;
    search.x0 = search.x0 < 0 ? 0 : search.x0;
    search.y0 = search.y0 < 0 ? 0 : search.y0;
    x1        = x1 >= map_w ? map_w - 1 : x1;
    y1        = y1 >= map_h ? map_h - 1 : y1;
    search.w  = x1 - search.x0 + 1;
    search.h  = y1 - search.y0 + 1;
    if (search.w <= 0 || search.h <= 0 || sx < search.x0 || sy < search.y0 || sx > x1 || sy > y1) {
        return false;
    }
    const size_t cells = (size_t)search.w * (size_t)search.h;
    search.g           = malloc(sizeof(float) * cells);
    search.parent      = malloc(sizeof(int) * cells);
    search.state       = calloc(cells, 1);
    FAIL_IF_NULL(search.g);
    FAIL_IF_NULL(search.parent);
    FAIL_IF_NULL(search.state);

    const int start      = ((sy - search.y0) * search.w) + (sx - search.x0);
    const int goal       = ((gy - search.y0) * search.w) + (gx - search.x0);
    search.g[start]      = 0;
    search.parent[start] = -1;
    search.state[start]  = 1;
    Path__Push(&search, Path__Heuristic(sx, sy, gx, gy), start);

    int   best       = start; // closest to the goal so far
    float best_h     = Path__Heuristic(sx, sy, gx, gy);
    int   expansions = 0;
    bool  found      = false;

    static const int   dirs_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int   dirs_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float costs[8]  = {1, 1, 1, 1, SQRT2, SQRT2, SQRT2, SQRT2};

    while (search.heap_count > 0 && expansions < PATH_MAX_EXPANSIONS) {
        const HeapNode node = Path__Pop(&search);
        if (search.state[node.index] == 2) {
            continue; // an outdated heap entry
        }
        search.state[node.index] = 2;
        expansions++;
        if (node.index == goal) {
            found = true;
            break;
        }

        const int   x = (node.index % search.w) + search.x0;
        const int   y = (node.index / search.w) + search.y0;
        const float h = Path__Heuristic(x, y, gx, gy);
        if (h < best_h) {
            best_h = h;
            best   = node.index;
        }

        for (int d = 0; d < 8; d++) {
            const int nx = x + dirs_x[d];
            const int ny = y + dirs_y[d];
            if (nx < search.x0 || ny < search.y0 || nx >= search.x0 + search.w || ny >= search.y0 + search.h ||
                !Path__Walkable(map, nx, ny)) {
                continue;
            }
            // Diagonals only when both sides are open (no squeezing through corners)
            if (d >= 4 && (!Path__Walkable(map, x + dirs_x[d], y) || !Path__Walkable(map, x, y + dirs_y[d]))) {
                continue;
            }
            const int   next = ((ny - search.y0) * search.w) + (nx - search.x0);
            const float g    = search.g[node.index] + costs[d];
            if (search.state[next] == 2 || (search.state[next] == 1 && g >= search.g[next])) {
                continue;
            }
            search.g[next]      = g;
            search.parent[next] = node.index;
            search.state[next]  = 1;
            Path__Push(&search, g + Path__Heuristic(nx, ny, gx, gy), next);
        }
    }

    const int end = found ? goal : best;
    bool      ok  = end != start;
    if (ok) {
        // Walk back from the end to collect the cells
        int length = 0;
        for (int i = end; i != -1; i = search.parent[i]) {
            length++;
        }
        int *cells_x = malloc(sizeof(int) * (size_t)length);
        int *cells_y = malloc(sizeof(int) * (size_t)length);
        FAIL_IF_NULL(cells_x);
        FAIL_IF_NULL(cells_y);
        int k = length;
        for (int i = end; i != -1; i = search.parent[i]) {
            k--;
            cells_x[k] = (i % search.w) + search.x0;
            cells_y[k] = (i / search.w) + search.y0;
        }
        const float end_x = found && Path__Walkable(map, (int)floorf(to_x), (int)floorf(to_y))
                                ? to_x
                                : (float)cells_x[length - 1] + 0.5f;
        const float end_y = found && Path__Walkable(map, (int)floorf(to_x), (int)floorf(to_y))
                                ? to_y
                                : (float)cells_y[length - 1] + 0.5f;
        // cells_x[0] is the start cell
        Path__Smooth(map, from_x, from_y, cells_x + 1, cells_y + 1, length - 1, end_x, end_y, path);
        // Stopped short rather than blocked: out of searching, out of points, or the closest spot is on the edge of
        // the box it looked in (the way on may be outside it)
        const int  ex      = cells_x[length - 1];
        const int  ey      = cells_y[length - 1];
        const bool at_edge = (ex == search.x0 && search.x0 > 0) || (ey == search.y0 && search.y0 > 0) ||
                             (ex == x1 && x1 < map_w - 1) || (ey == y1 && y1 < map_h - 1);
        path->partial = !found && (expansions >= PATH_MAX_EXPANSIONS || at_edge);
        if (path->count == PATH_MAX_POINTS &&
            (path->x[path->count - 1] != end_x || path->y[path->count - 1] != end_y)) {
            path->partial = true;
        }
        free(cells_x);
        free(cells_y);
        ok = path->count > 0;
    }

    free(search.g);
    free(search.parent);
    free(search.state);
    free(search.heap);
    return ok;
}
