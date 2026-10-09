#include "CatalogSprites.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Button.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPRITES_FRAME_TICKS 40 // 25 frames per second, the game's animation rate
#define SPRITES_MAX_SCALE   3.0f
#define SPRITES_INFO_LINES  3

// Preview area and controls, relative to the detail panel
#define SPRITES_NAME_Y      16
#define SPRITES_PREVIEW_Y   32
#define SPRITES_PREVIEW_H   240
#define SPRITES_PREVIEW_PAD 10
#define SPRITES_INFO_Y      284
#define SPRITES_INFO_LINE_H 16
#define SPRITES_BUTTONS_Y   326
#define SPRITES_BUTTONS_Y2  366

typedef enum SpriteButton {
    SPRITE_BUTTON_PREV,
    SPRITE_BUTTON_PLAY,
    SPRITE_BUTTON_NEXT,
    SPRITE_BUTTON_DIRECTION,
    SPRITE_BUTTON_PALETTE,
    SPRITE_BUTTON_COUNT
} SpriteButton;

static const char *sprite_palettes[] = {
    PALETTE_UNITS,  PALETTE_SKY,    PALETTE_FECHAR,  PALETTE_STATIC,    PALETTE_ACT_1,    PALETTE_ACT_2,
    PALETTE_ACT_3,  PALETTE_ACT_4,  PALETTE_ACT_5,   PALETTE_MENU_0,    PALETTE_MENU_1,   PALETTE_MENU_2,
    PALETTE_MENU_3, PALETTE_MENU_4, PALETTE_LOADING, PALETTE_TRADEMARK, PALETTE_END_GAME, PALETTE_END_GAME_2,
};

#define SPRITE_PALETTE_COUNT (int)(sizeof(sprite_palettes) / sizeof(sprite_palettes[0]))

struct CatalogSprites {
    const char **paths; // full MPQ paths (point into the file manager's list)
    char       **names; // shortened names shown in the list
    int          count;
    int          selected;
    Sprite      *sprite;
    int          palette;
    int          direction;
    int          frame; // frame within the current direction
    bool         playing;
    int          tile_columns; // > 0 when the image is stored as a grid of tiles (big UI screens, buttons)
    int          tile_rows;
    int          tiled_width;
    int          tiled_height;
    uint64_t     last_tick;
    Label       *name_label;
    Label       *info_labels[SPRITES_INFO_LINES];
    Label       *empty_label;
    Button      *buttons[SPRITE_BUTTON_COUNT];
    Button      *pause_button; // swapped with the play button while playing
};

