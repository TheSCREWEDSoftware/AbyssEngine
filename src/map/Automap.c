#include "Automap.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../drawing/Label.h"
#include "../types/DC6.h"
#include "../types/Palette.h"
#include "MapRenderer.h"
#include "World.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUTOMAP_DC6     "/data/global/ui/automap/MaxiMap.dc6"
#define AUTOMAP_TABLE   "/data/global/excel/AutoMap.txt"
#define LEVELS_TABLE    "/data/global/excel/Levels.txt"
#define LVLTYPES_TABLE  "/data/global/excel/LvlTypes.txt"
#define CELL_W          16 // an automap piece: the tile's diamond is its bottom 16x8, walls stand up from it
#define CELL_H          32
#define CELL_BASE_Y     24
#define TILE_HALF_W     8 // the tile diamond on the automap (a tenth of the 160x80 tile)
#define TILE_HALF_H     4
#define REVEAL_RADIUS   9.0f // tiles around you that count as seen
#define MAX_LEVELS      256
#define MAX_PIECE_ROWS  512
#define MAX_EXIT_LABELS MAP_MAX_EXITS

// One AutoMap.txt row: which automap pieces a range of tiles of one tile set gets
typedef struct AutomapRow {
    int type; // which of the area's tile sets (joined-up maps have several: the town's and the wilderness')
    int orientation;
    int style;
    int first_sequence; // -1 = any
    int last_sequence;  // -1 = to the end
    int cels[4];
    int cel_count;
} AutomapRow;

// What's been seen of one area
typedef struct SeenArea {
    uint32_t seed;
    int      width;
    int      height;
    uint8_t *tiles;
} SeenArea;

struct Automap {
    const Map  *map;
    int         level_id;
    AutomapRow *rows; // this area's tile set
    int         row_count;
    SeenArea    seen[MAX_LEVELS];
    int        *cels; // per tile and layer (floors, then walls): the piece to draw, -1 = none
    int         layers;

    DC6          *dc6;
    SDL_Texture **textures; // per piece, in the current act's colours
    int           texture_count;
    int           texture_act;

    Label *labels[MAX_EXIT_LABELS]; // "To The Cold Plains" at each way out
    Label *name_label;              // other players' names
};

