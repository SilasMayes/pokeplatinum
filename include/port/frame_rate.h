#ifndef POKEPLATINUM_PORT_FRAME_RATE_H
#define POKEPLATINUM_PORT_FRAME_RATE_H

// 60 FPS. The DS game runs its logic at 30 Hz: the main loop waits for two vblanks per
// iteration. With 60Fps on, an iteration waits for one, so the logic runs at 60 Hz
// ("double logic"), and the speed fixes (60FpsSpeedFix) halve the per-frame steps of what
// moves on the overworld (walking, biking, turning, ledge jumps, sprite animations,
// animated map textures and props) so they keep their real speed.
//
// By default the logic only runs at 60 Hz while the player is free on the overworld,
// which is where it pays off (smooth walking and scrolling). Battles, menus, events,
// warps and every other screen keep the original 30 Hz timing, so they run at their
// normal speed. 60FpsEverywhere runs all of the game at 60 Hz instead, which makes
// everything off the overworld (battles, text, menus) twice as fast.
//
// Music keeps its tempo either way: libntr's sequencer runs from the audio callback.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decides this main loop iteration's rate; called once per iteration before the logic
void FrameRate_Update(void);

// This iteration runs the game logic at 60 Hz
BOOL FrameRate_Is60Hz(void);

// A 30 Hz iteration is about to wait for its first vblank. The frame shown then is
// replaced by the one at the second before it can be seen, so it isn't drawn (half the
// drawing in battles and menus), unless DrawEveryFrame is on.
void FrameRate_BeforeFirstVBlank(void);

// This iteration runs at 60 Hz and per-frame steps should be halved
BOOL FrameRate_SpeedFixActive(void);

// For things that move a set step per frame and are simplest kept at 30 Hz (weather
// particles, field effects, NPC idle timers): TRUE on every other 60 Hz iteration, when
// they should skip their update. Always FALSE without the speed fix.
BOOL FrameRate_SkipHalfRateTick(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_FRAME_RATE_H
