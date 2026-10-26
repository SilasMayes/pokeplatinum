#include "scrcmd_game_corner_prize.h"

#include <nitro.h>
#include <string.h>

#include "generated/items.h"

#include "field_script_context.h"
#ifdef SDK_PORT
#include "port/rom_patches.h"
#endif
#include "inlines.h"

typedef struct GameCornerPrize {
    u16 item;
    u16 price;
} GameCornerPrize;

#ifdef SDK_PORT
// Writable: ROM hacks change the prizes (port/rom_patches.h)
static GameCornerPrize sGameCornerPrizeData[] = {
    { ITEM_SILK_SCARF, 1000 },
    { ITEM_WIDE_LENS, 1000 },
    { ITEM_ZOOM_LENS, 1000 },
    { ITEM_METRONOME, 1000 },
    { ITEM_TM90, 2000 },
    { ITEM_TM58, 2000 },
    { ITEM_TM75, 4000 },
    { ITEM_TM32, 4000 },
    { ITEM_TM44, 6000 },
    { ITEM_TM89, 6000 },
    { ITEM_TM10, 6000 },
    { ITEM_TM27, 8000 },
    { ITEM_TM21, 8000 },
    { ITEM_TM35, 10000 },
    { ITEM_TM24, 10000 },
    { ITEM_TM13, 10000 },
    { ITEM_TM29, 10000 },
    { ITEM_TM74, 15000 },
    { ITEM_TM68, 20000 }
};
ROM_PATCHES_TABLE(sGameCornerPrizeData, ROM_FILE_ARM9, 0xEC018, "shops")
#endif

BOOL ScrCmd_GetGameCornerPrizeData(ScriptContext *ctx)
{
    u16 index = ScriptContext_GetVar(ctx);
    u16 *item = ScriptContext_GetVarPointer(ctx);
    u16 *price = ScriptContext_GetVarPointer(ctx);

#ifndef SDK_PORT
    static const GameCornerPrize sGameCornerPrizeData[] = {
        { ITEM_SILK_SCARF, 1000 },
        { ITEM_WIDE_LENS, 1000 },
        { ITEM_ZOOM_LENS, 1000 },
        { ITEM_METRONOME, 1000 },
        { ITEM_TM90, 2000 },
        { ITEM_TM58, 2000 },
        { ITEM_TM75, 4000 },
        { ITEM_TM32, 4000 },
        { ITEM_TM44, 6000 },
        { ITEM_TM89, 6000 },
        { ITEM_TM10, 6000 },
        { ITEM_TM27, 8000 },
        { ITEM_TM21, 8000 },
        { ITEM_TM35, 10000 },
        { ITEM_TM24, 10000 },
        { ITEM_TM13, 10000 },
        { ITEM_TM29, 10000 },
        { ITEM_TM74, 15000 },
        { ITEM_TM68, 20000 }
    };
#endif

    *item = sGameCornerPrizeData[index].item;
    *price = sGameCornerPrizeData[index].price;

    return FALSE;
}
