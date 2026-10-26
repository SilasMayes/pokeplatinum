#ifndef POKEPLATINUM_PORT_GEAR_DATA_H
#define POKEPLATINUM_PORT_GEAR_DATA_H

// Game data for the Gear companion (src/port/gear_ui.cpp), copied out of the save and
// the field system into plain structs with ASCII text. Called from the companion UI,
// i.e. on the game thread between two frames.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEAR_NAME_LEN 24
#define GEAR_PARTY_MAX 6
#define GEAR_AREA_MAX_ENTRIES 64

typedef struct GearMove {
    u16 id;
    u8 type;
    u8 pp;
    u8 maxPp;
    u8 power;
    u8 accuracy;
    u8 category; // 0 physical, 1 special, 2 status
} GearMove;

typedef struct GearMon {
    BOOL isEgg;
    BOOL shiny;
    u16 species;
    char nickname[GEAR_NAME_LEN];
    u8 level;
    u8 gender; // 0 male, 1 female, 2 none
    u16 hp;
    u16 maxHp;
    u32 status; // MON_CONDITION_* bits
    u8 type1;
    u8 type2;
    u16 heldItem;
    u8 ability;
    u8 nature;
    u32 exp;
    u32 expLevelStart; // exp at the start of the current level
    u32 expNextLevel;  // exp needed for the next level (== exp at level 100)
    u16 stats[6];      // HP, Atk, Def, SpA, SpD, Spe
    u8 ivs[6];
    u8 evs[6];
    u8 friendship;
    GearMove moves[4];
} GearMon;

typedef struct GearTrainer {
    char name[GEAR_NAME_LEN];
    u8 gender;
    u32 id;
    u32 money;
    u8 badgeMask; // bit n = badge n
    int badgeCount;
    int hours;
    int minutes;
    BOOL pokedexObtained;
    BOOL nationalDex;
    int seenLocal;
    int caughtLocal;
    int seenNational;
    int caughtNational;
} GearTrainer;

enum GearEncounterMethod {
    GEAR_ENC_GRASS = 0,  // walking in grass/caves, for the current time of day
    GEAR_ENC_SURF,
    GEAR_ENC_OLD_ROD,
    GEAR_ENC_GOOD_ROD,
    GEAR_ENC_SUPER_ROD,
    GEAR_ENC_RADAR,      // Poke Radar (replaces some grass slots while it is in use)
    GEAR_ENC_SWARM,      // replaces grass slots while a swarm is reported here
    GEAR_ENC_METHOD_COUNT,
};

typedef struct GearEncounter {
    u8 method;
    u16 species;
    u8 minLevel;
    u8 maxLevel;
    u8 rate; // percent within the method (0 when it varies, e.g. radar/swarm)
} GearEncounter;

typedef struct GearArea {
    BOOL valid;          // a game is running on a map
    BOOL hasEncounters;
    int mapHeaderID;
    char name[GEAR_NAME_LEN];
    int timeOfDay;       // TIMEOFDAY_*
    int count;
    GearEncounter entries[GEAR_AREA_MAX_ENTRIES]; // grouped by method, highest rate first
} GearArea;

#define GEAR_BATTLERS_MAX 4

typedef struct GearBattler {
    BOOL present;        // a Pokemon is in this position
    BOOL enemy;          // on the opposing side
    BOOL playerControlled; // the local player picks its moves
    u16 species;
    char nickname[GEAR_NAME_LEN];
    u8 level;
    u8 gender;
    BOOL shiny;
    u16 hp;
    u16 maxHp;
    u32 status;          // MON_CONDITION_* bits
    BOOL confused;
    u8 type1;
    u8 type2;
    u8 ability;
    u16 heldItem;
    s8 statStages[8];    // BATTLE_STAT_*, -6..+6 (index 0, HP, is unused)
    GearMove moves[4];
} GearBattler;

typedef struct GearBattleMon {
    u16 species;
    u8 level;
    BOOL fainted;
    BOOL isEgg;
} GearBattleMon;

typedef struct GearBattle {
    BOOL active;
    BOOL trainer;        // a trainer battle (as opposed to a wild Pokemon)
    BOOL doubles;
    int choosing;        // battler whose command the player is picking, -1 if none
    GearBattler battlers[GEAR_BATTLERS_MAX]; // by battler number (even: player side)
    int enemyPartyCount;
    GearBattleMon enemyParty[GEAR_PARTY_MAX * 2];
} GearBattle;

struct BattleSystem;

// The battle code reports its battle system while it exists (port hook in battle_main.c)
void Gear_SetBattleSystem(struct BattleSystem *battleSys);
// Fills out the current battle; returns FALSE (and out->active FALSE) outside battles
BOOL Gear_GetBattle(GearBattle *out);

// The overworld is running (a save is loaded and the player is in the field or in a
// sub-screen/battle started from it)
BOOL Gear_IsGameRunning(void);

int Gear_GetParty(GearMon out[GEAR_PARTY_MAX]);
void Gear_GetTrainer(GearTrainer *out);
void Gear_GetArea(GearArea *out);

BOOL Gear_SpeciesSeen(u16 species);
BOOL Gear_SpeciesCaught(u16 species);

// Names (cached; ASCII, accents dropped)
const char *Gear_SpeciesName(u16 species);
const char *Gear_MoveName(u16 move);
const char *Gear_ItemName(u16 item);
const char *Gear_AbilityName(u8 ability);
const char *Gear_NatureName(u8 nature);
const char *Gear_TypeName(u8 type);
const char *Gear_LocationName(int mapHeaderID);

// Type effectiveness in tenths (0, 5, 10, 20) of attackType against defender types
int Gear_TypeEffectiveness(u8 attackType, u8 defenderType1, u8 defenderType2);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_DATA_H
