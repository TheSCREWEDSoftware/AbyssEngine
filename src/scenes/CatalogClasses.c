#include "CatalogClasses.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../data/DataTable.h"
#include "../data/StringTable.h"
#include "../drawing/Button.h"
#include "../drawing/Figure.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Skills.h"
#include "../managers/InputManager.h"
#include "CatalogTheme.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLASSES_MAX           8
#define CLASS_MAX_SKILLS      40
#define CLASS_STAT_LINES      16
#define CLASS_INFO_LINES      18
#define CLASS_TEXT_LEN        160
#define SKILL_PAGES           3
#define SKILL_ICON_SIZE       48
#define SKILL_TAB_LABEL_LINES 3

// Skill panel layout, relative to the panel's top-left corner (320x432 panel from skltree_?_back.dc6).
#define TREE_PANEL_X        440
#define TREE_PANEL_Y        110
#define TREE_ICON_X         15
#define TREE_ICON_Y         8
#define TREE_ICON_STEP_X    69
#define TREE_ICON_STEP_Y    68
#define TREE_TAB_X          230
#define TREE_TAB_W          90
#define TREE_TAB_FIRST_Y    111
#define TREE_TAB_H          105
#define TREE_TAB_STEP_Y     107
#define TREE_INFO_X         40
#define TREE_INFO_Y         110
#define TREE_INFO_W         380
#define TREE_INFO_H         416
#define TREE_INFO_TEXT_W    360
#define TREE_INFO_LINE_H    18
#define DETAIL_FIGURE_X     80
#define DETAIL_FIGURE_Y     235
#define DETAIL_STATS_COLUMN 360 // labels end / values start here
#define DETAIL_FIGURE_BOX_W 150
#define DETAIL_FIGURE_BOX_H 300
#define DETAIL_STAT_LINE_H  19
#define DETAIL_FIGURE_SCALE 3 // max integer zoom of the in-town figure
#define DETAIL_FIGURE_SPEED 0.75f

// Per-class graphics. Classes are matched by their charstats.txt name.
typedef struct ClassVisual {
    const char *name;
    const char *code;         // skills.txt "charclass"
    const char *icon_prefix;  // /data/global/ui/spells/<prefix>skillicon.dc6
    char        tree_letter;  // /data/global/ui/spells/skltree_<letter>_back.dc6
    const char *figure_token; // data/global/chars/<token>: lit in-town figure
    const char *figure_path;  // character select idle animation (fallback; dark, lit by the campfire)
    // Skill tab caption for pages 1..3, one StrSklTree<n> string per line (as shown on the in-game tabs).
    int tab_lines[SKILL_PAGES][SKILL_TAB_LABEL_LINES];
} ClassVisual;

static const ClassVisual class_visuals[] = {
    {"Amazon",      "ama", "am", 'a', "am", "/data/global/ui/FrontEnd/amazon/amnu1.dc6",    {{10, 11}, {8, 9}, {6, 7}}   },
    {"Sorceress",   "sor", "so", 's', "so", "/data/global/ui/FrontEnd/sorceress/sonu1.dc6", {{25, 5}, {24, 5}, {23, 5}}  },
    {"Necromancer",
     "nec",                "ne",
     'n',                             "ne",
     "/data/global/ui/FrontEnd/necromancer/nenu1.dc6",                                      {{19}, {17, 18, 5}, {16, 5}} },
    {"Paladin",     "pal", "pa", 'p', "pa", "/data/global/ui/FrontEnd/paladin/panu1.dc6",   {{15, 4}, {14, 13}, {12, 13}}},
    {"Barbarian",   "bar", "ba", 'b', "ba", "/data/global/ui/FrontEnd/barbarian/banu1.dc6", {{15, 4}, {21, 22}, {20}}    },
    {"Druid",       "dru", "dr", 'd', "dz", "/data/global/ui/FrontEnd/druid/dznu1.dc6",     {{26}, {27, 28}, {29}}       },
    {"Assassin",    "ass", "as", 'i', "ai", "/data/global/ui/FrontEnd/assassin/asnu1.dc6",  {{30}, {31, 32}, {33, 34}}   },
};

#define CLASS_VISUAL_COUNT (int)(sizeof(class_visuals) / sizeof(class_visuals[0]))

typedef struct SkillInfo {
    char id[64]; // skills.txt "skill" (used by reqskill columns)
    char name[64];
    char short_desc[CLASS_TEXT_LEN];
    char long_desc[CLASS_TEXT_LEN * 3];
    char prerequisites[3][64];
    int  page;
    int  row;
    int  column;
    int  icon_frame;
    int  required_level;
    int  mana_cost;
    bool passive;
} SkillInfo;

