#ifndef ABYSS_CONTROL_PRESETS_H
#define ABYSS_CONTROL_PRESETS_H

#include <stdbool.h>

// Sets of bindings saved under a name, like ArmA's presets: presets/<name>.ini next to abyss.ini, in the same
// "action = inputs" lines as options.ini, so they can be shared and edited by hand too.
#define CONTROL_PRESET_NAME_MAX 48

int  ControlPresets_List(char names[][CONTROL_PRESET_NAME_MAX], int max); // sorted by name
bool ControlPresets_Save(int profile, const char *name);                  // that profile's bindings
bool ControlPresets_Load(int profile, const char *name);                  // into that profile (it gets its own)
bool ControlPresets_Delete(const char *name);

#endif // ABYSS_CONTROL_PRESETS_H
