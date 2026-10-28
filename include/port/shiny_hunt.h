#ifndef POKEPLATINUM_PORT_SHINY_HUNT_H
#define POKEPLATINUM_PORT_SHINY_HUNT_H

// Shiny-hunt counters (shown by the Gear): wild and one-off Pokemon met, eggs hatched and
// soft resets since the last shiny, per species too, and a log of the shinies found.
// They are kept beside the save slot's file (<save file>.hunt) and not in the save itself,
// so soft resetting and reloading don't take them back.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHINY_HUNT_LOG_MAX 8

enum ShinyHuntMethod {
    SHINY_HUNT_MET = 0, // in the wild, or a one-off Pokemon met in a battle
    SHINY_HUNT_EGG,
};

typedef struct ShinyHuntFind {
    u16 species;
    u8 method;      // ShinyHuntMethod
    u32 count;      // encounters (or eggs) it took
    u32 resets;     // soft resets it took (for Pokemon met)
    char date[20];  // "YYYY-MM-DD HH:MM"
} ShinyHuntFind;

typedef struct ShinyHuntStats {
    // Since the last shiny
    u32 encounters; // Pokemon met in non-trainer battles
    u32 eggs;
    u32 resets;
    // Ever
    u32 totalEncounters;
    u32 totalEggs;
    u32 totalResets;
    u32 shinies;
    int findCount;
    ShinyHuntFind finds[SHINY_HUNT_LOG_MAX]; // newest first
} ShinyHuntStats;

// The current save slot's counters
const ShinyHuntStats *ShinyHunt_Get(void);
// Pokemon of a species met since the last shiny one of that species
u32 ShinyHunt_SpeciesCount(u16 species);
// Chance of at least one shiny in that many tries at the current odds (Cheats_ShinyOdds)
double ShinyHunt_Chance(u32 tries);

// Starts the counts over (the log and the totals stay)
void ShinyHunt_ResetCounters(void);

// Game hooks
struct Party;
struct Pokemon;
void ShinyHunt_OnBattleStart(u32 battleType, struct Party *wildParty);
void ShinyHunt_OnEggHatched(struct Pokemon *mon);
void ShinyHunt_OnSoftReset(void);

// Its file for the slot was replaced (save sync): read again when next needed
void ShinyHunt_ForgetLoaded(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SHINY_HUNT_H
