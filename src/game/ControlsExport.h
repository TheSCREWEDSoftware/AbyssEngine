#ifndef ABYSS_CONTROLS_EXPORT_H
#define ABYSS_CONTROLS_EXPORT_H

#include <stdbool.h>

// The controls for reading outside the game: a Markdown table of every action and its inputs, and a keyboard
// diagram (SVG) coloured by group with the actions on each key, like the guides people print. Options › Controls ›
// Export writes them next to abyss.ini; the wiki's Controls page has the defaults.
bool ControlsExport_Markdown(int profile, const char *path, const char *svg_name); // svg_name: pictured in it, or NULL
bool ControlsExport_Svg(int profile, const char *path);

#endif // ABYSS_CONTROLS_EXPORT_H
