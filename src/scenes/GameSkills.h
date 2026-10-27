#ifndef ABYSS_GAME_SKILLS_H
#define ABYSS_GAME_SKILLS_H

#include "../game/Character.h"
#include <stdbool.h>

// Single Player's skill tree, the game's own panel (skltree_<x>_back.dc6) on the right: the class's three trees on
// tabs, each skill's icon (dimmed until it can be learned) with its level, the points left to spend. Click a skill to
// put a point in it. And the pickers on the control panel: click the left or right skill to choose what the mouse
// buttons use.

// The control panel's skill buttons (48 x 48): where the panel puts them (Classic: 117 and 635, at y = 552)
extern int game_skill_left_x;
extern int game_skill_right_x;
extern int game_skill_y;
#define GAME_SKILL_LEFT_X  game_skill_left_x
#define GAME_SKILL_RIGHT_X game_skill_right_x
#define GAME_SKILL_Y       game_skill_y
void GameSkills_SetButtons(int left_x, int right_x, int y);

typedef struct GameSkills GameSkills;

typedef enum GameSkillsEvent {
    SKILLS_EVENT_NONE    = 0,
    SKILLS_EVENT_RAISED  = 1, // a point was spent
    SKILLS_EVENT_CHOSEN  = 2, // the left or right skill changed
    SKILLS_EVENT_CLICKED = 4, // the click was the skills' (not the world's)
} GameSkillsEvent;

GameSkills *GameSkills_Create(CharacterClass class_id);
void        GameSkills_Destroy(GameSkills **skills);

// Clicks (the moment the button goes down). Returns GameSkillsEvent flags; *message is for the player, or NULL.
int  GameSkills_Update(GameSkills *skills, Character *character, bool tree_open, int mouse_x, int mouse_y,
                       bool left_click, const char **message);
bool GameSkills_PickerOpen(const GameSkills *skills);
// The skill under the mouse in the open picker (SKILL_NONE_PICKED if none), and whether it's the left button's
#define SKILL_NONE_PICKED (-1000)
int  GameSkills_PickerSkillAt(const GameSkills *skills, const Character *character, int mouse_x, int mouse_y,
                              bool *left);
void GameSkills_ClosePicker(GameSkills *skills);
bool GameSkills_IsOver(const GameSkills *skills, bool tree_open, int mouse_x, int mouse_y);

void GameSkills_DrawTree(GameSkills *skills, const Character *character, int mouse_x, int mouse_y);
void GameSkills_DrawButtons(GameSkills *skills, const Character *character); // the left and right skill
void GameSkills_DrawOverlay(GameSkills *skills, const Character *character, bool tree_open, int mouse_x,
                            int mouse_y); // pickers and tooltips, on top

#endif // ABYSS_GAME_SKILLS_H