static bool Sprites__EndsWith(const char *text, const char *suffix) {
    const size_t text_len   = strlen(text);
    const size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }

    for (size_t i = 0; i < suffix_len; i++) {
        if (tolower((unsigned char)text[text_len - suffix_len + i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

static bool Sprites__StartsWith(const char *text, const char *prefix) {
    for (; *prefix != '\0'; text++, prefix++) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) {
            return false;
        }
    }
    return true;
}

static bool Sprites__Contains(const char *text, const char *needle) {
    for (const char *start = text; *start != '\0'; start++) {
        const char *t = start;
        const char *n = needle;
        while (*t != '\0' && *n != '\0' && tolower((unsigned char)*t) == tolower((unsigned char)*n)) {
            t++;
            n++;
        }
        if (*n == '\0') {
            return true;
        }
    }
    return false;
}

// DC6 files don't store colours; guess the palette the game uses for this folder.
static int Sprites__GuessPalette(const char *path) {
    static const struct {
        const char *folder;
        const char *palette;
    } rules[] = {
        {"\\charselect\\",            PALETTE_FECHAR   },
        {"\\frontend\\amazon\\",      PALETTE_FECHAR   },
        {"\\frontend\\sorceress\\",   PALETTE_FECHAR   },
        {"\\frontend\\necromancer\\", PALETTE_FECHAR   },
        {"\\frontend\\paladin\\",     PALETTE_FECHAR   },
        {"\\frontend\\barbarian\\",   PALETTE_FECHAR   },
        {"\\frontend\\druid\\",       PALETTE_FECHAR   },
        {"\\frontend\\assassin\\",    PALETTE_FECHAR   },
        {"\\loading\\",               PALETTE_LOADING  },
        {"trademark",                 PALETTE_TRADEMARK},
        {"gameselectscreen",          PALETTE_SKY      },
        {"\\ui\\spells\\",            PALETTE_SKY      },
    };

    for (size_t r = 0; r < sizeof(rules) / sizeof(rules[0]); r++) {
        if (Sprites__Contains(path, rules[r].folder)) {
            for (int p = 0; p < SPRITE_PALETTE_COUNT; p++) {
                if (strcmp(sprite_palettes[p], rules[r].palette) == 0) {
                    return p;
                }
            }
        }
    }

    return 0; // units: items, panels, buttons, most of the UI
}

CatalogSprites *CatalogSprites_Create(void) {
    CatalogSprites *sprites = malloc(sizeof(CatalogSprites));
    FAIL_IF_NULL(sprites);
    memset(sprites, 0, sizeof(CatalogSprites));
    sprites->selected = -1;

    const char *const *files      = NULL;
    const int          file_count = FileManager_GetFileList(&files);

    sprites->paths = calloc(file_count > 0 ? file_count : 1, sizeof(char *));
    sprites->names = calloc(file_count > 0 ? file_count : 1, sizeof(char *));
    FAIL_IF_NULL(sprites->paths);
    FAIL_IF_NULL(sprites->names);

    for (int i = 0; i < file_count; i++) {
        if (!Sprites__EndsWith(files[i], ".dc6")) {
            continue;
        }

        // "data\global\ui\FrontEnd\x.dc6" -> "global\ui\FrontEnd\x.dc6"
        const char *name = files[i];
        if (Sprites__StartsWith(name, "data\\")) {
            name += 5;
        }

        sprites->paths[sprites->count] = files[i];
        sprites->names[sprites->count] = strdup(name);
        FAIL_IF_NULL(sprites->names[sprites->count]);
        sprites->count++;
    }
    LOG_INFO("Catalog: %d DC6 sprites", sprites->count);

    sprites->name_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetAlignment(sprites->name_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int i = 0; i < SPRITES_INFO_LINES; i++) {
        sprites->info_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(sprites->info_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }
    sprites->empty_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(sprites->empty_label, "(empty frame)");
    Label_SetAlignment(sprites->empty_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    static const char *button_names[SPRITE_BUTTON_COUNT] = {"Prev", "Play", "Next", "Direction", "Palette"};
    for (int i = 0; i < SPRITE_BUTTON_COUNT; i++) {
        sprites->buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, button_names[i]);
    }
    sprites->pause_button = Button_Create(BUTTON_TYPE_MEDIUM, "Pause");

    return sprites;
}

void CatalogSprites_Destroy(CatalogSprites **sprites_ref) {
    if (sprites_ref == NULL || *sprites_ref == NULL) {
        return;
    }
    CatalogSprites *sprites = *sprites_ref;

    if (sprites->sprite != NULL) {
        Sprite_Destroy(&sprites->sprite);
    }
    for (int i = 0; i < sprites->count; i++) {
        free(sprites->names[i]);
    }
    free(sprites->names);
    free(sprites->paths);

    Label_Destroy(&sprites->name_label);
    for (int i = 0; i < SPRITES_INFO_LINES; i++) {
        Label_Destroy(&sprites->info_labels[i]);
    }
    Label_Destroy(&sprites->empty_label);
    for (int i = 0; i < SPRITE_BUTTON_COUNT; i++) {
        Button_Destroy(&sprites->buttons[i]);
    }
    Button_Destroy(&sprites->pause_button);

    free(sprites);
    *sprites_ref = NULL;
}

int CatalogSprites_GetCount(const CatalogSprites *sprites) { return sprites->count; }

const char *CatalogSprites_GetName(const CatalogSprites *sprites, const int index) { return sprites->names[index]; }

static int Sprites__FramesPerDirection(const CatalogSprites *sprites) {
    if (sprites->tile_columns > 0) {
        return 1; // shown as one stitched picture
    }
    const int total      = (int)Sprite_GetFrameCount(sprites->sprite);
    const int directions = (int)Sprite_GetDirectionCount(sprites->sprite);
    return directions > 0 ? total / directions : total;
}

static void Sprites__UpdateInfo(CatalogSprites *sprites) {
    char line[128];

    if (sprites->sprite == NULL) {
        for (int i = 0; i < SPRITES_INFO_LINES; i++) {
            Label_SetText(sprites->info_labels[i], "");
        }
        return;
    }

    const int frames_per_direction = Sprites__FramesPerDirection(sprites);
    const int directions           = (int)Sprite_GetDirectionCount(sprites->sprite);
    const int frame_index          = (sprites->direction * frames_per_direction) + sprites->frame;
    int       width                = 0;
    int       height               = 0;
    int       offset_x             = 0;
    int       offset_y             = 0;
    Sprite_GetFrameSize(sprites->sprite, frame_index, &width, &height);
    Sprite_GetFrameOffset(sprites->sprite, frame_index, &offset_x, &offset_y);

    if (sprites->tile_columns > 0) {
        snprintf(line, sizeof(line), "Tiled image: %d x %d tiles", sprites->tile_columns, sprites->tile_rows);
        Label_SetText(sprites->info_labels[0], line);
        snprintf(line, sizeof(line), "Size %dx%d", sprites->tiled_width, sprites->tiled_height);
        Label_SetText(sprites->info_labels[1], line);
    } else {
        snprintf(line, sizeof(line), "Frame %d of %d   Direction %d of %d", sprites->frame + 1, frames_per_direction,
                 sprites->direction + 1, directions);
        Label_SetText(sprites->info_labels[0], line);
        snprintf(line, sizeof(line), "Size %dx%d   Offset %d,%d", width, height, offset_x, offset_y);
        Label_SetText(sprites->info_labels[1], line);
    }

    snprintf(line, sizeof(line), "Palette: %s", sprite_palettes[sprites->palette]);
    Label_SetText(sprites->info_labels[2], line);

    for (int i = 0; i < SPRITES_INFO_LINES; i++) {
        Label_SetColor(sprites->info_labels[i], 0xD0, 0xD0, 0xD0, 0xFF);
    }
}

// Large UI images are cut into 256 pixel wide tiles, stored row by row as "frames". Detect that layout so the
// picture can be shown whole instead of playing the tiles as an animation.
static void Sprites__DetectTiles(CatalogSprites *sprites) {
    sprites->tile_columns = 0;
    sprites->tile_rows    = 0;

    const int frames = (int)Sprite_GetFrameCount(sprites->sprite);
    int       width  = 0;
    Sprite_GetFrameSize(sprites->sprite, 0, &width, NULL);
    if (frames < 2 || Sprite_GetDirectionCount(sprites->sprite) != 1 || width != 256) {
        return;
    }

    int columns = 0;
    int row_w   = 0;
    for (int f = 0; f < frames; f++) {
        int w = 0;
        Sprite_GetFrameSize(sprites->sprite, f, &w, NULL);
        row_w += w;
        columns++;
        if (w < 256) {
            break;
        }
    }
    if (frames % columns != 0) {
        return;
    }

    int column_h = 0;
    for (int f = 0; f < frames; f += columns) {
        int h = 0;
        Sprite_GetFrameSize(sprites->sprite, f, NULL, &h);
        column_h += h;
    }

    sprites->tile_columns = columns;
    sprites->tile_rows    = frames / columns;
    sprites->tiled_width  = row_w;
    sprites->tiled_height = column_h;
}

static void Sprites__Load(CatalogSprites *sprites) {
    if (sprites->sprite != NULL) {
        Sprite_Destroy(&sprites->sprite);
    }
    sprites->tile_columns = 0;

    if (sprites->selected < 0 || sprites->selected >= sprites->count) {
        return;
    }

    sprites->sprite = Sprite_Create(sprites->paths[sprites->selected], sprite_palettes[sprites->palette]);
    if (Sprite_GetFrameCount(sprites->sprite) == 0) {
        Sprite_Destroy(&sprites->sprite);
        return;
    }
    Sprites__DetectTiles(sprites);
}

void CatalogSprites_Select(CatalogSprites *sprites, const int index) {
    if (index == sprites->selected) {
        return;
    }

    sprites->selected  = index;
    sprites->direction = 0;
    sprites->frame     = 0;
    sprites->last_tick = SDL_GetTicks64();

    if (index < 0 || index >= sprites->count) {
        if (sprites->sprite != NULL) {
            Sprite_Destroy(&sprites->sprite);
        }
        Label_SetText(sprites->name_label, "");
        Sprites__UpdateInfo(sprites);
        return;
    }

    sprites->palette = Sprites__GuessPalette(sprites->paths[index]);
    Sprites__Load(sprites);

    // Animations start playing; single images stay still.
    sprites->playing = sprites->sprite != NULL && Sprites__FramesPerDirection(sprites) > 1;

    const char *slash = strrchr(sprites->paths[index], '\\');
    Label_SetText(sprites->name_label, slash != NULL ? slash + 1 : sprites->paths[index]);
    Label_SetColor(sprites->name_label, 0xC7, 0xB3, 0x77, 0xFF);
    Sprites__UpdateInfo(sprites);
}

static void Sprites__Step(CatalogSprites *sprites, const int amount) {
    const int frames = Sprites__FramesPerDirection(sprites);
    if (frames <= 0) {
        return;
    }

    sprites->frame = ((sprites->frame + amount) % frames + frames) % frames;
    Sprites__UpdateInfo(sprites);
}

void CatalogSprites_UpdateDetail(CatalogSprites *sprites) {
    if (sprites->sprite == NULL) {
        return;
    }

    Button *play_button = sprites->playing ? sprites->pause_button : sprites->buttons[SPRITE_BUTTON_PLAY];

    if (Button_Update(sprites->buttons[SPRITE_BUTTON_PREV])) {
        sprites->playing = false;
        Sprites__Step(sprites, -1);
    }
    if (Button_Update(sprites->buttons[SPRITE_BUTTON_NEXT])) {
        sprites->playing = false;
        Sprites__Step(sprites, 1);
    }
    if (Button_Update(play_button)) {
        sprites->playing   = !sprites->playing;
        sprites->last_tick = SDL_GetTicks64();
    }
    if (Button_Update(sprites->buttons[SPRITE_BUTTON_DIRECTION])) {
        sprites->direction = (sprites->direction + 1) % (int)Sprite_GetDirectionCount(sprites->sprite);
        sprites->frame     = 0;
        Sprites__UpdateInfo(sprites);
    }
    if (Button_Update(sprites->buttons[SPRITE_BUTTON_PALETTE])) {
        sprites->palette = (sprites->palette + 1) % SPRITE_PALETTE_COUNT;
        Sprites__Load(sprites);
        Sprites__UpdateInfo(sprites);
        if (sprites->sprite == NULL) {
            return;
        }
    }

    if (sprites->playing) {
        const uint64_t now = SDL_GetTicks64();
        while (now - sprites->last_tick >= SPRITES_FRAME_TICKS) {
            sprites->last_tick += SPRITES_FRAME_TICKS;
            Sprites__Step(sprites, 1);
        }
    }
}

void CatalogSprites_DrawDetail(CatalogSprites *sprites, const SDL_Rect *panel) {
    const int center_x = panel->x + (panel->w / 2);
    Label_Draw(sprites->name_label, center_x, panel->y + SPRITES_NAME_Y);

    // Checkerboard behind the image so transparent and dark pixels are visible
    const SDL_Rect preview = {panel->x + SPRITES_PREVIEW_PAD, panel->y + SPRITES_PREVIEW_Y,
                              panel->w - (2 * SPRITES_PREVIEW_PAD), SPRITES_PREVIEW_H};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_NONE);
    for (int y = 0; y < preview.h; y += 16) {
        for (int x = 0; x < preview.w; x += 16) {
            const bool     dark = ((x / 16) + (y / 16)) % 2 == 0;
            const SDL_Rect cell = {preview.x + x, preview.y + y, (x + 16 <= preview.w) ? 16 : preview.w - x,
                                   (y + 16 <= preview.h) ? 16 : preview.h - y};
            SDL_SetRenderDrawColor(sdl_renderer, dark ? 40 : 56, dark ? 40 : 56, dark ? 44 : 60, 255);
            SDL_RenderFillRect(sdl_renderer, &cell);
        }
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

    if (sprites->sprite == NULL) {
        return;
    }

    const int frame_index = (sprites->direction * Sprites__FramesPerDirection(sprites)) + sprites->frame;
    int       width       = sprites->tile_columns > 0 ? sprites->tiled_width : 0;
    int       height      = sprites->tile_columns > 0 ? sprites->tiled_height : 0;
    if (sprites->tile_columns == 0) {
        Sprite_GetFrameSize(sprites->sprite, frame_index, &width, &height);
    }

    if (width == 0 || height == 0) {
        Label_SetColor(sprites->empty_label, 0x90, 0x90, 0x90, 0xFF);
        Label_Draw(sprites->empty_label, center_x, preview.y + (preview.h / 2));
    } else {
        // Shrink big images to fit; enlarge small ones (whole steps keep the pixels crisp)
        float scale = (float)(preview.w - 8) / (float)width;
        if ((float)(preview.h - 8) / (float)height < scale) {
            scale = (float)(preview.h - 8) / (float)height;
        }
        if (scale >= 1.0f) {
            scale = (float)(int)scale;
            if (scale > SPRITES_MAX_SCALE) {
                scale = SPRITES_MAX_SCALE;
            }
        }

        const int draw_w   = (int)((float)width * scale);
        const int draw_h   = (int)((float)height * scale);
        const int origin_x = preview.x + ((preview.w - draw_w) / 2);
        const int origin_y = preview.y + ((preview.h - draw_h) / 2);
        SDL_RenderSetClipRect(sdl_renderer, &preview);

        if (sprites->tile_columns > 0) {
            int tile_y = 0;
            for (int row = 0; row < sprites->tile_rows; row++) {
                int tile_x = 0;
                int row_h  = 0;
                for (int column = 0; column < sprites->tile_columns; column++) {
                    const int tile = (row * sprites->tile_columns) + column;
                    int       w    = 0;
                    int       h    = 0;
                    Sprite_GetFrameSize(sprites->sprite, tile, &w, &h);
                    // Edges from the running totals, so neighbouring tiles meet without gaps when scaled
                    const int x0 = origin_x + (int)((float)tile_x * scale);
                    const int y0 = origin_y + (int)((float)tile_y * scale);
                    const int x1 = origin_x + (int)((float)(tile_x + w) * scale);
                    const int y1 = origin_y + (int)((float)(tile_y + h) * scale);
                    Sprite_DrawStretched(sprites->sprite, tile, x0, y0, x1 - x0, y1 - y0);
                    tile_x += w;
                    row_h   = h > row_h ? h : row_h;
                }
                tile_y += row_h;
            }
        } else {
            Sprite_DrawScaled(sprites->sprite, frame_index, origin_x, origin_y, scale);
        }

        SDL_RenderSetClipRect(sdl_renderer, NULL);
    }

    int y = panel->y + SPRITES_INFO_Y;
    for (int i = 0; i < SPRITES_INFO_LINES; i++) {
        Label_Draw(sprites->info_labels[i], center_x, y);
        y += SPRITES_INFO_LINE_H;
    }

    // Controls: [Prev][Play/Pause][Next] / [Direction][Palette]
    Button   *play_button = sprites->playing ? sprites->pause_button : sprites->buttons[SPRITE_BUTTON_PLAY];
    const int left        = panel->x + ((panel->w - (3 * 128) - 8) / 2);
    Button_SetPosition(sprites->buttons[SPRITE_BUTTON_PREV], left, panel->y + SPRITES_BUTTONS_Y);
    Button_SetPosition(play_button, left + 132, panel->y + SPRITES_BUTTONS_Y);
    Button_SetPosition(sprites->buttons[SPRITE_BUTTON_NEXT], left + 264, panel->y + SPRITES_BUTTONS_Y);
    Button_SetPosition(sprites->buttons[SPRITE_BUTTON_DIRECTION], left + 66, panel->y + SPRITES_BUTTONS_Y2);
    Button_SetPosition(sprites->buttons[SPRITE_BUTTON_PALETTE], left + 198, panel->y + SPRITES_BUTTONS_Y2);

    Button_SetEnabled(sprites->buttons[SPRITE_BUTTON_DIRECTION], Sprite_GetDirectionCount(sprites->sprite) > 1);
    const bool animated = Sprites__FramesPerDirection(sprites) > 1;
    Button_SetEnabled(sprites->buttons[SPRITE_BUTTON_PREV], animated);
    Button_SetEnabled(sprites->buttons[SPRITE_BUTTON_NEXT], animated);
    Button_SetEnabled(play_button, animated);

    Button_Draw(sprites->buttons[SPRITE_BUTTON_PREV]);
    Button_Draw(play_button);
    Button_Draw(sprites->buttons[SPRITE_BUTTON_NEXT]);
    Button_Draw(sprites->buttons[SPRITE_BUTTON_DIRECTION]);
    Button_Draw(sprites->buttons[SPRITE_BUTTON_PALETTE]);
}
