#include "SceneCatalog.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "../drawing/Button.h"
#include "../drawing/Cursor.h"
#include "../drawing/Label.h"
#include "../drawing/ListBox.h"
#include "../drawing/Sprite.h"
#include "../drawing/TextBox.h"
#include "../managers/AudioManager.h"
#include "CatalogClasses.h"
#include "CatalogSprites.h"
#include "SceneMainMenu.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATALOG_MAX_STAT_LINES 14
#define CATALOG_TEXT_LEN       128

#define CATALOG_LIST_X      40
#define CATALOG_SEARCH_Y    112
#define CATALOG_SEARCH_H    22
#define CATALOG_LIST_Y      140
#define CATALOG_LIST_WIDTH  300
#define CATALOG_LIST_ROWS   21
#define CATALOG_LIST_ROW_H  18
#define CATALOG_DETAIL_X    360
#define CATALOG_DETAIL_Y    112
#define CATALOG_DETAIL_W    400
#define CATALOG_DETAIL_H    414
#define CATALOG_ICON_BOX_H  130
#define CATALOG_STAT_LINE_H 20

typedef enum CatalogCategory {
    CATALOG_CATEGORY_WEAPONS,
    CATALOG_CATEGORY_ARMOR,
    CATALOG_CATEGORY_CLASSES,
    CATALOG_CATEGORY_SPRITES,
    CATALOG_CATEGORY_COUNT
} CatalogCategory;

typedef struct CatalogItem {
    char name[CATALOG_TEXT_LEN];
    char icon_path[CATALOG_TEXT_LEN];
    char stat_lines[CATALOG_MAX_STAT_LINES][CATALOG_TEXT_LEN];
    int  stat_line_count;
} CatalogItem;

typedef struct CatalogItemList {
    CatalogItem *items;
    int          count;
} CatalogItemList;

typedef struct SceneCatalog {
    Sprite         *background_sprite;
    Label          *title_label;
    Label          *count_label;
    Label          *name_label;
    Label          *stat_labels[CATALOG_MAX_STAT_LINES];
    Label          *empty_label;
    Sprite         *icon_sprite;
    int             icon_width;
    int             icon_height;
    Button         *category_buttons[CATALOG_CATEGORY_COUNT];
    Button         *back_button;
    ListBox        *list_box;
    TextBox        *search_box;
    CatalogClasses *classes;
    CatalogSprites *sprites;
    CatalogItemList categories[CATALOG_CATEGORY_COUNT]; // items for the weapon/armor tabs
    CatalogCategory current_category;
    int            *visible; // list row -> item/class index, after the search filter
    int             visible_count;
    int             shown_item; // item/class index shown in the detail panel, -1 for none
} SceneCatalog;

DEFINE_SCENE_CALLBACKS(Catalog);

static void Catalog__LoadCategory(CatalogItemList *list, CatalogCategory category);
static void Catalog__SelectCategory(SceneCatalog *catalog, CatalogCategory category);
static void Catalog__RefreshList(SceneCatalog *catalog);
static void Catalog__ShowItem(SceneCatalog *catalog, int index);
static void Catalog__OnBackClicked(void *user_data);

// ---------------------------------------------------------------------------------------------------------------------
// Text helpers

// Game string with trailing whitespace removed, or the fallback when the key does not exist.
static void Catalog__GameString(char *out, const size_t out_len, const char *key, const char *fallback) {
    const char *value = StringTable_Find(key);
    snprintf(out, out_len, "%s", value != NULL ? value : fallback);

    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }
}

// Copies only characters the in-game fonts can draw; drops colour codes and line breaks.
static void Catalog__Sanitize(char *out, const size_t out_len, const char *in) {
    size_t o = 0;
    for (const unsigned char *ch = (const unsigned char *)in; *ch != '\0' && o + 1 < out_len; ch++) {
        if (*ch == 0xFF && ch[1] == 'c' && ch[2] != '\0') { // \xFFc<n> colour code
            ch += 2;
            continue;
        }
        if (*ch == '\n' || *ch == '\r') {
            out[o++] = ' ';
            continue;
        }
        if (*ch >= 0x20 && *ch < 0x7F) {
            out[o++] = (char)*ch;
        }
    }
    out[o] = '\0';
}

