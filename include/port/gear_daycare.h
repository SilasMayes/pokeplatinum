#ifndef POKEPLATINUM_PORT_GEAR_DAYCARE_H
#define POKEPLATINUM_PORT_GEAR_DAYCARE_H

// The Gear's breeding helper: what the Day Care's two Pokemon would give, worked out the
// way the game's own Day Care code does it (overlay005/daycare.c), and the eggs in the
// party with the steps they still need.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEAR_DAYCARE_MAX_MOVES 12

typedef struct GearDaycareMon {
    u16 species;
    u8 form;
    u8 level;
    u8 gender; // GENDER_*
    u16 item;
    u8 ivs[6];   // HP, Atk, Def, Spe, SpA, SpD (the game's stat order)
    u8 nature;
    u16 moves[4];
    u32 personality;
    BOOL shiny;
    u32 steps;   // walked with the Day Care
    u8 levelsGained;
} GearDaycareMon;

typedef struct GearPartyEgg {
    u16 species;
    int steps; // about how many more to hatch
} GearPartyEgg;

typedef struct GearDaycare {
    int count; // Pokemon in the Day Care (0-2)
    GearDaycareMon mons[2];
    int compatibility;     // percent chance per check (every 256 steps): 0, 20, 50 or 70
    int stepsToNextCheck;
    BOOL eggWaiting;       // the Day Care Man has an egg
    u16 eggSpecies;        // what an egg would be (SPECIES_NONE when they can't breed)
    int motherSlot;        // the parent the egg's species comes from
    int everstoneSlot;     // -1, else the parent whose nature the egg gets half the time
    BOOL differentLanguages; // Masuda method: shiny eggs more likely
    int inheritedCount;    // moves the egg would know from its parents
    u16 inheritedMoves[GEAR_DAYCARE_MAX_MOVES];
    u8 inheritedHow[GEAR_DAYCARE_MAX_MOVES]; // 0 egg move, 1 TM, 2 both parents' level-up move, 3 Volt Tackle
    int partyEggCount;
    GearPartyEgg partyEggs[6];
    int eggCycleSteps;     // steps in one egg cycle now (255, 230 on some dates)
    BOOL fasterHatching;   // Flame Body / Magma Armor in the party
} GearDaycare;

BOOL Gear_GetDaycare(GearDaycare *out);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_DAYCARE_H
