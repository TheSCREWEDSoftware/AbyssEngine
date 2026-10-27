#include "StateOverlays.h"
#include "../data/DataTable.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static DataTable *states   = NULL;
static DataTable *overlays = NULL;
static bool       loaded   = false;

static void StateOverlays__Load(void) {
    if (loaded) {
        return;
    }
    loaded   = true;
    states   = DataTable_Load("/data/global/excel/states.txt");
    overlays = DataTable_Load("/data/global/excel/overlay.txt");
}

int StateOverlays_For(const char *state, StateOverlay *out, const int max) {
    StateOverlays__Load();
    if (state == NULL || state[0] == '\0' || states == NULL || overlays == NULL) {
        return 0;
    }
    const int c_state = DataTable_GetColumnIndex(states, "state");
    int       row     = -1;
    for (int r = 0; r < DataTable_GetRowCount(states) && row < 0; r++) {
        row = strcasecmp(DataTable_GetString(states, r, c_state), state) == 0 ? r : -1;
    }
    if (row < 0) {
        return 0;
    }
    const int c_name = DataTable_GetColumnIndex(overlays, "overlay");
    const int c_file = DataTable_GetColumnIndex(overlays, "Filename");
    const int c_x    = DataTable_GetColumnIndex(overlays, "Xoffset");
    const int c_y    = DataTable_GetColumnIndex(overlays, "Yoffset");
    const int c_pre  = DataTable_GetColumnIndex(overlays, "PreDraw");
    const int c_tr   = DataTable_GetColumnIndex(overlays, "Trans");
    int       count  = 0;
    for (int o = 1; o <= STATE_OVERLAYS_MAX && count < max; o++) {
        char column[16];
        snprintf(column, sizeof(column), "overlay%d", o);
        const char *name = DataTable_GetString(states, row, DataTable_GetColumnIndex(states, column));
        if (name[0] == '\0') {
            continue;
        }
        for (int r = 0; r < DataTable_GetRowCount(overlays); r++) {
            if (strcasecmp(DataTable_GetString(overlays, r, c_name), name) != 0) {
                continue;
            }
            StateOverlay *overlay = &out[count++];
            snprintf(overlay->file, sizeof(overlay->file), "%s", DataTable_GetString(overlays, r, c_file));
            overlay->x      = DataTable_GetInt(overlays, r, c_x);
            overlay->y      = DataTable_GetInt(overlays, r, c_y);
            overlay->behind = DataTable_GetInt(overlays, r, c_pre) != 0;
            overlay->glow   = DataTable_GetInt(overlays, r, c_tr) != 0;
            break;
        }
    }
    return count;
}

void StateOverlays_Release(void) {
    DataTable_Destroy(&states);
    DataTable_Destroy(&overlays);
    loaded = false;
}