static void Catalog__AddLine(CatalogItem *item, const char *format, ...) {
    if (item->stat_line_count >= CATALOG_MAX_STAT_LINES) {
        return;
    }

    char    raw[CATALOG_TEXT_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(raw, sizeof(raw), format, args);
    va_end(args);

    Catalog__Sanitize(item->stat_lines[item->stat_line_count++], CATALOG_TEXT_LEN, raw);
}

static void Catalog__AddRange(CatalogItem *item, const char *label_key, const char *label_fallback, const int min,
                              const int max) {
    char label[64];
    Catalog__GameString(label, sizeof(label), label_key, label_fallback);

    if (min == max) {
        Catalog__AddLine(item, "%s %d", label, min);
    } else {
        char to[16];
        Catalog__GameString(to, sizeof(to), "ItemStast1k", "to");
        Catalog__AddLine(item, "%s %d %s %d", label, min, to, max);
    }
}

static void Catalog__AddValue(CatalogItem *item, const char *label_key, const char *label_fallback, const int value) {
    char label[64];
    Catalog__GameString(label, sizeof(label), label_key, label_fallback);
    Catalog__AddLine(item, "%s %d", label, value);
}

// ---------------------------------------------------------------------------------------------------------------------
// Data loading

typedef struct CatalogColumns {
    int name;
    int name_str;
    int code;
    int spawnable;
    int min_damage;
    int max_damage;
    int two_hand_min;
    int two_hand_max;
    int two_handed;
    int one_or_two_handed;
    int throw_min;
    int throw_max;
    int throwable;
    int stackable;
    int max_stack;
    int min_ac;
    int max_ac;
    int block;
    int req_str;
    int req_dex;
    int level_req;
    int level;
    int durability;
    int no_durability;
    int sockets;
    int speed;
    int inv_file;
    int inv_width;
    int inv_height;
} CatalogColumns;

static CatalogColumns Catalog__FindColumns(const DataTable *table) {
    CatalogColumns c = {
        .name              = DataTable_GetColumnIndex(table, "name"),
        .name_str          = DataTable_GetColumnIndex(table, "namestr"),
        .code              = DataTable_GetColumnIndex(table, "code"),
        .spawnable         = DataTable_GetColumnIndex(table, "spawnable"),
        .min_damage        = DataTable_GetColumnIndex(table, "mindam"),
        .max_damage        = DataTable_GetColumnIndex(table, "maxdam"),
        .two_hand_min      = DataTable_GetColumnIndex(table, "2handmindam"),
        .two_hand_max      = DataTable_GetColumnIndex(table, "2handmaxdam"),
        .two_handed        = DataTable_GetColumnIndex(table, "2handed"),
        .one_or_two_handed = DataTable_GetColumnIndex(table, "1or2handed"),
        .throw_min         = DataTable_GetColumnIndex(table, "minmisdam"),
        .throw_max         = DataTable_GetColumnIndex(table, "maxmisdam"),
        .throwable         = DataTable_GetColumnIndex(table, "throwable"),
        .stackable         = DataTable_GetColumnIndex(table, "stackable"),
        .max_stack         = DataTable_GetColumnIndex(table, "maxstack"),
        .min_ac            = DataTable_GetColumnIndex(table, "minac"),
        .max_ac            = DataTable_GetColumnIndex(table, "maxac"),
        .block             = DataTable_GetColumnIndex(table, "block"),
        .req_str           = DataTable_GetColumnIndex(table, "reqstr"),
        .req_dex           = DataTable_GetColumnIndex(table, "reqdex"),
        .level_req         = DataTable_GetColumnIndex(table, "levelreq"),
        .level             = DataTable_GetColumnIndex(table, "level"),
        .durability        = DataTable_GetColumnIndex(table, "durability"),
        .no_durability     = DataTable_GetColumnIndex(table, "nodurability"),
        .sockets           = DataTable_GetColumnIndex(table, "gemsockets"),
        .speed             = DataTable_GetColumnIndex(table, "speed"),
        .inv_file          = DataTable_GetColumnIndex(table, "invfile"),
        .inv_width         = DataTable_GetColumnIndex(table, "invwidth"),
        .inv_height        = DataTable_GetColumnIndex(table, "invheight"),
    };
    return c;
}

static void Catalog__ResolveName(const DataTable *table, const CatalogColumns *c, const int row, CatalogItem *item) {
    // Lord of Destruction tables have a "namestr" string key; the classic ones are keyed by the item code.
    const char *name = NULL;
    if (c->name_str >= 0) {
        name = StringTable_Find(DataTable_GetString(table, row, c->name_str));
    }
    if (name == NULL) {
        name = StringTable_Find(DataTable_GetString(table, row, c->code));
    }
    if (name == NULL) {
        name = DataTable_GetString(table, row, c->name);
    }

    Catalog__Sanitize(item->name, sizeof(item->name), name);
}

static void Catalog__AddCommonStats(const DataTable *t, const CatalogColumns *c, const int row, CatalogItem *item) {
#define CELL_INT(col) DataTable_GetInt(t, row, (col))

    if (CELL_INT(c->req_str) > 0) {
        Catalog__AddValue(item, "ItemStats1e", "Required Strength:", CELL_INT(c->req_str));
    }
    if (c->req_dex >= 0 && CELL_INT(c->req_dex) > 0) {
        Catalog__AddValue(item, "ItemStats1f", "Required Dexterity:", CELL_INT(c->req_dex));
    }
    if (c->level_req >= 0 && CELL_INT(c->level_req) > 0) {
        Catalog__AddValue(item, "ItemStats1p", "Required Level:", CELL_INT(c->level_req));
    }

    // Stacked throwing items (javelins, knives) use quantity instead of durability.
    const bool is_stacked    = c->stackable >= 0 && CELL_INT(c->stackable) == 1;
    const bool no_durability = c->no_durability >= 0 && CELL_INT(c->no_durability) == 1;
    if (!(is_stacked && no_durability) && CELL_INT(c->durability) > 0) {
        Catalog__AddValue(item, "ItemStats1d", "Durability:", CELL_INT(c->durability));
    }

    if (c->stackable >= 0 && CELL_INT(c->stackable) == 1 && CELL_INT(c->max_stack) > 0) {
        Catalog__AddValue(item, "ItemStats1i", "Quantity:", CELL_INT(c->max_stack));
    }
    if (CELL_INT(c->sockets) > 0) {
        Catalog__AddLine(item, "Max Sockets: %d", CELL_INT(c->sockets));
    }
    Catalog__AddLine(item, "Quality Level: %d", CELL_INT(c->level));
    Catalog__AddLine(item, "Inventory Size: %dx%d", CELL_INT(c->inv_width), CELL_INT(c->inv_height));
    Catalog__AddLine(item, "Code: %s", DataTable_GetString(t, row, c->code));

#undef CELL_INT
}

static void Catalog__LoadCategory(CatalogItemList *list, const CatalogCategory category) {
    const char *path = category == CATALOG_CATEGORY_WEAPONS ? EXCEL_WEAPONS : EXCEL_ARMOR;
    DataTable  *t    = DataTable_Load(path);
    const int   rows = DataTable_GetRowCount(t);

    const CatalogColumns c = Catalog__FindColumns(t);

    list->items = calloc(rows > 0 ? rows : 1, sizeof(CatalogItem));
    list->count = 0;
    FAIL_IF_NULL(list->items);

#define CELL_INT(col) DataTable_GetInt(t, row, (col))
#define HAS(col)      ((col) >= 0 && DataTable_HasValue(t, row, (col)))

    for (int row = 0; row < rows; row++) {
        // Skip separator rows ("Expansion"), unused rows and items that never drop (quest/unfinished items).
        if (!HAS(c.code) || CELL_INT(c.spawnable) != 1) {
            continue;
        }

        CatalogItem *item = &list->items[list->count++];
        Catalog__ResolveName(t, &c, row, item);
        snprintf(item->icon_path, sizeof(item->icon_path), "/data/global/items/%s.dc6",
                 DataTable_GetString(t, row, c.inv_file));

        if (category == CATALOG_CATEGORY_WEAPONS) {
            const bool two_handed        = HAS(c.two_handed) && CELL_INT(c.two_handed) == 1;
            const bool one_or_two_handed = HAS(c.one_or_two_handed) && CELL_INT(c.one_or_two_handed) == 1;

            if ((!two_handed || one_or_two_handed) && CELL_INT(c.max_damage) > 0) {
                Catalog__AddRange(item, "ItemStats1l", "One-Hand Damage:", CELL_INT(c.min_damage),
                                  CELL_INT(c.max_damage));
            }
            if (two_handed && CELL_INT(c.two_hand_max) > 0) {
                Catalog__AddRange(item, "ItemStats1m", "Two-Hand Damage:", CELL_INT(c.two_hand_min),
                                  CELL_INT(c.two_hand_max));
            }
            // Bows also fill in the missile damage columns, but only throwing weapons show it as throw damage.
            if (HAS(c.throwable) && CELL_INT(c.throwable) == 1 && CELL_INT(c.throw_max) > 0) {
                Catalog__AddRange(item, "ItemStats1n", "Throw Damage:", CELL_INT(c.throw_min), CELL_INT(c.throw_max));
            }
            if (CELL_INT(c.speed) != 0) {
                Catalog__AddLine(item, "Speed Modifier: %+d", CELL_INT(c.speed));
            }
        } else {
            Catalog__AddRange(item, "ItemStats1h", "Defense:", CELL_INT(c.min_ac), CELL_INT(c.max_ac));
            if (HAS(c.block) && CELL_INT(c.block) > 0) {
                Catalog__AddLine(item, "Chance to Block: %d%%", CELL_INT(c.block));
            }
            if (CELL_INT(c.max_damage) > 0) {
                Catalog__AddRange(item, "ItemStats1o", "Damage:", CELL_INT(c.min_damage), CELL_INT(c.max_damage));
            }
        }

        Catalog__AddCommonStats(t, &c, row, item);
    }

#undef HAS
#undef CELL_INT

    DataTable_Destroy(&t);
    LOG_INFO("Catalog: %d items in '%s'", list->count, path);
}

// ---------------------------------------------------------------------------------------------------------------------
// Scene

void *Catalog_Create(void) {
    Cursor_SetVisible(true);
    AudioManager_PlayMusic(MUSIC_TITLE, true);

    SceneCatalog *result = malloc(sizeof(SceneCatalog));
    FAIL_IF_NULL(result);
    memset(result, 0, sizeof(SceneCatalog));
    result->shown_item = -1;

    if (FileManager_FileExists(GAME_SELECT_SCREEN)) {
        result->background_sprite = Sprite_Create(GAME_SELECT_SCREEN, PALETTE_SKY);
    }

    result->title_label = Label_Create(FONTS_FONT30, PALETTE_STATIC);
    Label_SetText(result->title_label, "Catalog");
    Label_SetAlignment(result->title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_SetColor(result->title_label, 0xC7, 0xB3, 0x77, 0xFF);

    result->count_label = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    Label_SetAlignment(result->count_label, LABEL_ALIGN_END, LABEL_ALIGN_CENTER);

    result->name_label = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetAlignment(result->name_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    for (int i = 0; i < CATALOG_MAX_STAT_LINES; i++) {
        result->stat_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(result->stat_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }

    result->empty_label = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    Label_SetText(result->empty_label, "No picture");
    Label_SetAlignment(result->empty_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    static const char *category_names[CATALOG_CATEGORY_COUNT] = {"Weapons", "Armor", "Classes", "Sprites"};
    for (int i = 0; i < CATALOG_CATEGORY_COUNT; i++) {
        result->category_buttons[i] = Button_Create(BUTTON_TYPE_MEDIUM, category_names[i]);
        Button_SetPosition(result->category_buttons[i], CATALOG_LIST_X + (i * 136), 68);
        if (i == CATALOG_CATEGORY_WEAPONS || i == CATALOG_CATEGORY_ARMOR) {
            Catalog__LoadCategory(&result->categories[i], (CatalogCategory)i);
        }
    }
    result->classes = CatalogClasses_Create();
    result->sprites = CatalogSprites_Create();

    result->back_button = Button_Create(BUTTON_TYPE_MEDIUM, "Back");
    Button_SetPosition(result->back_button, CATALOG_LIST_X, 540);
    Button_SetClickHandler(result->back_button, Catalog__OnBackClicked, result);

    result->search_box = TextBox_Create(FONTS_FONT16, CATALOG_LIST_X, CATALOG_SEARCH_Y, CATALOG_LIST_WIDTH,
                                        CATALOG_SEARCH_H, "Type to search...");
    TextBox_Focus(result->search_box);

    result->list_box = ListBox_Create(FONTS_FONT16, CATALOG_LIST_X, CATALOG_LIST_Y, CATALOG_LIST_WIDTH,
                                      CATALOG_LIST_ROWS, CATALOG_LIST_ROW_H);

    Catalog__SelectCategory(result, CATALOG_CATEGORY_WEAPONS);
    return result;
}

static int Catalog__SourceCount(const SceneCatalog *catalog) {
    if (catalog->current_category == CATALOG_CATEGORY_CLASSES) {
        return CatalogClasses_GetCount(catalog->classes);
    }
    if (catalog->current_category == CATALOG_CATEGORY_SPRITES) {
        return CatalogSprites_GetCount(catalog->sprites);
    }
    return catalog->categories[catalog->current_category].count;
}

static const char *Catalog__SourceName(const SceneCatalog *catalog, const int index) {
    if (catalog->current_category == CATALOG_CATEGORY_CLASSES) {
        return CatalogClasses_GetName(catalog->classes, index);
    }
    if (catalog->current_category == CATALOG_CATEGORY_SPRITES) {
        return CatalogSprites_GetName(catalog->sprites, index);
    }
    return catalog->categories[catalog->current_category].items[index].name;
}

// Case-insensitive "contains".
static bool Catalog__Matches(const char *text, const char *query) {
    if (query[0] == '\0') {
        return true;
    }

    for (const char *start = text; *start != '\0'; start++) {
        const char *t = start;
        const char *q = query;
        while (*t != '\0' && *q != '\0' && tolower((unsigned char)*t) == tolower((unsigned char)*q)) {
            t++;
            q++;
        }
        if (*q == '\0') {
            return true;
        }
    }

    return false;
}

static void Catalog__RefreshList(SceneCatalog *catalog) {
    const int   source_count = Catalog__SourceCount(catalog);
    const char *query        = TextBox_GetText(catalog->search_box);

    free(catalog->visible);
    catalog->visible       = calloc(source_count > 0 ? source_count : 1, sizeof(int));
    catalog->visible_count = 0;
    FAIL_IF_NULL(catalog->visible);

    const char **names = calloc(source_count > 0 ? source_count : 1, sizeof(char *));
    FAIL_IF_NULL(names);

    for (int i = 0; i < source_count; i++) {
        const char *name = Catalog__SourceName(catalog, i);
        if (Catalog__Matches(name, query)) {
            names[catalog->visible_count]            = name;
            catalog->visible[catalog->visible_count] = i;
            catalog->visible_count++;
        }
    }

    ListBox_SetItems(catalog->list_box, names, catalog->visible_count);
    free(names);

    char count_text[48];
    if (query[0] != '\0') {
        snprintf(count_text, sizeof(count_text), "%d of %d", catalog->visible_count, source_count);
    } else {
        snprintf(count_text, sizeof(count_text), "%d entries", source_count);
    }
    Label_SetText(catalog->count_label, count_text);
    Label_SetColor(catalog->count_label, 0xA0, 0xA0, 0xA0, 0xFF);

    catalog->shown_item = -2; // force a refresh
    const int row       = ListBox_GetSelectedIndex(catalog->list_box);
    Catalog__ShowItem(catalog, row >= 0 ? catalog->visible[row] : -1);
}

static void Catalog__SelectCategory(SceneCatalog *catalog, const CatalogCategory category) {
    catalog->current_category = category;

    for (int i = 0; i < CATALOG_CATEGORY_COUNT; i++) {
        Button_SetToggled(catalog->category_buttons[i], i == (int)category);
    }

    TextBox_Clear(catalog->search_box); // a search in one tab rarely makes sense in another
    Catalog__RefreshList(catalog);
}

static void Catalog__ShowItem(SceneCatalog *catalog, const int index) {
    if (index == catalog->shown_item) {
        return;
    }
    catalog->shown_item = index;

    if (catalog->current_category == CATALOG_CATEGORY_CLASSES) {
        CatalogClasses_Select(catalog->classes, index);
        return;
    }
    if (catalog->current_category == CATALOG_CATEGORY_SPRITES) {
        CatalogSprites_Select(catalog->sprites, index);
        return;
    }

    if (catalog->icon_sprite != NULL) {
        Sprite_Destroy(&catalog->icon_sprite);
    }

    const CatalogItemList *list = &catalog->categories[catalog->current_category];
    if (index < 0 || index >= list->count) {
        Label_SetText(catalog->name_label, "");
        for (int i = 0; i < CATALOG_MAX_STAT_LINES; i++) {
            Label_SetText(catalog->stat_labels[i], "");
        }
        return;
    }

    const CatalogItem *item = &list->items[index];

    Label_SetText(catalog->name_label, item->name);
    Label_SetColor(catalog->name_label, 0xC7, 0xB3, 0x77, 0xFF);

    for (int i = 0; i < CATALOG_MAX_STAT_LINES; i++) {
        Label_SetText(catalog->stat_labels[i], i < item->stat_line_count ? item->stat_lines[i] : "");
        Label_SetColor(catalog->stat_labels[i], 0xFF, 0xFF, 0xFF, 0xFF);
    }

    if (FileManager_FileExists(item->icon_path)) {
        catalog->icon_sprite = Sprite_Create(item->icon_path, PALETTE_UNITS);
        Sprite_GetFrameSize(catalog->icon_sprite, 0, &catalog->icon_width, &catalog->icon_height);
    } else {
        LOG_DEBUG("Catalog: no inventory picture '%s'", item->icon_path);
    }
}

static void Catalog__DrawItemDetail(const SceneCatalog *catalog, const SDL_Rect *panel) {
    const int center_x = panel->x + (panel->w / 2);
    const int icon_box = panel->y + 8;

    if (catalog->icon_sprite != NULL) {
        Sprite_DrawMulti(catalog->icon_sprite, 0, center_x - (catalog->icon_width / 2),
                         icon_box + ((CATALOG_ICON_BOX_H - catalog->icon_height) / 2), 1, 1);
    } else {
        Label_SetColor(catalog->empty_label, 0x80, 0x80, 0x80, 0xFF);
        Label_Draw(catalog->empty_label, center_x, icon_box + (CATALOG_ICON_BOX_H / 2));
    }

    int y = icon_box + CATALOG_ICON_BOX_H + 18;
    Label_Draw(catalog->name_label, center_x, y);
    y += 32;

    for (int i = 0; i < CATALOG_MAX_STAT_LINES; i++) {
        Label_Draw(catalog->stat_labels[i], center_x, y);
        y += CATALOG_STAT_LINE_H;
    }
}

void Catalog_Render(void *scene_ref) {
    SceneCatalog *catalog = (SceneCatalog *)scene_ref;

    if (catalog->background_sprite != NULL) {
        Sprite_DrawMulti(catalog->background_sprite, 0, 0, 0, 4, 3);
    }

    // Darken the background so the text stays readable
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 190);
    SDL_RenderFillRect(sdl_renderer, NULL);

    Label_Draw(catalog->title_label, 400, 34);

    for (int i = 0; i < CATALOG_CATEGORY_COUNT; i++) {
        Button_Draw(catalog->category_buttons[i]);
    }
    Button_Draw(catalog->back_button);

    if (CatalogClasses_IsSkillTreeOpen(catalog->classes)) {
        CatalogClasses_DrawSkillTree(catalog->classes);
        return;
    }

    TextBox_Draw(catalog->search_box);
    ListBox_Draw(catalog->list_box);
    Label_Draw(catalog->count_label, CATALOG_LIST_X + CATALOG_LIST_WIDTH,
               CATALOG_LIST_Y + (CATALOG_LIST_ROWS * CATALOG_LIST_ROW_H) + 12);

    // Detail panel
    const SDL_Rect panel = {CATALOG_DETAIL_X, CATALOG_DETAIL_Y, CATALOG_DETAIL_W, CATALOG_DETAIL_H};
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(sdl_renderer, &panel);
    SDL_SetRenderDrawColor(sdl_renderer, 120, 95, 40, 255);
    SDL_RenderDrawRect(sdl_renderer, &panel);

    if (catalog->shown_item < 0) {
        return;
    }

    if (catalog->current_category == CATALOG_CATEGORY_CLASSES) {
        CatalogClasses_DrawDetail(catalog->classes, &panel);
    } else if (catalog->current_category == CATALOG_CATEGORY_SPRITES) {
        CatalogSprites_DrawDetail(catalog->sprites, &panel);
    } else {
        Catalog__DrawItemDetail(catalog, &panel);
    }
}

void Catalog_Update(void *scene_ref, const uint64_t delta) {
    (void)(delta);
    SceneCatalog *catalog = (SceneCatalog *)scene_ref;

    for (int i = 0; i < CATALOG_CATEGORY_COUNT; i++) {
        if (Button_Update(catalog->category_buttons[i])) {
            CatalogClasses_CloseSkillTree(catalog->classes);
            if ((int)catalog->current_category != i) {
                Catalog__SelectCategory(catalog, (CatalogCategory)i);
            }
        }
    }

    if (Button_Update(catalog->back_button)) {
        return; // the handler either closed the skill tree or left the scene
    }

    if (CatalogClasses_IsSkillTreeOpen(catalog->classes)) {
        CatalogClasses_UpdateSkillTree(catalog->classes);
        return;
    }

    if (TextBox_Update(catalog->search_box)) {
        Catalog__RefreshList(catalog);
    }

    if (ListBox_Update(catalog->list_box)) {
        const int row = ListBox_GetSelectedIndex(catalog->list_box);
        Catalog__ShowItem(catalog, row >= 0 ? catalog->visible[row] : -1);
    }

    if (catalog->current_category == CATALOG_CATEGORY_CLASSES && CatalogClasses_UpdateDetail(catalog->classes)) {
        CatalogClasses_OpenSkillTree(catalog->classes);
    }
    if (catalog->current_category == CATALOG_CATEGORY_SPRITES) {
        CatalogSprites_UpdateDetail(catalog->sprites);
    }
}

void Catalog_Free(void **scene_ref) {
    SceneCatalog *catalog = *((SceneCatalog **)scene_ref);

    for (int i = 0; i < CATALOG_CATEGORY_COUNT; i++) {
        Button_Destroy(&catalog->category_buttons[i]);
        free(catalog->categories[i].items);
    }
    for (int i = 0; i < CATALOG_MAX_STAT_LINES; i++) {
        Label_Destroy(&catalog->stat_labels[i]);
    }
    if (catalog->icon_sprite != NULL) {
        Sprite_Destroy(&catalog->icon_sprite);
    }

    CatalogClasses_Destroy(&catalog->classes);
    CatalogSprites_Destroy(&catalog->sprites);
    TextBox_Destroy(&catalog->search_box);
    free(catalog->visible);
    Button_Destroy(&catalog->back_button);
    ListBox_Destroy(&catalog->list_box);
    Label_Destroy(&catalog->title_label);
    Label_Destroy(&catalog->count_label);
    Label_Destroy(&catalog->name_label);
    Label_Destroy(&catalog->empty_label);
    if (catalog->background_sprite != NULL) {
        Sprite_Destroy(&catalog->background_sprite);
    }

    free(*scene_ref);
    *scene_ref = NULL;
}

static void Catalog__OnBackClicked(void *user_data) {
    SceneCatalog *catalog = (SceneCatalog *)user_data;

    if (CatalogClasses_IsSkillTreeOpen(catalog->classes)) {
        CatalogClasses_CloseSkillTree(catalog->classes);
        return;
    }

    Scene_Set(SCENE_REF(MainMenu));
}
