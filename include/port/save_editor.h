#ifndef POKEPLATINUM_PORT_SAVE_EDITOR_H
#define POKEPLATINUM_PORT_SAVE_EDITOR_H

// The save editor (Settings > Save editor): changes the running game's save data in
// memory: Pokemon in the party and the PC, the bag, the trainer, and the Mystery Gift
// events of the legendary Pokemon. The player keeps the changes by saving in the game.
//
// Everything here runs while the settings screen is open (the game waits), and only
// when SaveEd_Unavailable() returns NULL: a save is loaded and no battle or menu of the
// game is open.

#include <nitro.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_ED_PARTY       (-1) // SaveEdRef.box of a Pokemon in the party
#define SAVE_ED_BOXES       18
#define SAVE_ED_BOX_SLOTS   30
#define SAVE_ED_NAME_LEN    24
#define SAVE_ED_MAX_LEVEL   100
#define SAVE_ED_MAX_IV      31
#define SAVE_ED_MAX_EV      252
#define SAVE_ED_MAX_EVS     510
#define SAVE_ED_MAX_SPECIES 493
#define SAVE_ED_MAX_MOVE    467

// Stats in the order the editor shows them
enum SaveEdStat {
    SAVE_ED_HP,
    SAVE_ED_ATK,
    SAVE_ED_DEF,
    SAVE_ED_SPATK,
    SAVE_ED_SPDEF,
    SAVE_ED_SPEED,
    SAVE_ED_STATS
};

// NULL when the save can be edited now, else why not
const char *SaveEd_Unavailable(void);

// --- Pokemon ---------------------------------------------------------------------------

typedef struct SaveEdRef {
    int box; // SAVE_ED_PARTY, or a PC box
    int slot;
} SaveEdRef;

typedef struct SaveEdMon {
    BOOL exists;
    BOOL isEgg;
    u16 species;
    u8 form;
    char name[SAVE_ED_NAME_LEN]; // nickname, or the species
    u8 level;
    u8 nature;
    u8 gender;        // 0 male, 1 female, 2 none
    BOOL genderFixed; // the species is always the same gender (or none)
    BOOL shiny;
    u8 ability;
    u8 abilities[2]; // the species' (abilities[1] 0: it has one)
    u16 item;
    u8 friendship;
    u8 ball;
    u8 ivs[SAVE_ED_STATS];
    u8 evs[SAVE_ED_STATS];
    u16 stats[SAVE_ED_STATS];
    u16 moves[4];
    u8 pp[4];
    u8 maxPp[4];
    u16 hp;
    u16 maxHp;
    u32 status;
    u8 type1;
    u8 type2;
} SaveEdMon;

typedef struct SaveEdSlot {
    u16 species; // 0: empty
    u8 level;
    BOOL isEgg;
    BOOL shiny;
} SaveEdSlot;

int SaveEd_PartyCount(void);
int SaveEd_BoxCount(int box);
void SaveEd_BoxName(int box, char *out, int size);
BOOL SaveEd_GetBoxSlot(int box, int slot, SaveEdSlot *out); // quicker than SaveEd_GetMon
BOOL SaveEd_GetMon(SaveEdRef ref, SaveEdMon *out);

// Each returns TRUE when the Pokemon changed
BOOL SaveEd_SetLevel(SaveEdRef ref, int level);
// A new personality value with this nature, gender (-1: any), shininess and ability
// slot (-1: any). Keeps everything else.
BOOL SaveEd_SetPersonality(SaveEdRef ref, int nature, int gender, BOOL shiny, int abilitySlot);
BOOL SaveEd_SetIV(SaveEdRef ref, enum SaveEdStat stat, int value);
BOOL SaveEd_SetEV(SaveEdRef ref, enum SaveEdStat stat, int value); // the total stays at 510 or less
BOOL SaveEd_SetMove(SaveEdRef ref, int slot, u16 move);            // 0 removes it (not the last one)
BOOL SaveEd_SetItem(SaveEdRef ref, u16 item);
BOOL SaveEd_SetFriendship(SaveEdRef ref, int value);
BOOL SaveEd_Heal(SaveEdRef ref); // HP, status and PP

