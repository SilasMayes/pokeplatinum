#ifndef POKEPLATINUM_PORT_SOFT_RESET_H
#define POKEPLATINUM_PORT_SOFT_RESET_H

// Soft reset from the settings screen, the same as pressing L+R+Start+Select: back to
// the title screen from the last save (main.c's SoftReset, which libntr turns into a
// restart of the app into the same game, see sim_carts.h).

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The game allows it right now (not while saving or linked up)
BOOL SoftReset_Allowed(void);
// Resets at the game's next frame
void SoftReset_Request(void);
// main.c: a reset was requested (clears the request)
BOOL SoftReset_TakeRequest(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SOFT_RESET_H
