#ifndef ABYSS_DT1_H
#define ABYSS_DT1_H

#include <stdbool.h>
#include <stdint.h>

// DT1: a set of map tiles (floors, walls, shadows, roofs). Each tile is drawn from blocks of palette indexed
// pixels; they are decoded once into a single image per tile.

// Tile orientations
#define DT1_ORIENTATION_FLOOR      0
#define DT1_ORIENTATION_LEFT_WALL  1
#define DT1_ORIENTATION_RIGHT_WALL 2
#define DT1_ORIENTATION_NW_RIGHT   3 // north-west corner, right half (drawn together with 4)
#define DT1_ORIENTATION_NW_LEFT    4
#define DT1_ORIENTATION_SPECIAL_1  10 // warps, spawn markers: not drawn by the game
#define DT1_ORIENTATION_SPECIAL_2  11
#define DT1_ORIENTATION_SHADOW     13
#define DT1_ORIENTATION_TREE       14
#define DT1_ORIENTATION_ROOF       15
#define DT1_ORIENTATION_LOWER_WALL 16 // 16..19: the part of a wall below the floor line

#define DT1_FLOOR_WIDTH  160
#define DT1_FLOOR_HEIGHT 80

// Sub-tile flags (5x5 per tile)
#define DT1_SUBTILE_BLOCK_WALK   0x01
#define DT1_SUBTILE_BLOCK_LIGHT  0x02 // and line of sight
#define DT1_SUBTILE_BLOCK_JUMP   0x04
#define DT1_SUBTILE_BLOCK_PLAYER 0x08

// Floor materials (what footsteps sound like)
#define DT1_MATERIAL_WATER         0x0002
#define DT1_MATERIAL_INSIDE_STONE  0x0008
#define DT1_MATERIAL_OUTSIDE_STONE 0x0010
#define DT1_MATERIAL_DIRT          0x0020
#define DT1_MATERIAL_SAND          0x0040
#define DT1_MATERIAL_WOOD          0x0080
#define DT1_MATERIAL_LAVA          0x0100
#define DT1_MATERIAL_SNOW          0x0400

typedef struct DT1Tile DT1Tile;
struct DT1Tile {
    int32_t  direction;
    int16_t  roof_height;
    uint16_t material;
    int32_t  height;
    int32_t  width;
    int32_t  orientation;
    int32_t  main_index; // "style"
    int32_t  sub_index;  // "sequence"
    int32_t  rarity;     // 0 = never picked at random
    uint8_t  subtile_flags[25];

    // Decoded image: pixel (x, y) of the tile is image[(y - image_y) * image_width + (x - image_x)], 0 = transparent.
    // Tile coordinates: floors span (0,0)-(160,80) with the diamond's top corner at (80,0); walls extend upwards
    // into negative y from the floor line.
    int      image_x;
    int      image_y;
    int      image_width;
    int      image_height;
    uint8_t *image;

    // Animated floors (lava, some water): all frames of the tile's animation in order, filled in by the map that
    // uses the tile set. 0/NULL for still tiles.
    int                    anim_frame_count;
    const struct DT1Tile **anim_frames;
};

typedef struct DT1 DT1;

DT1           *DT1_Load(const char *path); // NULL if the file is missing or broken
void           DT1_Destroy(DT1 **dt1);
int            DT1_GetTileCount(const DT1 *dt1);
bool           DT1_IsPictureless(const DT1 *dt1); // only the tiles' headers (--extract --small): no pictures
const DT1Tile *DT1_GetTile(const DT1 *dt1, int index);

#endif // ABYSS_DT1_H
