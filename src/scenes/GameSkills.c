#include "GameSkills.h"
#include "../common/FileManager.h"
#include "../common/Globals.h"
#include "../common/Logging.h"
#include "../common/ResourcePaths.h"
#include "../drawing/Label.h"
#include "../drawing/Sprite.h"
#include "../game/Affixes.h"
#include "../game/SkillData.h"
#include "../game/Skills.h"
#include "GameInventory.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int game_skill_left_x  = 117;
int game_skill_right_x = 635;
int game_skill_y       = 552;

void GameSkills_SetButtons(const int left_x, const int right_x, const int y) {
    game_skill_left_x  = left_x;
    game_skill_right_x = right_x;
    game_skill_y       = y;
}

#define ICON          48
#define ATTACK_ICON   2  // in the shared Skillicon.dc6
#define TREE_ICON_X   15 // the skill tree's layout, from the panel's top left
#define TREE_ICON_Y   8
#define TREE_STEP_X   69
#define TREE_STEP_Y   68
#define TREE_TAB_X    230
#define TREE_TAB_W    90
#define TREE_TAB_Y    111
#define TREE_TAB_H    105
#define TREE_TAB_STEP 107
#define TIP_LINES     12
#define TIP_WIDTH     300

struct GameSkills {
    CharacterClass    class_id;
    const SkillClass *table;
    Sprite           *tree;
    Sprite           *icons;                         // the class's
    Sprite           *shared;                        // Attack and the other skills every class has
    Sprite           *others[CHARACTER_CLASS_COUNT]; // other classes' icons (skills from items), when needed
    Label            *text;
    Label            *small;
    int               page;   // 1..3
    int               picker; // 0 closed, 1 left, 2 right
};

GameSkills *GameSkills_Create(const CharacterClass class_id) {
    GameSkills *skills = calloc(1, sizeof(GameSkills));
    FAIL_IF_NULL(skills);
    skills->class_id = class_id;
    skills->table    = Skills_ForClass(class_id);
    skills->page     = 1;
    if (FileManager_FileExists(skills->table->tree_path)) {
        skills->tree = Sprite_Create(skills->table->tree_path, PALETTE_SKY);
    }
    if (FileManager_FileExists(skills->table->icon_path)) {
        skills->icons = Sprite_Create(skills->table->icon_path, PALETTE_SKY);
    }
    skills->shared = Sprite_Create("/data/global/ui/SPELLS/Skillicon.dc6", PALETTE_SKY);
    skills->text   = Label_Create(FONTS_FONT16, PALETTE_STATIC);
    skills->small  = Label_Create(FONTS_FONT8, PALETTE_STATIC);
    return skills;
}

void GameSkills_Destroy(GameSkills **skills_ref) {
    GameSkills *skills = *skills_ref;
    if (skills == NULL) {
        return;
    }
    Sprite_Destroy(&skills->tree);
    Sprite_Destroy(&skills->icons);
    Sprite_Destroy(&skills->shared);
    for (int c = 0; c < CHARACTER_CLASS_COUNT; c++) {
        Sprite_Destroy(&skills->others[c]);
    }
    Label_Destroy(&skills->text);
    Label_Destroy(&skills->small);
    free(skills);
    *skills_ref = NULL;
}