typedef struct ClassInfo {
    const ClassVisual *visual;
    char               name[32];
    char               tab_names[SKILL_PAGES][64];
    char               tab_lines[SKILL_PAGES][SKILL_TAB_LABEL_LINES][32];
    char               stat_lines[CLASS_STAT_LINES][CLASS_TEXT_LEN];
    int                stat_line_count;
    SkillInfo          skills[CLASS_MAX_SKILLS];
    int                skill_count;
} ClassInfo;

struct CatalogClasses {
    ClassInfo classes[CLASSES_MAX];
    int       class_count;
    int       selected_class;

    // Detail panel
    Sprite  *figure_sprite;
    Figure  *figure;
    int      figure_frame;
    uint64_t figure_tick;
    Label   *stat_labels[CLASS_STAT_LINES]; // "Strength:" (right aligned)
    Label   *stat_values[CLASS_STAT_LINES]; // "10"
    Label   *class_title_label;
    Button  *skill_tree_button;

    // Skill tree view
    bool    tree_open;
    int     tree_page; // 1..3
    int     hovered_skill;
    int     selected_skill;
    Sprite *tree_sprite;
    Sprite *icon_sprite;
    Label  *tab_labels[SKILL_PAGES][SKILL_TAB_LABEL_LINES];
    Label  *info_title_label;
    Label  *info_labels[CLASS_INFO_LINES];
    int     info_line_count;
    bool    prev_mouse_left;
    bool    armed;
};

// ---------------------------------------------------------------------------------------------------------------------
// Text helpers

// Bounded string copy / append (always NUL terminated).
static void Classes__Append(char *dst, const size_t dst_len, const char *src) {
    const size_t used = strlen(dst);
    if (used + 1 >= dst_len) {
        return;
    }
    size_t count = strlen(src);
    if (count > dst_len - used - 1) {
        count = dst_len - used - 1;
    }
    memcpy(dst + used, src, count);
    dst[used + count] = '\0';
}

static void Classes__Copy(char *dst, const size_t dst_len, const char *src) {
    dst[0] = '\0';
    Classes__Append(dst, dst_len, src);
}

static void Classes__Sanitize(char *out, const size_t out_len, const char *in) {
    size_t o = 0;
    for (const unsigned char *ch = (const unsigned char *)in; *ch != '\0' && o + 1 < out_len; ch++) {
        if (*ch == 0xFF && ch[1] == 'c' && ch[2] != '\0') { // \xFFc<n> colour code
            ch += 2;
            continue;
        }
        if (*ch == '\n' || (*ch >= 0x20 && *ch < 0x7F)) {
            out[o++] = (char)*ch;
        }
    }
    out[o] = '\0';
}

static void Classes__String(char *out, const size_t out_len, const char *key, const char *fallback) {
    const char *value = StringTable_Find(key);
    Classes__Sanitize(out, out_len, value != NULL ? value : fallback);
}

// Quarter-point values (life per level etc.) as "2" or "1.5".
static void Classes__FormatQuarters(char *out, const size_t out_len, const int quarters) {
    const int whole    = quarters / 4;
    const int fraction = (quarters % 4) * 25; // 0, 25, 50 or 75 hundredths

    if (fraction == 0) {
        snprintf(out, out_len, "%d", whole);
    } else if (fraction == 50) {
        snprintf(out, out_len, "%d.5", whole);
    } else {
        snprintf(out, out_len, "%d.%02d", whole, fraction);
    }
}

