#ifndef POKEPLATINUM_PORT_GEAR_MAP_H
#define POKEPLATINUM_PORT_GEAR_MAP_H

// The Gear's map of the area around the player, from the map's own data: each tile's
// terrain (the land data's tile behaviors and collision), the current map's events (warps,
// item balls, hidden items) and the save's flags (items already picked up). ROM hacks show
// their own maps and items: the item in a ball comes from its script in the ROM.
//
// The area is the current map's blocks of the map matrix (32x32 tiles each) and the blocks
// around them, where neighbouring maps show dimmed. What you can walk on is found by a
// flood from the player, the warps and the map's people and items (through floors, water,
// ledges, doors...), so the empty space around a cave or beyond a map's edge stays empty.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEAR_MAP_MAX_ITEMS 64
#define GEAR_MAP_MAX_WARPS 64

enum GearMapTile {
    GEAR_MAP_VOID = 0, // nothing you can walk on
    GEAR_MAP_WALL,     // blocked, next to somewhere you can walk
    GEAR_MAP_FLOOR,
    GEAR_MAP_GRASS,
    GEAR_MAP_TALL_GRASS,
    GEAR_MAP_WATER,
    GEAR_MAP_SAND,
    GEAR_MAP_SNOW,
    GEAR_MAP_ICE,
    GEAR_MAP_MUD,
    GEAR_MAP_CAVE,
    GEAR_MAP_LEDGE_NORTH, // jumped down northward
    GEAR_MAP_LEDGE_SOUTH,
    GEAR_MAP_LEDGE_WEST,
    GEAR_MAP_LEDGE_EAST,
    GEAR_MAP_WARP, // doors, stairs, exits
    GEAR_MAP_BRIDGE,
    GEAR_MAP_ROCK_CLIMB,
    GEAR_MAP_WATERFALL,
    GEAR_MAP_SOIL, // berry patch
    GEAR_MAP_TILE_COUNT,
};

#define GEAR_MAP_OTHER_AREA 0x80 // flag on a tile: a neighbouring map's
#define GEAR_MAP_TILE_MASK  0x7F

typedef struct GearMapItem {
    s16 x; // tiles, from the map's origin
    s16 z;
    u16 item; // ITEM_*
    u8 quantity;
    u8 hidden; // found by searching (the Dowsing Machine finds these)
} GearMapItem;

typedef struct GearMap {
    BOOL valid; // a game is running on a map
    int mapHeaderID;
    u32 tilesVersion; // changes whenever the tiles do
    int width; // tiles
    int height;
    const u8 *tiles; // width * height, rows top to bottom (north first): GearMapTile | GEAR_MAP_OTHER_AREA
    int areaX0; // the current map's own tiles (not void) lie in [areaX0, areaX1] x [areaZ0, areaZ1]
    int areaZ0;
    int areaX1;
    int areaZ1;
    int playerX; // tiles, from the map's origin
    int playerZ;
    int playerDir; // DIR_*
    int itemCount; // items still there, visible and hidden
    GearMapItem items[GEAR_MAP_MAX_ITEMS];
    int warpCount;
    s16 warps[GEAR_MAP_MAX_WARPS][2]; // x, z
} GearMap;

// The map, refreshed (its tiles only when the area changes)
const GearMap *Gear_GetMap(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_MAP_H
