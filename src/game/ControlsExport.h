#ifndef ABYSS_CONTROLS_EXPORT_H
#define ABYSS_CONTROLS_EXPORT_H

#include <stdbool.h>
#include <stdio.h>

// The controls for reading outside the game: a Markdown table of every action and its inputs, a keyboard diagram
// coloured by group with the actions on each key (like the guides people print), and a gamepad diagram (triggers,
// bumpers, sticks, D-pad and buttons with what each does), each light or dark. Options › Controls › Export writes them
// next to abyss.ini; the wiki's Controls page has the defaults.
bool ControlsExport_Markdown(int profile, const char *path, bool pictures); // pictures: the four SVGs, folded away
bool ControlsExport_Svg(int profile, const char *path, bool dark);          // controls-keyboard(-dark).svg
bool ControlsExport_PadSvg(int profile, const char *path, bool dark);       // controls-gamepad(-dark).svg

// The working actions of a mode (CONTROL_CONTEXT_GAME for Single Player, CONTROL_CONTEXT_DEBUG for Free Roam and the
// Testing Grounds) as one table with what each does: the wiki's key tables come from it (AbyssEngine --export-controls)
void ControlsExport_ModeTable(FILE *file, int profile, unsigned contexts);

#endif // ABYSS_CONTROLS_EXPORT_H