static void Classes__AddStat(ClassInfo *info, const char *format, ...) {
    if (info->stat_line_count >= CLASS_STAT_LINES) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(info->stat_lines[info->stat_line_count++], CLASS_TEXT_LEN, format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading

static const ClassVisual *Classes__FindVisual(const char *name) {
    for (int i = 0; i < CLASS_VISUAL_COUNT; i++) {
        if (strcmp(class_visuals[i].name, name) == 0) {
            return &class_visuals[i];
        }
    }
    return NULL;
}

static void Classes__LoadStats(CatalogClasses *classes) {
    DataTable *t    = DataTable_Load(EXCEL_CHARSTATS);
    const int  rows = DataTable_GetRowCount(t);

#define COL(name) DataTable_GetColumnIndex(t, name)
    const int c_class = COL("class"), c_str = COL("str"), c_dex = COL("dex"), c_int = COL("int"), c_vit = COL("vit"),
              c_stamina = COL("stamina"), c_hpadd = COL("hpadd"), c_life_lvl = COL("LifePerLevel"),
              c_stam_lvl = COL("StaminaPerLevel"), c_mana_lvl = COL("ManaPerLevel"),
              c_life_vit = COL("LifePerVitality"), c_stam_vit = COL("StaminaPerVitality"),
              c_mana_nrg = COL("ManaPerMagic");
#undef COL

    for (int row = 0; row < rows && classes->class_count < CLASSES_MAX; row++) {
        const ClassVisual *visual = Classes__FindVisual(DataTable_GetString(t, row, c_class));
        if (visual == NULL) { // "Expansion" separator rows etc.
            continue;
        }

        ClassInfo *info = &classes->classes[classes->class_count++];
        info->visual    = visual;
        Classes__String(info->name, sizeof(info->name), visual->name, visual->name);

        for (int page = 0; page < SKILL_PAGES; page++) {
            for (int line = 0; line < SKILL_TAB_LABEL_LINES; line++) {
                const int string_index = visual->tab_lines[page][line];
                if (string_index == 0) {
                    continue;
                }

                char key[32];
                snprintf(key, sizeof(key), "StrSklTree%d", string_index);
                Classes__String(info->tab_lines[page][line], sizeof(info->tab_lines[page][line]), key, "");
                Classes__Append(info->tab_names[page], sizeof(info->tab_names[page]), line > 0 ? " " : "");
                Classes__Append(info->tab_names[page], sizeof(info->tab_names[page]), info->tab_lines[page][line]);
            }
        }

        const int str = DataTable_GetInt(t, row, c_str), dex = DataTable_GetInt(t, row, c_dex),
                  energy = DataTable_GetInt(t, row, c_int), vit = DataTable_GetInt(t, row, c_vit);
        char life_lvl[16], stam_lvl[16], mana_lvl[16], life_vit[16], stam_vit[16], mana_nrg[16];
        Classes__FormatQuarters(life_lvl, sizeof(life_lvl), DataTable_GetInt(t, row, c_life_lvl));
        Classes__FormatQuarters(stam_lvl, sizeof(stam_lvl), DataTable_GetInt(t, row, c_stam_lvl));
        Classes__FormatQuarters(mana_lvl, sizeof(mana_lvl), DataTable_GetInt(t, row, c_mana_lvl));
        Classes__FormatQuarters(life_vit, sizeof(life_vit), DataTable_GetInt(t, row, c_life_vit));
        Classes__FormatQuarters(stam_vit, sizeof(stam_vit), DataTable_GetInt(t, row, c_stam_vit));
        Classes__FormatQuarters(mana_nrg, sizeof(mana_nrg), DataTable_GetInt(t, row, c_mana_nrg));

        Classes__AddStat(info, "Strength: %d", str);
        Classes__AddStat(info, "Dexterity: %d", dex);
        Classes__AddStat(info, "Vitality: %d", vit);
        Classes__AddStat(info, "Energy: %d", energy);
        Classes__AddStat(info, " ");
        Classes__AddStat(info, "Life: %d", vit + DataTable_GetInt(t, row, c_hpadd));
        Classes__AddStat(info, "Stamina: %d", DataTable_GetInt(t, row, c_stamina));
        Classes__AddStat(info, "Mana: %d", energy);
        Classes__AddStat(info, " ");
        Classes__AddStat(info, "Life per level: +%s", life_lvl);
        Classes__AddStat(info, "Stamina per level: +%s", stam_lvl);
        Classes__AddStat(info, "Mana per level: +%s", mana_lvl);
        Classes__AddStat(info, "Life per Vitality: +%s", life_vit);
        Classes__AddStat(info, "Stamina per Vitality: +%s", stam_vit);
        Classes__AddStat(info, "Mana per Energy: +%s", mana_nrg);
    }

    DataTable_Destroy(&t);
}

// The skills come from the shared table (game/Skills.h), which the game's skill tree uses too
static void Classes__LoadSkills(CatalogClasses *classes) {
    for (int c = 0; c < classes->class_count; c++) {
        ClassInfo *info     = &classes->classes[c];
        int        class_id = -1;
        for (int k = 0; k < CHARACTER_CLASS_COUNT; k++) {
            class_id = strcmp(Character_ClassName((CharacterClass)k), info->visual->name) == 0 ? k : class_id;
        }
        if (class_id < 0) {
            continue;
        }
        const SkillClass *skills = Skills_ForClass((CharacterClass)class_id);
        for (int s = 0; s < skills->count && info->skill_count < CLASS_MAX_SKILLS; s++) {
            const SkillDef *def   = &skills->skills[s];
            SkillInfo      *skill = &info->skills[info->skill_count++];
            memset(skill, 0, sizeof(SkillInfo));
            Classes__Copy(skill->id, sizeof(skill->id), def->id);
            Classes__Copy(skill->name, sizeof(skill->name), def->name);
            Classes__Copy(skill->short_desc, sizeof(skill->short_desc), def->short_desc);
            Classes__Copy(skill->long_desc, sizeof(skill->long_desc), def->long_desc);
            for (int r = 0; r < 3; r++) {
                if (def->prerequisites[r] >= 0) {
                    Classes__Copy(skill->prerequisites[r], sizeof(skill->prerequisites[r]),
                                  skills->skills[def->prerequisites[r]].name);
                }
            }
            skill->page           = def->page;
            skill->row            = def->row;
            skill->column         = def->column;
            skill->icon_frame     = def->icon_frame;
            skill->required_level = def->required_level;
            skill->mana_cost      = def->mana_cost;
            skill->passive        = def->passive;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime

CatalogClasses *CatalogClasses_Create(void) {
    CatalogClasses *classes = malloc(sizeof(CatalogClasses));
    FAIL_IF_NULL(classes);
    memset(classes, 0, sizeof(CatalogClasses));

    classes->selected_class = -1;
    classes->hovered_skill  = -1;
    classes->selected_skill = -1;
    classes->tree_page      = 1;

    Classes__LoadStats(classes);
    Classes__LoadSkills(classes);
    LOG_INFO("Catalog: %d classes", classes->class_count);

    classes->class_title_label = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetAlignment(classes->class_title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);

    for (int i = 0; i < CLASS_STAT_LINES; i++) {
        classes->stat_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(classes->stat_labels[i], LABEL_ALIGN_END, LABEL_ALIGN_CENTER);
        classes->stat_values[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(classes->stat_values[i], LABEL_ALIGN_BEGIN, LABEL_ALIGN_CENTER);
    }

    classes->skill_tree_button = Button_Create(BUTTON_TYPE_WIDE, "Skill Trees");

    for (int page = 0; page < SKILL_PAGES; page++) {
        for (int line = 0; line < SKILL_TAB_LABEL_LINES; line++) {
            classes->tab_labels[page][line] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
            Label_SetAlignment(classes->tab_labels[page][line], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
        }
    }

    classes->info_title_label = Label_Create(FONTS_FONT24, PALETTE_STATIC);
    Label_SetAlignment(classes->info_title_label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    for (int i = 0; i < CLASS_INFO_LINES; i++) {
        classes->info_labels[i] = Label_Create(FONTS_FONT16, PALETTE_STATIC);
        Label_SetAlignment(classes->info_labels[i], LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    }

    return classes;
}

static void Classes__FreeSprite(Sprite **sprite) {
    if (*sprite != NULL) {
        Sprite_Destroy(sprite);
    }
}

void CatalogClasses_Destroy(CatalogClasses **classes_ref) {
    if (classes_ref == NULL || *classes_ref == NULL) {
        return;
    }
    CatalogClasses *classes = *classes_ref;

    Figure_Destroy(&classes->figure);
    Classes__FreeSprite(&classes->figure_sprite);
    Classes__FreeSprite(&classes->tree_sprite);
    Classes__FreeSprite(&classes->icon_sprite);

    for (int i = 0; i < CLASS_STAT_LINES; i++) {
        Label_Destroy(&classes->stat_labels[i]);
        Label_Destroy(&classes->stat_values[i]);
    }
    for (int page = 0; page < SKILL_PAGES; page++) {
        for (int line = 0; line < SKILL_TAB_LABEL_LINES; line++) {
            Label_Destroy(&classes->tab_labels[page][line]);
        }
    }
    for (int i = 0; i < CLASS_INFO_LINES; i++) {
        Label_Destroy(&classes->info_labels[i]);
    }
    Label_Destroy(&classes->class_title_label);
    Label_Destroy(&classes->info_title_label);
    Button_Destroy(&classes->skill_tree_button);

    free(classes);
    *classes_ref = NULL;
}

int CatalogClasses_GetCount(const CatalogClasses *classes) { return classes->class_count; }

const char *CatalogClasses_GetName(const CatalogClasses *classes, const int index) {
    return classes->classes[index].name;
}

// ---------------------------------------------------------------------------------------------------------------------
// Class detail panel

void CatalogClasses_Select(CatalogClasses *classes, const int index) {
    if (index == classes->selected_class) {
        return;
    }

    classes->selected_class = index;
    Classes__FreeSprite(&classes->figure_sprite);
    Figure_Destroy(&classes->figure);

    if (index < 0 || index >= classes->class_count) {
        return;
    }

    const ClassInfo *info = &classes->classes[index];

    Label_SetText(classes->class_title_label, info->name);
    Label_SetColor(classes->class_title_label, 0xC7, 0xB3, 0x77, 0xFF);

    for (int i = 0; i < CLASS_STAT_LINES; i++) {
        const char *line  = i < info->stat_line_count ? info->stat_lines[i] : "";
        const char *colon = strchr(line, ':');
        char        label[CLASS_TEXT_LEN];
        if (colon != NULL) {
            snprintf(label, sizeof(label), "%.*s", (int)(colon - line + 1), line);
            Label_SetText(classes->stat_labels[i], label);
            Label_SetText(classes->stat_values[i], colon[1] == ' ' ? colon + 2 : colon + 1);
        } else {
            Label_SetText(classes->stat_labels[i], line);
            Label_SetText(classes->stat_values[i], "");
        }
        Label_SetColor(classes->stat_labels[i], 0xD8, 0xC0, 0x80, 0xFF);
        Label_SetColor(classes->stat_values[i], 0xFF, 0xFF, 0xFF, 0xFF);
    }

    // The character select figures stand in the dark by the campfire; show the lit in-town figure instead.
    FigureSpec spec = {.base = "chars"};
    snprintf(spec.token, sizeof(spec.token), "%s", info->visual->figure_token);
    snprintf(spec.weapon_class, sizeof(spec.weapon_class), "hth");
    for (int c = 0; c < COF_COMPONENT_COUNT; c++) {
        if (c != COF_COMPONENT_RH && c != COF_COMPONENT_LH && c != COF_COMPONENT_SH) {
            snprintf(spec.armor[c], sizeof(spec.armor[c]), "lit");
        }
    }
    classes->figure       = Figure_Create(&spec, "tn", PALETTE_ACT_1);
    classes->figure_frame = 0;
    classes->figure_tick  = SDL_GetTicks64();

    if (classes->figure == NULL && FileManager_FileExists(info->visual->figure_path)) {
        classes->figure_sprite = Sprite_Create(info->visual->figure_path, PALETTE_FECHAR);
        Sprite_SetPlayLength(classes->figure_sprite, 2.0);
    }
}

bool CatalogClasses_UpdateDetail(CatalogClasses *classes) {
    if (classes->selected_class < 0) {
        return false;
    }

    if (classes->figure != NULL) {
        const uint64_t now      = SDL_GetTicks64();
        const int      frame_ms = (int)((float)Figure_GetFrameMilliseconds(classes->figure) / DETAIL_FIGURE_SPEED);
        const int      frames   = Figure_GetFramesPerDirection(classes->figure);
        while (frame_ms > 0 && frames > 0 && now - classes->figure_tick >= (uint64_t)frame_ms) {
            classes->figure_tick  += (uint64_t)frame_ms;
            classes->figure_frame  = (classes->figure_frame + 1) % frames;
        }
    }

    return Button_Update(classes->skill_tree_button);
}

static void Classes__DrawFigure(const CatalogClasses *classes, const SDL_Rect *box) {
    int xmin;
    int ymin;
    int xmax;
    int ymax;
    Figure_GetBounds(classes->figure, &xmin, &ymin, &xmax, &ymax);
    const int width  = xmax - xmin > 0 ? xmax - xmin : 1;
    const int height = ymax - ymin > 0 ? ymax - ymin : 1;

    int scale = DETAIL_FIGURE_SCALE;
    while (scale > 1 && (width * scale > box->w - 16 || height * scale > box->h - 16)) {
        scale--;
    }

    // Centre the bounding box; the figure is drawn relative to its feet
    const int anchor_x = box->x + (box->w / 2) - ((xmin + (width / 2)) * scale);
    const int anchor_y = box->y + ((box->h - (height * scale)) / 2) - (ymin * scale);

    // Facing the viewer: direction 4 is south for both 8 and 16 direction figures
    SDL_RenderSetClipRect(sdl_renderer, box);
    Figure_Draw(classes->figure, 4, classes->figure_frame, anchor_x, anchor_y, (float)scale);
    SDL_RenderSetClipRect(sdl_renderer, NULL);
}

void CatalogClasses_DrawDetail(const CatalogClasses *classes, const SDL_Rect *panel) {
    if (classes->selected_class < 0) {
        return;
    }

    Label_Draw(classes->class_title_label, panel->x + (panel->w / 2), panel->y + 22);

    const SDL_Rect figure_box = {panel->x + 10, panel->y + 44, DETAIL_FIGURE_BOX_W, DETAIL_FIGURE_BOX_H};
    CatalogTheme_FillPreview(&figure_box);
    if (classes->figure != NULL) {
        Classes__DrawFigure(classes, &figure_box);
    } else if (classes->figure_sprite != NULL) {
        Sprite_DrawAnimated(classes->figure_sprite, panel->x + DETAIL_FIGURE_X, panel->y + DETAIL_FIGURE_Y);
    }

    int y = panel->y + 60;
    for (int i = 0; i < CLASS_STAT_LINES; i++) {
        Label_Draw(classes->stat_labels[i], panel->x + DETAIL_STATS_COLUMN - 3, y);
        Label_Draw(classes->stat_values[i], panel->x + DETAIL_STATS_COLUMN + 3, y);
        y += DETAIL_STAT_LINE_H;
    }

    int button_w;
    int button_h;
    Button_GetSize(classes->skill_tree_button, &button_w, &button_h);
    Button_SetPosition(classes->skill_tree_button, panel->x + ((panel->w - button_w) / 2),
                       panel->y + panel->h - button_h - 12);
    Button_Draw(classes->skill_tree_button);
}

// ---------------------------------------------------------------------------------------------------------------------
// Skill tree

static void Classes__SetTabLabels(CatalogClasses *classes, const ClassInfo *info) {
    for (int page = 0; page < SKILL_PAGES; page++) {
        for (int line = 0; line < SKILL_TAB_LABEL_LINES; line++) {
            Label_SetText(classes->tab_labels[page][line], info->tab_lines[page][line]);
        }
    }
}

void CatalogClasses_OpenSkillTree(CatalogClasses *classes) {
    if (classes->selected_class < 0) {
        return;
    }

    const ClassInfo *info = &classes->classes[classes->selected_class];
    char             path[128];

    Classes__FreeSprite(&classes->tree_sprite);
    Classes__FreeSprite(&classes->icon_sprite);

    snprintf(path, sizeof(path), "/data/global/ui/spells/skltree_%c_back.dc6", info->visual->tree_letter);
    if (FileManager_FileExists(path)) {
        classes->tree_sprite = Sprite_Create(path, PALETTE_SKY);
    }

    snprintf(path, sizeof(path), "/data/global/ui/spells/%sskillicon.dc6", info->visual->icon_prefix);
    if (FileManager_FileExists(path)) {
        classes->icon_sprite = Sprite_Create(path, PALETTE_SKY);
    }

    Classes__SetTabLabels(classes, info);

    char title[64];
    snprintf(title, sizeof(title), "%s", info->name);
    Label_SetText(classes->info_title_label, title);
    Label_SetColor(classes->info_title_label, 0xC7, 0xB3, 0x77, 0xFF);

    classes->tree_open       = true;
    classes->tree_page       = 1;
    classes->hovered_skill   = -1;
    classes->selected_skill  = -1;
    classes->info_line_count = 0;
    classes->armed           = false;
    Label_SetText(classes->info_labels[0], "Click a skill to see its details");
    Label_SetColor(classes->info_labels[0], 0xA0, 0xA0, 0xA0, 0xFF);
    InputManager_GetMouseButtons(&classes->prev_mouse_left, NULL, NULL);
}

void CatalogClasses_CloseSkillTree(CatalogClasses *classes) {
    classes->tree_open = false;
    Classes__FreeSprite(&classes->tree_sprite);
    Classes__FreeSprite(&classes->icon_sprite);
}

bool CatalogClasses_IsSkillTreeOpen(const CatalogClasses *classes) { return classes->tree_open; }

static SDL_Rect Classes__SkillRect(const SkillInfo *skill) {
    const SDL_Rect rect = {TREE_PANEL_X + TREE_ICON_X + ((skill->column - 1) * TREE_ICON_STEP_X),
                           TREE_PANEL_Y + TREE_ICON_Y + ((skill->row - 1) * TREE_ICON_STEP_Y), SKILL_ICON_SIZE,
                           SKILL_ICON_SIZE};
    return rect;
}

static SDL_Rect Classes__TabRect(const int page) {
    const SDL_Rect rect = {TREE_PANEL_X + TREE_TAB_X, TREE_PANEL_Y + TREE_TAB_FIRST_Y + ((3 - page) * TREE_TAB_STEP_Y),
                           TREE_TAB_W, TREE_TAB_H};
    return rect;
}

static bool Classes__PointInRect(const int x, const int y, const SDL_Rect *rect) {
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

static void Classes__AddInfoLine(CatalogClasses *classes, const char *text, const uint8_t r, const uint8_t g,
                                 const uint8_t b) {
    if (classes->info_line_count >= CLASS_INFO_LINES) {
        return;
    }

    Label *label = classes->info_labels[classes->info_line_count++];
    Label_SetText(label, text[0] != '\0' ? text : " ");
    Label_SetColor(label, r, g, b, 0xFF);
}

// Adds text word-wrapped to the info panel width; '\n' starts a new line.
static void Classes__AddWrapped(CatalogClasses *classes, const char *text, const uint8_t r, const uint8_t g,
                                const uint8_t b) {
    char buffer[CLASS_TEXT_LEN * 3];
    snprintf(buffer, sizeof(buffer), "%s", text);

    char *paragraph = buffer;
    while (paragraph != NULL) {
        char *next = strchr(paragraph, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        char line[CLASS_TEXT_LEN] = "";
        for (char *word = paragraph; word != NULL && *word != '\0';) {
            char *space = strchr(word, ' ');
            if (space != NULL) {
                *space = '\0';
            }

            char candidate[CLASS_TEXT_LEN];
            Classes__Copy(candidate, sizeof(candidate), line);
            Classes__Append(candidate, sizeof(candidate), line[0] ? " " : "");
            Classes__Append(candidate, sizeof(candidate), word);
            if (line[0] != '\0' && Label_MeasureText(classes->info_labels[0], candidate) > TREE_INFO_TEXT_W) {
                Classes__AddInfoLine(classes, line, r, g, b);
                Classes__Copy(line, sizeof(line), word);
            } else {
                Classes__Copy(line, sizeof(line), candidate);
            }

            word = space != NULL ? space + 1 : NULL;
        }
        if (line[0] != '\0') {
            Classes__AddInfoLine(classes, line, r, g, b);
        }

        paragraph = next;
    }
}

static void Classes__ShowSkill(CatalogClasses *classes, const int skill_index) {
    classes->selected_skill  = skill_index;
    classes->info_line_count = 0;

    const ClassInfo *info  = &classes->classes[classes->selected_class];
    const SkillInfo *skill = &info->skills[skill_index];
    char             line[CLASS_TEXT_LEN * 2];

    Label_SetText(classes->info_title_label, skill->name);
    Label_SetColor(classes->info_title_label, 0xC7, 0xB3, 0x77, 0xFF);

    Classes__AddInfoLine(classes, info->tab_names[skill->page - 1], 0x90, 0x90, 0x90);
    Classes__AddInfoLine(classes, "", 0, 0, 0);

    snprintf(line, sizeof(line), "Required Level: %d", skill->required_level);
    Classes__AddInfoLine(classes, line, 0xFF, 0xFF, 0xFF);

    line[0] = '\0';
    for (int r = 0; r < 3; r++) {
        if (skill->prerequisites[r][0] == '\0') {
            continue;
        }
        strncat(line, line[0] ? ", " : "Requires: ", sizeof(line) - strlen(line) - 1);
        strncat(line, skill->prerequisites[r], sizeof(line) - strlen(line) - 1);
    }
    if (line[0] != '\0') {
        Classes__AddWrapped(classes, line, 0xFF, 0xFF, 0xFF);
    }

    if (skill->passive) {
        Classes__AddInfoLine(classes, "Passive", 0x90, 0x90, 0xFF);
    } else if (skill->mana_cost > 0) {
        snprintf(line, sizeof(line), "Mana Cost: %d (level 1)", skill->mana_cost);
        Classes__AddInfoLine(classes, line, 0x90, 0x90, 0xFF);
    }

    Classes__AddInfoLine(classes, "", 0, 0, 0);
    Classes__AddWrapped(classes, skill->long_desc[0] != '\0' ? skill->long_desc : skill->short_desc, 0xC0, 0xC0, 0xC0);
}

void CatalogClasses_UpdateSkillTree(CatalogClasses *classes) {
    if (!classes->tree_open) {
        return;
    }

    int  mouse_x;
    int  mouse_y;
    bool mouse_left;
    InputManager_GetMousePosition(&mouse_x, &mouse_y);
    InputManager_GetMouseButtons(&mouse_left, NULL, NULL);

    const bool was_mouse_left = classes->prev_mouse_left;
    classes->prev_mouse_left  = mouse_left;

    const ClassInfo *info = &classes->classes[classes->selected_class];

    classes->hovered_skill = -1;
    for (int i = 0; i < info->skill_count; i++) {
        const SDL_Rect rect = Classes__SkillRect(&info->skills[i]);
        if (info->skills[i].page == classes->tree_page && Classes__PointInRect(mouse_x, mouse_y, &rect)) {
            classes->hovered_skill = i;
            break;
        }
    }

    if (mouse_left && !was_mouse_left) {
        classes->armed = true;
    }

    if (!mouse_left && was_mouse_left && classes->armed) {
        classes->armed = false;

        for (int page = 1; page <= SKILL_PAGES; page++) {
            const SDL_Rect tab = Classes__TabRect(page);
            if (Classes__PointInRect(mouse_x, mouse_y, &tab)) {
                classes->tree_page = page;
                return;
            }
        }

        if (classes->hovered_skill >= 0) {
            Classes__ShowSkill(classes, classes->hovered_skill);
        }
    }
}

void CatalogClasses_DrawSkillTree(CatalogClasses *classes) {
    if (!classes->tree_open) {
        return;
    }

    const ClassInfo *info = &classes->classes[classes->selected_class];

    // Left: skill information
    const SDL_Rect info_rect = {TREE_INFO_X, TREE_INFO_Y, TREE_INFO_W, TREE_INFO_H};
    CatalogTheme_FillPanel(&info_rect);

    const int center_x = TREE_INFO_X + (TREE_INFO_W / 2);
    if (classes->selected_skill >= 0) {
        const SkillInfo *skill = &info->skills[classes->selected_skill];
        if (classes->icon_sprite != NULL) {
            Sprite_DrawMulti(classes->icon_sprite, (uint8_t)skill->icon_frame, center_x - (SKILL_ICON_SIZE / 2),
                             TREE_INFO_Y + 14, 1, 1);
        }
        Label_Draw(classes->info_title_label, center_x, TREE_INFO_Y + 86);

        int y = TREE_INFO_Y + 116;
        for (int i = 0; i < classes->info_line_count; i++) {
            Label_Draw(classes->info_labels[i], center_x, y);
            y += TREE_INFO_LINE_H;
        }
    } else {
        Label_Draw(classes->info_title_label, center_x, TREE_INFO_Y + 40);
        Label_Draw(classes->info_labels[0], center_x, TREE_INFO_Y + 80);
    }

    // Right: the game's skill panel, tab strip first, then the selected page on top of it
    if (classes->tree_sprite != NULL) {
        Sprite_DrawMulti(classes->tree_sprite, 0, TREE_PANEL_X, TREE_PANEL_Y, 2, 2);
        Sprite_DrawMulti(classes->tree_sprite, (uint8_t)(classes->tree_page * 4), TREE_PANEL_X, TREE_PANEL_Y, 2, 2);
    }

    for (int page = 1; page <= SKILL_PAGES; page++) {
        const SDL_Rect tab      = Classes__TabRect(page);
        int            lines    = 0;
        int            line_h   = 0;
        const bool     selected = page == classes->tree_page;

        for (int l = 0; l < SKILL_TAB_LABEL_LINES; l++) {
            int w;
            int h;
            Label_GetSize(classes->tab_labels[page - 1][l], &w, &h);
            if (w > 0) {
                lines++;
                line_h = h > line_h ? h : line_h;
            }
        }

        int y = tab.y + (tab.h / 2) - ((lines * line_h) / 2) + (line_h / 2);
        for (int l = 0; l < lines; l++) {
            Label *label = classes->tab_labels[page - 1][l];
            if (selected) {
                Label_SetColor(label, 0xFF, 0xFF, 0xFF, 0xFF);
            } else {
                Label_SetColor(label, 0x90, 0x90, 0x90, 0xFF);
            }
            Label_Draw(label, tab.x + (tab.w / 2), y);
            y += line_h;
        }
    }

    if (classes->icon_sprite != NULL) {
        for (int i = 0; i < info->skill_count; i++) {
            const SkillInfo *skill = &info->skills[i];
            if (skill->page != classes->tree_page) {
                continue;
            }

            const SDL_Rect rect = Classes__SkillRect(skill);
            Sprite_DrawMulti(classes->icon_sprite, (uint8_t)skill->icon_frame, rect.x, rect.y, 1, 1);

            if (i == classes->selected_skill) {
                SDL_SetRenderDrawColor(sdl_renderer, 230, 200, 110, 255);
                const SDL_Rect outline = {rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
                SDL_RenderDrawRect(sdl_renderer, &outline);
                SDL_RenderDrawRect(sdl_renderer, &rect);
            } else if (i == classes->hovered_skill) {
                SDL_SetRenderDrawColor(sdl_renderer, 160, 140, 90, 255);
                SDL_RenderDrawRect(sdl_renderer, &rect);
            }
        }
    }
}
