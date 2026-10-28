#ifndef POKEPLATINUM_PORT_GEAR_SPRITES_H
#define POKEPLATINUM_PORT_GEAR_SPRITES_H

// Pokemon pictures for the Gear, decoded from the game's own graphics (the ROM's files, so
// ROM hacks' sprites show too) into RGBA8 pixels (byte order R, G, B, A; transparent
// where the DS draws nothing). Read with plain file access, outside the game's heaps.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Party menu icon: two 32x32 frames, one above the other (32x64)
#define GEAR_ICON_WIDTH 32
#define GEAR_ICON_HEIGHT 64
BOOL GearSprites_Icon(u16 species, u8 form, BOOL isEgg, u32 *rgba);

// Battle front sprite, its first frame (80x80); shiny colors, Spinda's spots
#define GEAR_FRONT_SIZE 80
BOOL GearSprites_Front(u16 species, u8 gender, u8 form, BOOL shiny, BOOL isEgg, u32 personality, u32 *rgba);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_SPRITES_H
