#ifndef POKEPLATINUM_PORT_HM_FREE_H
#define POKEPLATINUM_PORT_HM_FREE_H

// Field moves without HMs (Settings > Gameplay): with the HM in the bag (and the badge the
// game asks for, as always), Cut, Fly, Surf, Strength, Defog, Rock Smash, Waterfall and
// Rock Climb work even when no Pokemon in the party knows the move. A Pokemon that could
// learn it does it (or the first one that isn't an Egg if none could), so the cut-in and
// the messages are the usual ones. Owning the HM keeps the story's order: the game checks
// for the move where it hands out the HM.

#include <nitro/types.h>

#include "party.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL HmFree_Enabled(void);

// An HM field move nobody in the party knows can be used anyway: the setting is on, the
// HM is in the bag and there's a Pokemon (not an Egg) to do it
BOOL HmFree_CanUse(Party *party, u16 move);
// The party slot that does it: -1 when it can't be used
int HmFree_SubstituteSlot(Party *party, u16 move);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_HM_FREE_H
