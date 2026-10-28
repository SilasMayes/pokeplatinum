#ifndef POKEPLATINUM_PORT_CHEATS_H
#define POKEPLATINUM_PORT_CHEATS_H

// Cheats (Settings > Cheats), all off by default. Walking through walls, no wild
// encounters and running from trainer battles are checked where they apply (player_move.c,
// wild_encounters.c, battle_controller_player.c); the ones below have their hooks here.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shiny odds: wild Pokemon and hatched eggs are shiny 1 in this many. The game's own odds
// (1 in 8192 in vanilla, better in some ROM hacks) when the cheat is off or asks for worse.
u32 Cheats_ShinyOdds(void);
BOOL Cheats_ShinyOddsBoosted(void);

// EXP a Pokemon gains from a battle, scaled by the EXP setting
u32 Cheats_ScaleExp(u32 exp);

BOOL Cheats_AlwaysCatch(void);  // every Poke Ball catches
BOOL Cheats_InfinitePP(void);   // the player's moves don't use PP
BOOL Cheats_AlwaysEscape(void); // running from wild Pokemon always works, even when trapped
BOOL Cheats_FastEggs(void);     // eggs hatch after a few steps
BOOL Cheats_AnyTM(void);        // any Pokemon can learn any TM or HM

// Game hooks: a battle from the field is starting (src/encounter.c), an egg hatched
// (src/egg_hatch.c). They make wild Pokemon and hatchlings shiny at the boosted odds.
struct Party;
struct Pokemon;
void Cheats_OnBattleStart(u32 battleType, struct Party *wildParty);
void Cheats_OnEggHatched(struct Pokemon *mon);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_CHEATS_H