// AutoMap.txt's tile names for the orientations
static int Automap__Orientation(const char *name) {
    static const char *names[] = {"fl",  "wl",  "wr", "wtlr", "wtll", "wtr", "wbl", "wbr", "wld", "wrd",
                                  "wle", "wre", "co", "sh",   "tr",   "rf",  "ld",  "rd",  "fd",  "fi"};
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

// AutoMap.txt names tile sets "1 Town", "5 Ice": the act and the first word of LvlTypes.txt's "Act 1 - Town"
static void Automap__TypeName(const int level_id, char *out, const size_t size) {
    out[0]               = '\0';
    DataTable *levels    = DataTable_Load(LEVELS_TABLE);
    DataTable *lvl_types = DataTable_Load(LVLTYPES_TABLE);
    if (levels != NULL && lvl_types != NULL) {
        int       type = -1;
        const int c_id = DataTable_GetColumnIndex(levels, "Id");
        for (int row = 0; row < DataTable_GetRowCount(levels) && type < 0; row++) {
            if (DataTable_HasValue(levels, row, c_id) && DataTable_GetInt(levels, row, c_id) == level_id) {
                type = DataTable_GetInt(levels, row, DataTable_GetColumnIndex(levels, "LevelType"));
            }
        }
        const int c_type_id = DataTable_GetColumnIndex(lvl_types, "Id");
        const int c_name    = DataTable_GetColumnIndex(lvl_types, "Name");
        for (int row = 0; row < DataTable_GetRowCount(lvl_types) && type >= 0; row++) {
            if (!DataTable_HasValue(lvl_types, row, c_type_id) || DataTable_GetInt(lvl_types, row, c_type_id) != type) {
                continue;
            }
            int  act = 0;
            char rest[64];
            if (sscanf(DataTable_GetString(lvl_types, row, c_name), "Act %d - %63s", &act, rest) == 2) {
                snprintf(out, size, "%d %s", act, rest);
            }
            break;
        }
    }
    DataTable_Destroy(&levels);
    DataTable_Destroy(&lvl_types);
}

// Adds a tile set's rows, tagged with `type`
static void Automap__LoadRows(Automap *automap, const char *type_name, const int type) {
    DataTable *table = DataTable_Load(AUTOMAP_TABLE);
    if (table == NULL || type_name[0] == '\0') {
        DataTable_Destroy(&table);
        return;
    }
    if (automap->rows == NULL) {
        automap->rows = calloc(MAX_PIECE_ROWS, sizeof(AutomapRow));
        FAIL_IF_NULL(automap->rows);
    }
    const int c_level  = DataTable_GetColumnIndex(table, "LevelName");
    const int c_tile   = DataTable_GetColumnIndex(table, "TileName");
    const int c_style  = DataTable_GetColumnIndex(table, "Style");
    const int c_start  = DataTable_GetColumnIndex(table, "StartSequence");
    const int c_end    = DataTable_GetColumnIndex(table, "EndSequence");
    const int c_cel[4] = {DataTable_GetColumnIndex(table, "Cel1"), DataTable_GetColumnIndex(table, "Cel2"),
                          DataTable_GetColumnIndex(table, "Cel3"), DataTable_GetColumnIndex(table, "Cel4")};
    for (int row = 0; row < DataTable_GetRowCount(table) && automap->row_count < MAX_PIECE_ROWS; row++) {
        if (strcmp(DataTable_GetString(table, row, c_level), type_name) != 0) {
            continue;
        }
        AutomapRow *out  = &automap->rows[automap->row_count];
        out->type        = type;
        out->orientation = Automap__Orientation(DataTable_GetString(table, row, c_tile));
        if (out->orientation < 0) {
            continue;
        }
        out->style          = DataTable_GetInt(table, row, c_style);
        out->first_sequence = DataTable_HasValue(table, row, c_start) ? DataTable_GetInt(table, row, c_start) : -1;
        out->last_sequence  = DataTable_HasValue(table, row, c_end) ? DataTable_GetInt(table, row, c_end) : -1;
        out->cel_count      = 0;
        for (int k = 0; k < 4; k++) {
            if (c_cel[k] >= 0 && DataTable_HasValue(table, row, c_cel[k]) &&
                DataTable_GetInt(table, row, c_cel[k]) >= 0) {
                out->cels[out->cel_count++] = DataTable_GetInt(table, row, c_cel[k]);
            }
        }
        if (out->cel_count > 0) {
            automap->row_count++;
        }
    }
    DataTable_Destroy(&table);
}

// The piece for one tile (-1: not on the automap). Tiles with several pieces get one picked by position.
static int Automap__Cel(const Automap *automap, const int type, const DS1Cell *cell, const int orientation,
                        const int tx, const int ty) {
    if (cell == NULL || cell->prop1 == 0 || cell->hidden) {
        return -1;
    }
    for (int r = 0; r < automap->row_count; r++) {
        const AutomapRow *row = &automap->rows[r];
        if (row->type != type || row->orientation != orientation || row->style != cell->style) {
            continue;
        }
        if (row->first_sequence >= 0 && cell->sequence < row->first_sequence) {
            continue;
        }
        if (row->first_sequence >= 0 && row->last_sequence >= 0 && cell->sequence > row->last_sequence) {
            continue;
        }
        const unsigned hash = ((unsigned)tx * 73856093u) ^ ((unsigned)ty * 19349663u);
        return row->cels[hash % (unsigned)row->cel_count];
    }
    return -1;
}

Automap *Automap_Create(void) {
    Automap *automap = calloc(1, sizeof(Automap));
    FAIL_IF_NULL(automap);
    automap->texture_act = -1;
    for (int i = 0; i < MAX_EXIT_LABELS; i++) {
        automap->labels[i] = Label_Create(FONTS_FONT8, PALETTE_STATIC);
        Label_SetAlignment(automap->labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }
    return automap;
}

static void Automap__FreeTextures(Automap *automap) {
    for (int i = 0; i < automap->texture_count; i++) {
        if (automap->textures[i] != NULL) {
            SDL_DestroyTexture(automap->textures[i]);
        }
    }
    free(automap->textures);
    automap->textures      = NULL;
    automap->texture_count = 0;
    automap->texture_act   = -1;
}

void Automap_Destroy(Automap **automap_ref) {
    if (automap_ref == NULL || *automap_ref == NULL) {
        return;
    }
    Automap *automap = *automap_ref;
    Automap__FreeTextures(automap);
    DC6_Destroy(&automap->dc6);
    for (int i = 0; i < MAX_LEVELS; i++) {
        free(automap->seen[i].tiles);
    }
    for (int i = 0; i < MAX_EXIT_LABELS; i++) {
        Label_Destroy(&automap->labels[i]);
    }
    { Label_Destroy(&automap->name_label); }
    free(automap->rows);
    free(automap->cels);
    free(automap);
    *automap_ref = NULL;
}

void Automap_SetArea(Automap *automap, const Map *map, const int level_id, const uint32_t world_seed) {
    automap->map      = map;
    automap->level_id = level_id;
    free(automap->cels);
    automap->cels = NULL;
    if (map == NULL) {
        return;
    }

    // The tile sets' automap pieces: one per area of a joined-up map (the town's and the wilderness'), else the area's
    free(automap->rows);
    automap->rows      = NULL;
    automap->row_count = 0;
    int       area_type[MAP_MAX_AREAS];
    char      type_names[MAP_MAX_AREAS][32];
    const int areas = map->area_count > 0 ? map->area_count : 1;
    for (int a = 0; a < areas; a++) {
        Automap__TypeName(map->area_count > 0 ? map->areas[a].level_id : level_id, type_names[a],
                          sizeof(type_names[a]));
        area_type[a] = a;
        for (int b = 0; b < a; b++) { // areas of the same tile set share their rows
            if (strcmp(type_names[b], type_names[a]) == 0) {
                area_type[a] = area_type[b];
                break;
            }
        }
        if (area_type[a] == a) {
            Automap__LoadRows(automap, type_names[a], a);
        }
    }

    // The pieces per tile, worked out once: floors, then walls
    automap->layers = map->floor_layer_count + map->wall_layer_count;
    const int cells = map->width * map->height;
    automap->cels   = malloc(sizeof(int) * (size_t)cells * (size_t)(automap->layers > 0 ? automap->layers : 1));
    FAIL_IF_NULL(automap->cels);
    for (int c = 0; c < cells; c++) {
        const int     tx   = c % map->width;
        const int     ty   = c / map->width;
        const uint8_t area = map->area_of != NULL ? map->area_of[c] : 0;
        const int     type = area < areas ? area_type[area] : -1;
        int           l    = 0;
        for (int f = 0; f < map->floor_layer_count; f++) {
            automap->cels[(c * automap->layers) + l++] = Automap__Cel(automap, type, &map->floors[f][c], 0, tx, ty);
        }
        for (int w = 0; w < map->wall_layer_count; w++) {
            const DS1Cell *cell                        = &map->walls[w][c];
            automap->cels[(c * automap->layers) + l++] = Automap__Cel(automap, type, cell, cell->orientation, tx, ty);
        }
    }

    // What's been seen here this world
    if (level_id > 0 && level_id < MAX_LEVELS) {
        SeenArea *seen = &automap->seen[level_id];
        if (seen->tiles == NULL || seen->seed != world_seed || seen->width != map->width ||
            seen->height != map->height) {
            free(seen->tiles);
            seen->tiles = calloc((size_t)cells, 1);
            FAIL_IF_NULL(seen->tiles);
            seen->seed   = world_seed;
            seen->width  = map->width;
            seen->height = map->height;
        }
    }

    // Labels for the ways out
    for (int e = 0; e < map->exit_count && e < MAX_EXIT_LABELS; e++) {
        char text[96];
        World_GetWarpText(map->exits[e].target_level, text, sizeof(text));
        Label_SetText(automap->labels[e], text);
    }

    // The pieces in this act's colours
    if (automap->dc6 == NULL) {
        automap->dc6 = DC6_Load(AUTOMAP_DC6);
    }
    if (automap->texture_act != map->act) {
        Automap__FreeTextures(automap);
        if (automap->dc6 != NULL) {
            automap->texture_count = (int)DC6_GetTotalFrameCount(automap->dc6);
            automap->textures      = calloc((size_t)automap->texture_count, sizeof(SDL_Texture *));
            FAIL_IF_NULL(automap->textures);
            automap->texture_act = map->act;
        }
    }
}

static SDL_Texture *Automap__Texture(Automap *automap, const int cel) {
    if (automap->dc6 == NULL || cel < 0 || cel >= automap->texture_count) {
        return NULL;
    }
    if (automap->textures[cel] == NULL) {
        uint32_t width;
        uint32_t height;
        DC6_GetFrameSize(automap->dc6, (uint32_t)cel, &width, &height);
        if (width == 0 || height == 0) {
            return NULL;
        }
        const Palette *palette = Palette_Get(MapView_ActPalette(automap->texture_act));
        const uint8_t *indices = DC6_GetFramePixelData(automap->dc6, (uint32_t)cel);
        uint32_t      *pixels  = malloc(sizeof(uint32_t) * width * height);
        FAIL_IF_NULL(pixels);
        for (uint32_t i = 0; i < width * height; i++) {
            pixels[i] = indices[i] == 0 ? 0 : palette->entries[indices[i]];
        }
        SDL_Texture *texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                                 (int)width, (int)height);
        if (texture != NULL) {
            SDL_UpdateTexture(texture, NULL, pixels, (int)width * 4);
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        free(pixels);
        automap->textures[cel] = texture;
    }
    return automap->textures[cel];
}

static SeenArea *Automap__Seen(Automap *automap) {
    if (automap->map == NULL || automap->level_id <= 0 || automap->level_id >= MAX_LEVELS) {
        return NULL;
    }
    SeenArea *seen = &automap->seen[automap->level_id];
    return seen->tiles != NULL ? seen : NULL;
}

void Automap_Reveal(Automap *automap, const float subtile_x, const float subtile_y) {
    SeenArea *seen = Automap__Seen(automap);
    if (seen == NULL) {
        return;
    }
    const float cx = subtile_x / MAP_SUBTILES;
    const float cy = subtile_y / MAP_SUBTILES;
    const int   r  = (int)ceilf(REVEAL_RADIUS);
    for (int ty = (int)cy - r; ty <= (int)cy + r; ty++) {
        for (int tx = (int)cx - r; tx <= (int)cx + r; tx++) {
            if (tx < 0 || ty < 0 || tx >= seen->width || ty >= seen->height) {
                continue;
            }
            const float dx = ((float)tx + 0.5f) - cx;
            const float dy = ((float)ty + 0.5f) - cy;
            if ((dx * dx) + (dy * dy) <= REVEAL_RADIUS * REVEAL_RADIUS) {
                seen->tiles[(ty * seen->width) + tx] = 1;
            }
        }
    }
}

void Automap_DrawScaled(Automap *automap, const SDL_Rect *view, const float player_x, const float player_y,
                        const AutomapMark *marks, const int mark_count, const int shrink) {
    // shrink: 1 the game's size, 2 half (the corner map)
    const int  half_w = TILE_HALF_W / shrink;
    const int  half_h = TILE_HALF_H / shrink;
    const int  cell_w = CELL_W / shrink;
    const int  cell_h = CELL_H / shrink;
    const int  base_y = CELL_BASE_Y / shrink;
    const Map *map    = automap->map;
    SeenArea  *seen   = Automap__Seen(automap);
    if (map == NULL || seen == NULL || automap->cels == NULL) {
        return;
    }
    SDL_RenderSetClipRect(sdl_renderer, view);

    // Automap pixels: a tile's top corner is at ((tx - ty) * 8, (tx + ty) * 4); the player is in the middle
    const float player_ax = ((player_x - player_y) / MAP_SUBTILES) * half_w;
    const float player_ay = ((player_x + player_y) / MAP_SUBTILES) * half_h;
    const int   origin_x  = view->x + (view->w / 2) - (int)player_ax;
    const int   origin_y  = view->y + (view->h / 2) - (int)player_ay;

    for (int pass = 0; pass < 2; pass++) { // floors first, then walls on top
        const int first = pass == 0 ? 0 : map->floor_layer_count;
        const int last  = pass == 0 ? map->floor_layer_count : automap->layers;
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                const int c = (ty * map->width) + tx;
                if (!seen->tiles[c]) {
                    continue;
                }
                const int sx = origin_x + ((tx - ty) * half_w) - half_w;
                const int sy = origin_y + ((tx + ty) * half_h) - base_y;
                if (sx + cell_w < view->x || sy + cell_h < view->y || sx > view->x + view->w ||
                    sy > view->y + view->h) {
                    continue;
                }
                for (int l = first; l < last; l++) {
                    SDL_Texture *texture = Automap__Texture(automap, automap->cels[(c * automap->layers) + l]);
                    if (texture != NULL) {
                        const SDL_Rect dest = {sx, sy, cell_w, cell_h};
                        SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
                    }
                }
            }
        }
    }

    // The objects' marks you've seen: the piece stands on the object's spot, like a tile's walls
    for (int m = 0; m < mark_count; m++) {
        const int tx = (int)(marks[m].x / MAP_SUBTILES);
        const int ty = (int)(marks[m].y / MAP_SUBTILES);
        if (marks[m].cel < 0) {
            // Another player (your party): a cross in their colour wherever they are, and their name
            const int     px = origin_x + (int)(((marks[m].x - marks[m].y) / MAP_SUBTILES) * half_w);
            const int     py = origin_y + (int)(((marks[m].x + marks[m].y) / MAP_SUBTILES) * half_h);
            const uint8_t r  = (uint8_t)(marks[m].color >> 16);
            const uint8_t g  = (uint8_t)(marks[m].color >> 8);
            const uint8_t b  = (uint8_t)marks[m].color;
            SDL_SetRenderDrawColor(sdl_renderer, r, g, b, 255);
            SDL_RenderDrawLine(sdl_renderer, px - 4, py - 2, px + 4, py + 2);
            SDL_RenderDrawLine(sdl_renderer, px - 4, py + 2, px + 4, py - 2);
            SDL_RenderDrawLine(sdl_renderer, px - 4, py - 1, px + 4, py + 3);
            SDL_RenderDrawLine(sdl_renderer, px - 4, py + 3, px + 4, py - 1);
            if (marks[m].name != NULL && shrink == 1) {
                if (automap->name_label == NULL) {
                    automap->name_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
                    Label_SetAlignment(automap->name_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
                }
                Label_SetText(automap->name_label, marks[m].name);
                Label_SetColor(automap->name_label, r, g, b, 0xFF);
                Label_Draw(automap->name_label, px, py - 10);
            }
            continue;
        }
        if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height || !seen->tiles[(ty * map->width) + tx]) {
            continue;
        }
        SDL_Texture *texture = Automap__Texture(automap, marks[m].cel);
        if (texture == NULL) {
            continue;
        }
        int w;
        int h;
        SDL_QueryTexture(texture, NULL, NULL, &w, &h);
        const int mx         = origin_x + (int)(((marks[m].x - marks[m].y) / MAP_SUBTILES) * half_w);
        const int my         = origin_y + (int)(((marks[m].x + marks[m].y) / MAP_SUBTILES) * half_h);
        w                   /= shrink;
        h                   /= shrink;
        const SDL_Rect dest  = {mx - (w / 2), my - h + half_h, w, h};
        SDL_RenderCopy(sdl_renderer, texture, NULL, &dest);
    }

    // The ways out you've seen, labelled
    for (int e = 0; shrink == 1 && e < map->exit_count && e < MAX_EXIT_LABELS; e++) {
        const MapExit *exit = &map->exits[e];
        const float    ex   = (float)(exit->x0 + exit->x1) / 2.0f;
        const float    ey   = (float)(exit->y0 + exit->y1) / 2.0f;
        const int      tx   = (int)(ex / MAP_SUBTILES);
        const int      ty   = (int)(ey / MAP_SUBTILES);
        if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height || !seen->tiles[(ty * map->width) + tx]) {
            continue;
        }
        const int lx = origin_x + (int)(((ex - ey) / MAP_SUBTILES) * half_w);
        const int ly = origin_y + (int)(((ex + ey) / MAP_SUBTILES) * half_h);
        Label_SetColor(automap->labels[e], 0xFF, 0xFF, 0xFF, 0xFF);
        Label_Draw(automap->labels[e], lx, ly - 6);
    }

    // You: a cross, like the game's
    const int cx = view->x + (view->w / 2);
    const int cy = view->y + (view->h / 2);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 240, 220, 120, 255);
    SDL_RenderDrawLine(sdl_renderer, cx - 4, cy - 2, cx + 4, cy + 2);
    SDL_RenderDrawLine(sdl_renderer, cx - 4, cy + 2, cx + 4, cy - 2);
    SDL_RenderDrawLine(sdl_renderer, cx - 4, cy - 1, cx + 4, cy + 3);
    SDL_RenderDrawLine(sdl_renderer, cx - 4, cy + 3, cx + 4, cy - 1);

    SDL_RenderSetClipRect(sdl_renderer, NULL);
}

void Automap_Draw(Automap *automap, const SDL_Rect *view, const float player_x, const float player_y,
                  const AutomapMark *marks, const int mark_count) {
    Automap_DrawScaled(automap, view, player_x, player_y, marks, mark_count, 1);
}
