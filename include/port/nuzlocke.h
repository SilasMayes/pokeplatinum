#ifndef POKEPLATINUM_PORT_NUZLOCKE_H
#define POKEPLATINUM_PORT_NUZLOCKE_H

// Nuzlocke helper (Settings > Gameplay > Nuzlocke, off by default): keeps the run's books.
// Each area's first wild encounter is the one that counts (with the dupes clause, species
// already caught don't; with the shiny clause, shinies never do and can always be
// caught), whether it was caught or missed, and which Pokemon fainted (died), optionally
// sending them to the PC after the battle. The Gear shows it all; nothing is stopped by
// force. Kept beside the save slot (save_slotN.nuzlocke), like the shiny-hunt counters.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUZLOCKE_MAX_AREAS 160
#define NUZLOCKE_MAX_DEATHS 64

enum NuzlockeResult {
    NUZLOCKE_IN_BATTLE = 0, // the first encounter is going on
    NUZLOCKE_CAUGHT,
    NUZLOCKE_MISSED,        // fainted, fled or the player ran
};

typedef struct NuzlockeArea {
    u16 location; // map label (location name) ID
    u16 species;
    u8 result;    // NuzlockeResult
} NuzlockeArea;

typedef struct NuzlockeDeath {
    u32 personality;
    u16 species;
    u8 level;
    u16 location;
    char name[16];
} NuzlockeDeath;

typedef struct NuzlockeRun {
    int areaCount;
    NuzlockeArea areas[NUZLOCKE_MAX_AREAS];
    int deathCount;
    NuzlockeDeath deaths[NUZLOCKE_MAX_DEATHS];
} NuzlockeRun;

// What the wild battle going on is to the run
enum NuzlockeEncounter {
    NUZLOCKE_ENC_NONE = 0,  // the helper is off, or no wild battle
    NUZLOCKE_ENC_FIRST,     // this area's first encounter: catch it!
    NUZLOCKE_ENC_USED,      // the area's encounter was already used
    NUZLOCKE_ENC_DUPE,      // already caught that species: doesn't count (dupes clause)
    NUZLOCKE_ENC_SHINY,     // shiny: doesn't count, can be caught (shiny clause)
};

BOOL Nuzlocke_Enabled(void);
const NuzlockeRun *Nuzlocke_Get(void);
const NuzlockeArea *Nuzlocke_Area(u16 location); // NULL: still open
int Nuzlocke_CurrentEncounter(void);              // NuzlockeEncounter
BOOL Nuzlocke_IsDead(u32 personality);
void Nuzlocke_Reset(void);                        // starts a new run's books

// Game hooks (src/encounter.c)
struct Party;
void Nuzlocke_OnBattleStart(u32 battleType, struct Party *wildParty);
// After the battle, before its party is copied back to the save: records deaths, and with
// "send fainted Pokemon to the PC" takes them out of playerParty into the PC
void Nuzlocke_OnBattleEnd(u32 battleType, int resultMask, struct Party *playerParty, struct Party *wildParty);

// Its file for the slot was replaced (save sync): read again when next needed
void Nuzlocke_ForgetLoaded(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_NUZLOCKE_H
