#ifndef POKEPLATINUM_PORT_GEAR_BERRIES_H
#define POKEPLATINUM_PORT_GEAR_BERRIES_H

// The Gear's berry timers: every berry patch with something growing, where it is, how far
// along it is and when it's next ready, from the save's patches and the berries' growth
// data (as berry_patches.c grows them). The times are the game clock's minutes, which run
// with the device's (the game's clock offset moves both alike).

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GearBerryPatch {
    int patchID;
    u16 mapHeaderID;   // 0 when unknown
    u16 berryItem;
    int stage;         // BERRY_GROWTH_STAGE_*
    int minutesToNextStage;
    int minutesToRipe; // 0 when ripe
    int yield;         // berries now (ripe), else the estimate
    int moisture;      // 0-100
    int mulch;         // MULCH_TYPE_*
    BOOL growing;      // planted by the player: it grows with time. Else a tree the game
                       // starts with ripe berries, which keep until picked.
} GearBerryPatch;

// The player's patches first, the soonest ready first, then the ripe trees the game starts
// with; returns how many
int Gear_GetBerryPatches(GearBerryPatch *out, int max);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_BERRIES_H
