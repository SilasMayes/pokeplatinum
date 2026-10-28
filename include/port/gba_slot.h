#ifndef POKEPLATINUM_PORT_GBA_SLOT_H
#define POKEPLATINUM_PORT_GBA_SLOT_H

// The DS's GBA slot on the port: a Ruby, Sapphire, Emerald, FireRed or LeafGreen game "in"
// it (libntr's simulator/sim_gba_slot.h), with a save from a GBA emulator. As on a DS Lite,
// that gives the dual-slot wild encounters (with the National Pokedex) and the main menu's
// Migrate from... option to send Pokemon to Pal Park.
//
// The save is the player's copy in the app's folder (SIM_SettingsPath), so the file they
// picked is never changed; migrating takes Pokemon out of the copy, as it would out of the
// cartridge, and GBASlot_SavePath() is there to export it.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GBASlotGame {
    GBA_SLOT_NONE = 0,
    GBA_SLOT_RUBY,
    GBA_SLOT_SAPPHIRE,
    GBA_SLOT_EMERALD,
    GBA_SLOT_FIRERED,
    GBA_SLOT_LEAFGREEN,
    GBA_SLOT_GAME_COUNT,
};

typedef struct GBASlotSummary {
    BOOL valid;       // a save the game can read
    int family;       // a GBA_SLOT_* game of its pair: Ruby (RS), FireRed (FRLG) or Emerald
    char name[24];    // the player's name (UTF-8)
    u8 gender;        // 0 boy, 1 girl
    u16 trainerID;
    u16 hours;
    u8 minutes;
    u8 partyCount;
    u16 boxedCount;   // Pokemon in the PC boxes
} GBASlotSummary;

void GBASlot_Init(void); // at start-up: the game from the settings
int GBASlot_Game(void);
const char *GBASlot_GameName(int game);
// Changes the game in the slot (the save stays); the encounters follow straight away
void GBASlot_SetGame(int game);

BOOL GBASlot_HasSave(void);
const char *GBASlot_SavePath(void);
// Copies a .sav in (replacing the one there). When it's from another pair of games than the
// one in the slot, the slot changes to its pair; message says so. FALSE: err says why.
BOOL GBASlot_ImportSave(const char *path, char *message, int messageSize);
void GBASlot_RemoveSave(void);
void GBASlot_GetSummary(GBASlotSummary *out);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GBA_SLOT_H
