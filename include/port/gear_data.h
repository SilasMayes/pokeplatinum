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
    u8 form;
    u32 personality;
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
    GEAR_ENC_GBA,        // replaces grass slots with a GBA game in the DS's GBA slot
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
    int radarChain;      // Poke Radar chain going on (0: none)
    int count;
    GearEncounter entries[GEAR_AREA_MAX_ENTRIES]; // grouped by method, highest rate first
} GearArea;

#define GEAR_BATTLERS_MAX 4

typedef struct GearBattler {
    BOOL present;        // a Pokemon is in this position
    BOOL enemy;          // on the opposing side
    BOOL playerControlled; // the local player picks its moves
    u16 species;
    u8 form;
    u32 personality;     // to find it in the party
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
    s8 movePriority[4];
    u32 speed;           // speed as the turn order uses it (stages, items, abilities...)
    u8 movesLast;        // last among the same priority: 2 Lagging Tail or Full Incense, 1 Stall
    // Each move's damage against each battler (no critical hit); -1: not worked out
    // (status moves, random or turn-dependent power, fainted or same-side targets).
    // Per hit: moveHits is how many (0: 2 to 5).
    s16 damageMin[4][GEAR_BATTLERS_MAX];
    s16 damageMax[4][GEAR_BATTLERS_MAX];
    u8 moveHits[4];
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
    BOOL trickRoom;      // slower Pokemon move first
    GearBattler battlers[GEAR_BATTLERS_MAX]; // by battler number (even: player side)
    int enemyPartyCount;
    GearBattleMon enemyParty[GEAR_PARTY_MAX * 2];
} GearBattle;

struct BattleSystem;

// The battle code reports its battle system while it exists (port hook in battle_main.c)
void Gear_SetBattleSystem(struct BattleSystem *battleSys);
// Fills out the current battle; returns FALSE (and out->active FALSE) outside battles
BOOL Gear_GetBattle(GearBattle *out);

// The battle's menu on the DS bottom screen, which the Gear can show and use instead
enum GearBattleMenuKind {
    GEAR_MENU_NONE = 0, // nothing to choose right now
    GEAR_MENU_ACTION,   // buttons: 0 Fight, 1 Bag, 2 Pokemon, 3 Run (or Cancel)
    GEAR_MENU_FIRST,    // the first battle: 0 Fight
    GEAR_MENU_PAL_PARK, // 0 Park Ball, 1 Run
    GEAR_MENU_MOVES,    // 0 Cancel, 1-4 the moves
    GEAR_MENU_TARGETS,  // 0-3 targets (bottom left, top right, bottom right, top left), 4 Cancel
    GEAR_MENU_YES_NO,   // 0 yes, 1 no (labels below)
    GEAR_MENU_STOP,     // 0 stop watching a recorded battle
};

typedef struct GearBattleMenu {
    int kind;
    BOOL ready;          // the game is reading the menu now: buttons can be pressed
    int cursor;          // button under the game's own cursor (D-pad), -1 if hidden
    int battler;         // whose command it is (actions, moves, targets), -1 if unknown
    BOOL safari;         // actions are Ball / Bait / Mud / Run
    BOOL cancel;         // the last action button is Cancel (the second Pokemon of a double)
    char yes[40];        // GEAR_MENU_YES_NO
    char no[40];
    GearMove moves[4];   // GEAR_MENU_MOVES
    BOOL targetOk[4];    // GEAR_MENU_TARGETS: can be chosen
    int targetBattler[4]; // battler on each target button
} GearBattleMenu;

BOOL Gear_GetBattleMenu(GearBattleMenu *out);
void Gear_PressBattleButton(int button);
// The battle shows something only the DS screen can (bag, party, move to forget, the
// catching tutorial): the Gear should make room for the DS screen
BOOL Gear_BattleNeedsDsScreen(void);
void Gear_NoteBattleDsScreen(void); // battle code: such a screen is up this frame

// The overworld is running (a save is loaded and the player is in the field or in a
// sub-screen/battle started from it)
BOOL Gear_IsGameRunning(void);

// The Pokétch on the DS touch screen
enum GearPoketchState {
    GEAR_POKETCH_NO_GAME = -1,
    GEAR_POKETCH_NOT_OBTAINED = 0,
    GEAR_POKETCH_SHOWN,   // on the DS touch screen now
    GEAR_POKETCH_HIDDEN,  // another screen is up (a menu, a battle...)
};
int Gear_PoketchState(void);
int Gear_PoketchApp(void); // the app on it (POKETCH_APPID_*), -1 outside a game
const char *Gear_PoketchAppName(int app);

int Gear_GetParty(GearMon out[GEAR_PARTY_MAX]);
void Gear_GetTrainer(GearTrainer *out);
void Gear_GetArea(GearArea *out);

// Bosses: gym leaders, the Elite Four and Champion, the rival and Team Galactic, in story
// order, with their teams as a battle against them builds them (from the game's own
// trainer data, so a ROM hack's teams show as they are)
enum GearBossKind {
    GEAR_BOSS_GYM = 0,
    GEAR_BOSS_ELITE_FOUR,
    GEAR_BOSS_CHAMPION,
    GEAR_BOSS_RIVAL,
    GEAR_BOSS_GALACTIC,
};

typedef struct GearBoss {
    u16 trainerID;
    u8 kind;           // GearBossKind
    u8 stage;          // badges the player has when this battle comes (0-8)
    BOOL doubles;
    BOOL beaten;       // known to be beaten (gym leaders: their badge)
    char title[GEAR_NAME_LEN]; // "Leader", "Elite Four", "Champion", "Rival", "Commander", "Boss"
    char name[GEAR_NAME_LEN];
    char place[GEAR_NAME_LEN];
    int partyCount;
    GearMon party[GEAR_PARTY_MAX];
    u8 aceLevel;       // highest level on the team
    u16 aceSpecies;
} GearBoss;

int Gear_BossCount(void);
// NULL outside a game; the rival's team follows the starter you chose
const GearBoss *Gear_Boss(int index);
// The next gym leader (the Champion once all 8 badges are in): its index, -1 outside a game
int Gear_NextGymBoss(void);
// The level cap: the highest level on that boss's team (0 outside a game)
int Gear_LevelCap(void);

BOOL Gear_SpeciesSeen(u16 species);
BOOL Gear_SpeciesCaught(u16 species);

// Names (cached; ASCII, accents dropped)
// Game text (charcode_t) to ASCII, up to maxLen characters or the end of the string
void Gear_TextToAscii(const u16 *chars, int maxLen, char *out, int outSize);
const char *Gear_SpeciesName(u16 species);
const char *Gear_MoveName(u16 move);
const char *Gear_ItemName(u16 item);
const char *Gear_AbilityName(u8 ability);
const char *Gear_NatureName(u8 nature);
const char *Gear_TypeName(u8 type);
const char *Gear_LocationName(int mapHeaderID);
const char *Gear_LocationLabelName(int labelTextID); // by the map's label (location name) ID
int Gear_CurrentLocationLabel(void);                 // the player's, -1 outside a game

// A move's type, power, accuracy and category (pp left 0)
void Gear_MoveInfo(u16 move, GearMove *out);

// Type effectiveness in tenths (0, 5, 10, 20) of attackType against defender types
int Gear_TypeEffectiveness(u8 attackType, u8 defenderType1, u8 defenderType2);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_DATA_H
