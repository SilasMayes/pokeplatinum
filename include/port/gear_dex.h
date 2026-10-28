#ifndef POKEPLATINUM_PORT_GEAR_DEX_H
#define POKEPLATINUM_PORT_GEAR_DEX_H

// Species lookup for the Gear: everything about a species, read from the ROM's data (its
// personal data, learnsets, evolutions, TM table and wild encounter tables), so a ROM hack
// such as Renegade Platinum shows its own values. Where to find it covers the walking,
// water, rod, Poke Radar, swarm and GBA-slot encounter tables of every map.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEAR_DEX_MAX_EVOS 7
#define GEAR_DEX_MAX_LEVEL_MOVES 48
#define GEAR_DEX_MAX_PLACES 96

enum GearDexTime {
    GEAR_TIME_MORNING = 0,
    GEAR_TIME_DAY,   // day and evening
    GEAR_TIME_NIGHT,
    GEAR_TIME_COUNT,
};

typedef struct GearDexEvolution {
    u16 method; // EVO_*
    u16 param;
    u16 species; // into (or, for a pre-evolution, from)
} GearDexEvolution;

typedef struct GearDexPlace {
    u16 label;     // map label (location name) ID
    u8 method;     // GearEncounterMethod
    u8 gbaGames;   // GEAR_ENC_GBA: bit 0 Ruby, 1 Sapphire, 2 Emerald, 3 FireRed, 4 LeafGreen
    u8 minLevel;
    u8 maxLevel;
    u8 rates[GEAR_TIME_COUNT]; // percent within the method at each time of day (0: not then, or it varies)
} GearDexPlace;

typedef struct GearSpecies {
    u16 species;
    u16 sinnohNumber; // 0: not in the Sinnoh Pokedex
    u8 types[2];
    u8 abilities[2];
    u8 baseStats[6]; // HP, Atk, Def, SpA, SpD, Spe
    u8 evYield[6];
    u8 catchRate;
    u8 genderRatio;  // 0 always male, 254 always female, 255 genderless
    u8 eggGroups[2];
    u8 hatchCycles;
    u8 expRate;
    u8 baseFriendship;
    u16 heldItems[2]; // in the wild: 50% and 5%
    int evoCount;
    GearDexEvolution evos[GEAR_DEX_MAX_EVOS];
    int preEvoCount;
    GearDexEvolution preEvos[4];
    int levelMoveCount;
    struct {
        u16 move;
        u8 level;
    } levelMoves[GEAR_DEX_MAX_LEVEL_MOVES];
    int tmCount;
    u8 tms[100]; // 0-91 TM01-TM92, 92-99 HM01-HM08
    int eggMoveCount;
    u16 eggMoves[16];
    int tutorMoveCount;
    u16 tutorMoves[40];
    int placeCount;
    GearDexPlace places[GEAR_DEX_MAX_PLACES];
} GearSpecies;

BOOL Gear_GetSpecies(u16 species, GearSpecies *out);

// "Level 16", "Use a Fire Stone", "Trade holding a Metal Coat"...
void Gear_EvolutionText(const GearDexEvolution *evo, char *out, int size);
const char *Gear_EggGroupName(u8 group);
const char *Gear_ExpRateName(u8 rate);
u16 Gear_TMMove(int tm); // 0-99, as GearSpecies.tms
// A species' types (cached)
void Gear_SpeciesTypes(u16 species, u8 *type1, u8 *type2);
// Species by name, A to Z (the ROM's names)
int Gear_SpeciesByName(u16 *out, int max);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_DEX_H
