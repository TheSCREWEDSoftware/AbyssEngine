#include "MissileData.h"
#include "../common/Logging.h"
#include "../data/DataTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define EXCEL_MISSILES "/data/global/excel/Missiles.txt"

static MissileInfo *missiles      = NULL;
static int          missile_count = -1;

static void MissileData__Load(void) {
    if (missile_count >= 0) {
        return;
    }
    missile_count   = 0;
    DataTable *data = DataTable_Load(EXCEL_MISSILES);
    if (data == NULL) {
        LOG_WARN("No Missiles.txt: no missiles");
        return;
    }
    const int rows = DataTable_GetRowCount(data);
    missiles       = calloc(rows > 0 ? (size_t)rows : 1, sizeof(MissileInfo));
    FAIL_IF_NULL(missiles);
#define COL(name)        DataTable_GetColumnIndex(data, name)
#define INT(name)        DataTable_GetInt(data, r, COL(name))
#define STR(field, name) snprintf(field, sizeof(field), "%s", DataTable_GetString(data, r, COL(name)))
    for (int r = 0; r < rows; r++) {
        if (DataTable_GetString(data, r, COL("Missile"))[0] == '\0') {
            continue;
        }
        MissileInfo *m = &missiles[missile_count++];
        STR(m->name, "Missile");
        // The game moves missiles Vel / 8 sub-tiles a frame (25 frames a second) and lets them fly Range frames
        m->speed     = (float)INT("Vel") * 25.0f / 8.0f;
        m->max_speed = (float)INT("MaxVel") * 25.0f / 8.0f;
        m->accel     = (float)INT("Accel") * 25.0f / 8.0f / 100.0f * 25.0f;
        m->seconds   = (float)INT("Range") / 25.0f;
        STR(m->cel_file, "CelFile");
        m->frames     = INT("AnimLen") > 0 ? INT("AnimLen") : 1;
        m->frame_rate = (INT("animrate") > 0 ? (float)INT("animrate") : 1024.0f) / 1024.0f * 25.0f;
        m->directions = INT("NumDirections") > 0 ? INT("NumDirections") : 1;
        m->loop       = INT("LoopAnim") != 0;
        m->glow       = INT("Trans") != 0;
        m->light      = INT("Light");
        m->size       = INT("Size") > 0 ? INT("Size") : 1;
        m->pierce     = INT("Pierce") != 0;
        m->hit_func   = INT("pSrvHitFunc");
        m->explosion  = INT("Explosion") != 0 || INT("Vel") == 0;
        STR(m->explode_into, "ExplosionMissile");
        STR(m->sub_missile, "SubMissile1");
        STR(m->hit_missile, "HitSubMissile1");
        STR(m->travel_sound, "TravelSound");
        STR(m->hit_sound, "HitSound");
        if (m->max_speed < m->speed) {
            m->max_speed = m->speed;
        }
    }
#undef COL
#undef INT
#undef STR
    DataTable_Destroy(&data);
}

const MissileInfo *MissileData_Get(const char *name) {
    MissileData__Load();
    for (int i = 0; i < missile_count && name != NULL && name[0] != '\0'; i++) {
        if (strcasecmp(missiles[i].name, name) == 0) {
            return &missiles[i];
        }
    }
    return NULL;
}

void MissileData_Release(void) {
    free(missiles);
    missiles      = NULL;
    missile_count = -1;
}