// Moves the species learns by level and by TM/HM, sorted by name; returns the count
int SaveEd_LearnableMoves(u16 species, int form, u16 *out, int max);
int SaveEd_MoveType(u16 move);
int SaveEd_MovePower(u16 move);
int SaveEd_MovePP(u16 move);

// The stat a nature raises and lowers (enum SaveEdStat; the same one: neutral)
void SaveEd_NatureStats(int nature, int *raised, int *lowered);

typedef struct SaveEdNewMon {
    u16 species;
    int level;
    int nature; // -1: random
    BOOL shiny;
    BOOL maxIVs; // else random
    u16 item;
} SaveEdNewMon;

// Gives a Pokemon caught here in a Poke Ball with the player as its trainer: to the party
// (when it has room and toParty is set) or the PC. Returns 0 when there's no room, 1 for
// the party, 2 for the PC (where: *box, *slot).
int SaveEd_GiveMon(const SaveEdNewMon *newMon, BOOL toParty, int *box, int *slot);

// --- Bag -------------------------------------------------------------------------------

#define SAVE_ED_POCKETS 8 // POCKET_* order (constants/items.h)

typedef struct SaveEdBagItem {
    u16 item;
    u16 quantity;
} SaveEdBagItem;

const char *SaveEd_PocketName(int pocket);
// The items in the bag's pocket, in the bag's order
int SaveEd_BagPocket(int pocket, SaveEdBagItem *out, int max);
// Every item that goes in the pocket, sorted by name
int SaveEd_PocketItems(int pocket, u16 *out, int max);
int SaveEd_ItemPocket(u16 item);
int SaveEd_ItemMaxQuantity(u16 item);
int SaveEd_ItemQuantity(u16 item);
BOOL SaveEd_SetItemQuantity(u16 item, int quantity); // 0 removes it; FALSE: no room

// --- Trainer ---------------------------------------------------------------------------

#define SAVE_ED_MAX_MONEY 999999
#define SAVE_ED_MAX_BP    9999
#define SAVE_ED_MAX_HOURS 999

typedef struct SaveEdTrainer {
    char name[SAVE_ED_NAME_LEN];
    u16 id;
    u32 money;
    int battlePoints;
    u8 badges; // bit per badge
    u16 hours;
    u8 minutes;
    BOOL nationalDex;
    BOOL gameCompleted;
} SaveEdTrainer;

const char *SaveEd_BadgeName(int badge);
void SaveEd_GetTrainer(SaveEdTrainer *out);
void SaveEd_SetMoney(u32 money);
void SaveEd_SetBattlePoints(int points);
void SaveEd_SetBadge(int badge, BOOL has);
void SaveEd_SetPlayTime(int hours, int minutes);
void SaveEd_GiveNationalDex(void);

// --- Mystery Gift events ---------------------------------------------------------------

enum SaveEdEvent {
    SAVE_ED_EVENT_DARKRAI,
    SAVE_ED_EVENT_SHAYMIN,
    SAVE_ED_EVENT_ARCEUS,
    SAVE_ED_EVENT_ROTOM,
    SAVE_ED_EVENTS
};

typedef struct SaveEdEventState {
    BOOL started;  // the event is on (the Mystery Gift was received)
    BOOL hasItem;  // its key item is in the bag
    BOOL caught;   // the Pokemon was caught (Rotom: its room's visit is done)
    BOOL needsPostgame;
    BOOL gameCompleted;
    BOOL nationalDex;
    BOOL storyStep; // Darkrai: the sailor's son was woken (Lunar Wing); Rotom: Team Galactic left the Eterna building
} SaveEdEventState;

void SaveEd_GetEvent(enum SaveEdEvent event, SaveEdEventState *out);
// Does what receiving the event's Mystery Gift does: gives the key item and turns the event on
BOOL SaveEd_StartEvent(enum SaveEdEvent event);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SAVE_EDITOR_H
