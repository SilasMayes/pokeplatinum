#ifndef POKEPLATINUM_PORT_SPEED_UP_H
#define POKEPLATINUM_PORT_SPEED_UP_H

// What Renegade Platinum's SpeedUp patch does, for any game (Settings > Gameplay):
// - instant text: a message appears at once, up to where it waits for a button
// - instant bars: battle HP and EXP bars jump to their new value

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL SpeedUp_InstantText(void);
BOOL SpeedUp_InstantBars(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SPEED_UP_H