static bool GameSkills__In(const SDL_Rect *rect, const int x, const int y) {
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

// ---------------------------------------------------------------------------------------------------------------------
// The tree

static SDL_Rect GameSkills__IconRect(const SkillDef *skill) {
    const SDL_Rect rect = {GAME_PANEL_X + TREE_ICON_X + ((skill->column - 1) * TREE_STEP_X),
                           TREE_ICON_Y + ((skill->row - 1) * TREE_STEP_Y), ICON, ICON};
    return rect;
}

static SDL_Rect GameSkills__TabRect(const int page) {
    const SDL_Rect rect = {GAME_PANEL_X + TREE_TAB_X, TREE_TAB_Y + ((3 - page) * TREE_TAB_STEP), TREE_TAB_W,
                           TREE_TAB_H};
    return rect;
}

static int GameSkills__TreeSkillAt(const GameSkills *skills, const int x, const int y) {
    for (int s = 0; s < skills->table->count; s++) {
        const SkillDef *skill = &skills->table->skills[s];
        const SDL_Rect  rect  = GameSkills__IconRect(skill);
        if (skill->page == skills->page && GameSkills__In(&rect, x, y)) {
            return s;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// The pickers: the skills that can go on that button, a row per tree above it (Attack in the bottom row)

#define PICKER_MAX 48

static int GameSkills__PickerList(const GameSkills *skills, const Character *character, const bool left, int *list,
                                  int *rows) {
    int count     = 0;
    list[count++] = SKILL_ATTACK;
    rows[0]       = 0;
    // Skills from items (charges, another class's) beside Attack
    int       from_items[16];
    const int items = Affixes_ItemSkills(character, from_items, 16);
    for (int i = 0; i < items && count < PICKER_MAX; i++) {
        if (Skills_CanAssign(character, SKILL_FROM_ITEM + from_items[i], left)) {
            rows[count]   = 0;
            list[count++] = SKILL_FROM_ITEM + from_items[i];
        }
    }
    for (int page = 1; page <= SKILL_PAGES; page++) {
        for (int s = 0; s < skills->table->count && count < PICKER_MAX; s++) {
            if (skills->table->skills[s].page == page && Skills_CanAssign(character, s, left)) {
                rows[count]   = page;
                list[count++] = s;
            }
        }
    }
    return count;
}

// Where each icon of the open picker is: rows going up from above the button, left to right (right: right to left)
static void GameSkills__PickerRects(const GameSkills *skills, const Character *character, int *list, SDL_Rect *rects,
                                    int *count) {
    int rows[PICKER_MAX];
    *count               = GameSkills__PickerList(skills, character, skills->picker == 1, list, rows);
    int        row_index = -1;
    int        last_row  = -1;
    int        in_row    = 0;
    const bool right     = skills->picker == 2;
    for (int i = 0; i < *count; i++) {
        if (rows[i] != last_row) {
            last_row = rows[i];
            row_index++;
            in_row = 0;
        }
        const int x = right ? GAME_SKILL_RIGHT_X - (in_row * ICON) : GAME_SKILL_LEFT_X + (in_row * ICON);
        rects[i]    = (SDL_Rect){x, GAME_SKILL_Y - ((row_index + 1) * ICON) - 4, ICON, ICON};
        in_row++;
    }
}

bool GameSkills_PickerOpen(const GameSkills *skills) { return skills->picker != 0; }

int GameSkills_PickerSkillAt(const GameSkills *skills, const Character *character, const int mouse_x, const int mouse_y,
                             bool *left) {
    if (skills->picker == 0) {
        return SKILL_NONE_PICKED;
    }
    int      list[PICKER_MAX];
    SDL_Rect rects[PICKER_MAX];
    int      count = 0;
    GameSkills__PickerRects(skills, character, list, rects, &count);
    for (int i = 0; i < count; i++) {
        if (GameSkills__In(&rects[i], mouse_x, mouse_y) && list[i] < SKILL_FROM_ITEM) {
            *left = skills->picker == 1;
            return list[i];
        }
    }
    return SKILL_NONE_PICKED;
}
void GameSkills_ClosePicker(GameSkills *skills) { skills->picker = 0; }

static int GameSkills__ButtonAt(const int x, const int y) {
    const SDL_Rect left  = {GAME_SKILL_LEFT_X, GAME_SKILL_Y, ICON, ICON};
    const SDL_Rect right = {GAME_SKILL_RIGHT_X, GAME_SKILL_Y, ICON, ICON};
    return GameSkills__In(&left, x, y) ? 1 : (GameSkills__In(&right, x, y) ? 2 : 0);
}

bool GameSkills_IsOver(const GameSkills *skills, const bool tree_open, const int mouse_x, const int mouse_y) {
    if ((tree_open && GameInventory_IsOver(mouse_x, mouse_y)) || GameSkills__ButtonAt(mouse_x, mouse_y) != 0) {
        return true;
    }
    return skills->picker != 0; // an open picker takes the next click wherever it is
}

// ---------------------------------------------------------------------------------------------------------------------
// Clicks

int GameSkills_Update(GameSkills *skills, Character *character, const bool tree_open, const int mouse_x,
                      const int mouse_y, const bool left_click, const char **message) {
    *message = NULL;
    if (!left_click) {
        return SKILLS_EVENT_NONE;
    }
    if (skills->picker != 0) {
        int      list[PICKER_MAX];
        SDL_Rect rects[PICKER_MAX];
        int      count = 0;
        GameSkills__PickerRects(skills, character, list, rects, &count);
        int events = SKILLS_EVENT_CLICKED;
        for (int i = 0; i < count; i++) {
            if (GameSkills__In(&rects[i], mouse_x, mouse_y)) {
                *(skills->picker == 1 ? &character->left_skill : &character->right_skill)  = list[i];
                events                                                                    |= SKILLS_EVENT_CHOSEN;
            }
        }
        skills->picker = 0;
        return events;
    }
    const int button = GameSkills__ButtonAt(mouse_x, mouse_y);
    if (button != 0) {
        skills->picker = button;
        return SKILLS_EVENT_CLICKED;
    }
    if (!tree_open || !GameInventory_IsOver(mouse_x, mouse_y)) {
        return SKILLS_EVENT_NONE;
    }
    for (int page = 1; page <= SKILL_PAGES; page++) {
        const SDL_Rect tab = GameSkills__TabRect(page);
        if (GameSkills__In(&tab, mouse_x, mouse_y)) {
            skills->page = page;
            return SKILLS_EVENT_CLICKED;
        }
    }
    const int skill = GameSkills__TreeSkillAt(skills, mouse_x, mouse_y);
    if (skill >= 0) {
        const char *why = NULL;
        if (Skills_Raise(character, skill)) {
            return SKILLS_EVENT_CLICKED | SKILLS_EVENT_RAISED;
        }
        Skills_CanRaise(character, skill, &why);
        *message = why;
    }
    return SKILLS_EVENT_CLICKED;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing

static void GameSkills__Text(Label *label, const char *text, const int x, const int y, const uint8_t r, const uint8_t g,
                             const uint8_t b) {
    Label_SetText(label, text);
    Label_SetColor(label, r, g, b, 0xFF);
    Label_SetAlignment(label, LABEL_ALIGN_CENTER, LABEL_ALIGN_CENTER);
    Label_Draw(label, x, y);
}

// A skill's icon (SKILL_ATTACK: the shared Attack icon); dimmed: darkened, the game's look for skills not available yet
// A skill from an item: the class it belongs to and its place in that class's list (false: no class's)
static bool GameSkills__ItemSkill(const int skill, CharacterClass *owner, const SkillDef **def) {
    const SkillRow *row = SkillData_ByRow(skill - SKILL_FROM_ITEM);
    for (int c = 0; row != NULL && c < CHARACTER_CLASS_COUNT; c++) {
        const SkillClass *table = Skills_ForClass((CharacterClass)c);
        const int         k     = Skills_Find(table, row->id);
        if (k >= 0) {
            *owner = (CharacterClass)c;
            *def   = &table->skills[k];
            return true;
        }
    }
    return false;
}

static void GameSkills__DrawIcon(GameSkills *skills, const int skill, const int x, const int y, const bool dimmed) {
    CharacterClass  owner = (CharacterClass)0;
    const SkillDef *def   = NULL;
    if (skill >= SKILL_FROM_ITEM) {
        if (GameSkills__ItemSkill(skill, &owner, &def)) {
            const SkillClass *table = Skills_ForClass(owner);
            if (skills->others[owner] == NULL && FileManager_FileExists(table->icon_path)) {
                skills->others[owner] = Sprite_Create(table->icon_path, PALETTE_SKY);
            }
            if (skills->others[owner] != NULL) {
                Sprite_DrawScaled(skills->others[owner], def->icon_frame, x, y, 1.0f);
            }
        } else {
            Sprite_DrawScaled(skills->shared, ATTACK_ICON, x, y, 1.0f);
        }
    } else if (skill == SKILL_ATTACK || skill < 0) {
        Sprite_DrawScaled(skills->shared, ATTACK_ICON, x, y, 1.0f);
    } else if (skills->icons != NULL) {
        Sprite_DrawScaled(skills->icons, skills->table->skills[skill].icon_frame, x, y, 1.0f);
    }
    if (dimmed) {
        const SDL_Rect rect = {x, y, ICON, ICON};
        SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 150);
        SDL_RenderFillRect(sdl_renderer, &rect);
    }
}

void GameSkills_DrawTree(GameSkills *skills, const Character *character, const int mouse_x, const int mouse_y) {
    if (skills->tree != NULL) {
        Sprite_DrawMulti(skills->tree, 0, GAME_PANEL_X, 0, 2, 2);
        Sprite_DrawMulti(skills->tree, (uint8_t)(skills->page * 4), GAME_PANEL_X, 0, 2, 2);
    }
    // The tabs' names, and the points left to spend above them
    for (int page = 1; page <= SKILL_PAGES; page++) {
        const SDL_Rect tab   = GameSkills__TabRect(page);
        int            lines = 0;
        for (int l = 0; l < SKILL_TAB_LINES; l++) {
            lines += skills->table->tab_lines[page - 1][l][0] != '\0' ? 1 : 0;
        }
        int y = tab.y + (tab.h / 2) - ((lines * 16) / 2) + 8;
        for (int l = 0; l < SKILL_TAB_LINES; l++) {
            if (skills->table->tab_lines[page - 1][l][0] == '\0') {
                continue;
            }
            const bool chosen = page == skills->page;
            GameSkills__Text(skills->text, skills->table->tab_lines[page - 1][l], tab.x + (tab.w / 2), y,
                             chosen ? 0xFF : 0x90, chosen ? 0xFF : 0x90, chosen ? 0xFF : 0x90);
            y += 16;
        }
    }
    GameSkills__Text(skills->small, "Skill Choices", GAME_PANEL_X + TREE_TAB_X + (TREE_TAB_W / 2), 30, 0xC7, 0xB3,
                     0x77);
    GameSkills__Text(skills->small, "Remaining", GAME_PANEL_X + TREE_TAB_X + (TREE_TAB_W / 2), 44, 0xC7, 0xB3, 0x77);
    char text[16];
    snprintf(text, sizeof(text), "%d", character->skill_points);
    GameSkills__Text(skills->text, text, GAME_PANEL_X + TREE_TAB_X + (TREE_TAB_W / 2), 72,
                     character->skill_points > 0 ? 0x40 : 0xFF, 0xFF, character->skill_points > 0 ? 0x40 : 0xFF);

    // The skills: bright once learned or ready to learn, dimmed until then; the level beside each
    const int hovered = GameSkills__TreeSkillAt(skills, mouse_x, mouse_y);
    for (int s = 0; s < skills->table->count; s++) {
        const SkillDef *skill = &skills->table->skills[s];
        if (skill->page != skills->page) {
            continue;
        }
        const SDL_Rect rect  = GameSkills__IconRect(skill);
        const int      level = character->skill_levels[s];
        const int      bonus = Affixes_SkillBonus(character, s); // +skills from what's worn
        const bool     ready = level > 0 || bonus > 0 || Skills_CanRaise(character, s, NULL);
        GameSkills__DrawIcon(skills, s, rect.x, rect.y, !ready);
        if (level + bonus > 0) {
            snprintf(text, sizeof(text), "%d", level + bonus);
            GameSkills__Text(skills->text, text, rect.x + rect.w + 8, rect.y + rect.h - 6, bonus > 0 ? 0x69 : 0xFF,
                             bonus > 0 ? 0x69 : 0xFF, 0xFF); // (blue when items add to it, like the game)
        }
        if (s == hovered) {
            SDL_SetRenderDrawColor(sdl_renderer, 230, 200, 110, 255);
            SDL_RenderDrawRect(sdl_renderer, &rect);
        }
    }
}

void GameSkills_DrawButtons(GameSkills *skills, const Character *character) {
    GameSkills__DrawIcon(skills, character->left_skill, GAME_SKILL_LEFT_X, GAME_SKILL_Y, false);
    GameSkills__DrawIcon(skills, character->right_skill, GAME_SKILL_RIGHT_X, GAME_SKILL_Y, false);
}

// A box of lines near (x, y), kept on the screen
static void GameSkills__Tip(GameSkills *skills, char lines[][128], uint8_t colors[][3], const int count, const int x,
                            const int y) {
    int width = 0;
    for (int l = 0; l < count; l++) {
        int w = 0;
        Label_SetText(skills->text, lines[l]);
        Label_GetSize(skills->text, &w, NULL);
        width = w > width ? w : width;
    }
    const int line_h   = 16;
    int       box_x    = x - (width / 2) - 6;
    int       box_y    = y - (count * line_h) - 12;
    box_x              = box_x < 2 ? 2 : (box_x + width + 12 > 798 ? 798 - width - 12 : box_x);
    box_y              = box_y < 2 ? y + ICON + 4 : box_y; // no room above: below the icon
    const SDL_Rect box = {box_x, box_y, width + 12, (count * line_h) + 8};
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(sdl_renderer, &box);
    for (int l = 0; l < count; l++) {
        GameSkills__Text(skills->text, lines[l], box.x + (box.w / 2), box.y + 4 + (l * line_h) + (line_h / 2),
                         colors[l][0], colors[l][1], colors[l][2]);
    }
}

static void GameSkills__AddLine(char lines[][128], uint8_t colors[][3], int *count, const char *text, const uint8_t r,
                                const uint8_t g, const uint8_t b) {
    if (*count < TIP_LINES) {
        snprintf(lines[*count], 128, "%s", text);
        colors[*count][0] = r;
        colors[*count][1] = g;
        colors[*count][2] = b;
        (*count)++;
    }
}

// Long text wrapped to the tip's width, a line at a time
static void GameSkills__AddWrapped(GameSkills *skills, char lines[][128], uint8_t colors[][3], int *count,
                                   const char *text) {
    char buffer[SKILL_TEXT_MAX * 3];
    snprintf(buffer, sizeof(buffer), "%s", text);
    for (char *c = buffer; *c != '\0'; c++) {
        *c = *c == '\n' ? ' ' : *c;
    }
    char line[128] = "";
    for (char *word = strtok(buffer, " "); word != NULL; word = strtok(NULL, " ")) {
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "%s%s%s", line, line[0] != '\0' ? " " : "", word);
        Label_SetText(skills->text, candidate);
        int w = 0;
        Label_GetSize(skills->text, &w, NULL);
        if (line[0] != '\0' && w > TIP_WIDTH) {
            GameSkills__AddLine(lines, colors, count, line, 0xC0, 0xC0, 0xC0);
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%.127s", candidate);
        }
    }
    if (line[0] != '\0') {
        GameSkills__AddLine(lines, colors, count, line, 0xC0, 0xC0, 0xC0);
    }
}

static void GameSkills__SkillTip(GameSkills *skills, const Character *character, const int skill, const int x,
                                 const int y, const bool tree) {
    char    lines[TIP_LINES][128];
    uint8_t colors[TIP_LINES][3];
    int     count = 0;
    char    text[128];
    if (skill == SKILL_ATTACK) {
        GameSkills__AddLine(lines, colors, &count, "Attack", 0xFF, 0xFF, 0xFF);
        GameSkills__AddLine(lines, colors, &count, "Your weapon's own attack", 0xC0, 0xC0, 0xC0);
        GameSkills__Tip(skills, lines, colors, count, x, y);
        return;
    }
    if (skill >= SKILL_FROM_ITEM) {
        CharacterClass  owner   = (CharacterClass)0;
        const SkillDef *def     = NULL;
        bool            charged = false;
        const int       level   = Affixes_ItemSkillLevel(character, skill - SKILL_FROM_ITEM, &charged);
        if (GameSkills__ItemSkill(skill, &owner, &def)) {
            GameSkills__AddLine(lines, colors, &count, def->name, 0x60, 0xE0, 0x60);
            if (def->short_desc[0] != '\0') {
                GameSkills__AddWrapped(skills, lines, colors, &count, def->short_desc);
            }
        }
        snprintf(text, sizeof(text), "Skill Level: %d", level);
        GameSkills__AddLine(lines, colors, &count, text, 0xFF, 0xFF, 0xFF);
        GameSkills__AddLine(lines, colors, &count, charged ? "From an item's charges" : "From an item", 0x90, 0x90,
                            0xFF);
        GameSkills__Tip(skills, lines, colors, count, x, y);
        return;
    }
    const SkillDef *def   = &skills->table->skills[skill];
    const int       bonus = Affixes_SkillBonus(character, skill);
    const int       level = character->skill_levels[skill] + bonus;
    GameSkills__AddLine(lines, colors, &count, def->name, 0x60, 0xE0, 0x60);
    if (def->short_desc[0] != '\0') {
        GameSkills__AddWrapped(skills, lines, colors, &count, def->short_desc);
    }
    snprintf(text, sizeof(text), level > 0 ? "Current Skill Level: %d" : "Not learned yet", level);
    GameSkills__AddLine(lines, colors, &count, text, 0xFF, 0xFF, 0xFF);
    if (def->passive) {
        GameSkills__AddLine(lines, colors, &count, "Passive", 0x90, 0x90, 0xFF);
    } else if (def->mana_cost > 0) {
        snprintf(text, sizeof(text), "Mana Cost: %d", def->mana_cost);
        GameSkills__AddLine(lines, colors, &count, text, 0x90, 0x90, 0xFF);
    }
    if (tree) {
        const char *why = NULL;
        if (Skills_CanRaise(character, skill, &why)) {
            GameSkills__AddLine(lines, colors, &count, "Click to add a point", 0xC7, 0xB3, 0x77);
        } else if (why != NULL && level < SKILL_MAX_LEVEL) {
            GameSkills__AddLine(lines, colors, &count, why, 0xFF, 0x50, 0x50);
        }
    } else if (!def->passive) {
        GameSkills__AddLine(lines, colors, &count, "(casting isn't in the engine yet)", 0x90, 0x90, 0x90);
    }
    GameSkills__Tip(skills, lines, colors, count, x, y);
}

// A skill key's name in a picker icon's corner
static void GameSkills__HotkeyLabel(GameSkills *skills, const int hotkey, const int x, const int y) {
    char text[8];
    snprintf(text, sizeof(text), "F%d", hotkey + 1);
    Label_SetText(skills->small, text);
    Label_SetColor(skills->small, 0xFF, 0xE0, 0x80, 0xFF);
    Label_SetAlignment(skills->small, LABEL_ALIGN_BEGIN, LABEL_ALIGN_BEGIN);
    Label_Draw(skills->small, x, y);
}

void GameSkills_DrawOverlay(GameSkills *skills, const Character *character, const bool tree_open, const int mouse_x,
                            const int mouse_y) {
    if (skills->picker != 0) {
        int      list[PICKER_MAX];
        SDL_Rect rects[PICKER_MAX];
        int      count = 0;
        GameSkills__PickerRects(skills, character, list, rects, &count);
        int hovered = -1;
        for (int i = 0; i < count; i++) {
            GameSkills__DrawIcon(skills, list[i], rects[i].x, rects[i].y, false);
            // Its skill key, if it has one for this button ("F3" in the corner, like the game)
            for (int h = 0; h < 8; h++) {
                if (character->hotkey_set[h] && character->hotkey_skill[h] == list[i] &&
                    character->hotkey_left[h] == (skills->picker == 1)) {
                    GameSkills__HotkeyLabel(skills, h, rects[i].x + 2, rects[i].y + 2);
                }
            }
            if (GameSkills__In(&rects[i], mouse_x, mouse_y)) {
                hovered = i;
                SDL_SetRenderDrawColor(sdl_renderer, 230, 200, 110, 255);
                SDL_RenderDrawRect(sdl_renderer, &rects[i]);
            }
        }
        if (hovered >= 0) {
            GameSkills__SkillTip(skills, character, list[hovered], rects[hovered].x + (ICON / 2), rects[hovered].y,
                                 false);
        }
        return;
    }
    const int button = GameSkills__ButtonAt(mouse_x, mouse_y);
    if (button != 0) {
        GameSkills__SkillTip(skills, character, button == 1 ? character->left_skill : character->right_skill,
                             (button == 1 ? GAME_SKILL_LEFT_X : GAME_SKILL_RIGHT_X) + (ICON / 2), GAME_SKILL_Y, false);
        return;
    }
    if (tree_open) {
        const int skill = GameSkills__TreeSkillAt(skills, mouse_x, mouse_y);
        if (skill >= 0) {
            const SDL_Rect rect = GameSkills__IconRect(&skills->table->skills[skill]);
            GameSkills__SkillTip(skills, character, skill, rect.x + (ICON / 2), rect.y, true);
        }
    }
}
