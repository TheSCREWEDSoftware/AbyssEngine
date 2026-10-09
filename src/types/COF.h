#ifndef ABYSS_COF_H
#define ABYSS_COF_H

#include <stdbool.h>
#include <stdint.h>

// COF: describes how an animation is assembled from DCC layers (head, torso, legs, arms, weapons, specials):
// which layers exist, their weapon class and draw effect, and the drawing order for every direction and frame.
#define COF_MAX_LAYERS     16
#define COF_COMPONENT_NONE 0xFF

typedef enum COFComponent {
    COF_COMPONENT_HD, // head
    COF_COMPONENT_TR, // torso
    COF_COMPONENT_LG, // legs
    COF_COMPONENT_RA, // right arm
    COF_COMPONENT_LA, // left arm
    COF_COMPONENT_RH, // right hand (weapon)
    COF_COMPONENT_LH, // left hand
    COF_COMPONENT_SH, // shield
    COF_COMPONENT_S1, // special 1..8
    COF_COMPONENT_S2,
    COF_COMPONENT_S3,
    COF_COMPONENT_S4,
    COF_COMPONENT_S5,
    COF_COMPONENT_S6,
    COF_COMPONENT_S7,
    COF_COMPONENT_S8,
    COF_COMPONENT_COUNT
} COFComponent;

typedef struct COFLayer {
    int  component;
    bool shadow;
    bool transparent;
    int  draw_effect;
    char weapon_class[4];
} COFLayer;

typedef struct COF COF;

COF            *COF_Load(const char *path); // NULL when missing or malformed
void            COF_Destroy(COF **cof);
const char     *COF_ComponentCode(int component); // "hd", "tr", ...
int             COF_GetLayerCount(const COF *cof);
int             COF_GetDirectionCount(const COF *cof);
int             COF_GetFramesPerDirection(const COF *cof);
int             COF_GetAnimationRate(const COF *cof); // 256 = one frame per game tick (25 per second)
const COFLayer *COF_GetLayer(const COF *cof, int layer);
// Component drawn at position `order` (back to front) for the given direction and frame.
int COF_GetDrawOrder(const COF *cof, int direction, int frame, int order);

#endif // ABYSS_COF_H
