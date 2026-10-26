#ifndef POKEPLATINUM_PORT_SAVE_SLOTS_H
#define POKEPLATINUM_PORT_SAVE_SLOTS_H

// Save slots: several independent save files, each a raw DS save image (the same
// format as an emulator .sav). Slot 1 is the original save.bin, so existing saves keep
// working. The slot in use is remembered in sim_config_prj.ini.
//
// Slots are switched from the main menu (L/R, or the Saves window in the settings
// GUI), where nothing else holds on to the loaded save yet.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_SLOT_COUNT 4
#define SAVE_SLOT_IMAGE_SIZE 0x80000 // 4 Mbit flash

struct SaveData;

typedef struct SaveSlotInfo {
    BOOL hasData;      // the file holds a save (not blank)
    BOOL hasSummary;   // the fields below are known
    char name[16];
    int hours;
    int minutes;
    int badges;
    BOOL pokedexObtained;
    int seen;
    char savedAt[32];  // local time of the last save, "YYYY-MM-DD HH:MM"
} SaveSlotInfo;

// Points the save file at the configured slot. Call before SaveData_Init().
void SaveSlots_Init(void);

// 0-based slot currently loaded
int SaveSlots_Current(void);

void SaveSlots_FilePath(int slot, char *out, int outSize);
void SaveSlots_GetInfo(int slot, SaveSlotInfo *out);
BOOL SaveSlots_HasData(int slot);
BOOL SaveSlots_AnyOtherSlotHasData(void);

// Switches to another slot and reloads the save data from it. Only safe while nothing
// is using the loaded save (title screen / main menu).
void SaveSlots_Select(struct SaveData *saveData, int slot);

// Called after every successful save (manual or automatic)
void SaveSlots_OnSaved(struct SaveData *saveData);

// Called once the save of the current slot has been loaded
void SaveSlots_OnLoaded(struct SaveData *saveData);

// Main menu integration: the GUI may only switch slots while the main menu is up.
void SaveSlots_SetMainMenuActive(BOOL active);
BOOL SaveSlots_IsMainMenuActive(void);
void SaveSlots_RequestSwitch(int slot);
int SaveSlots_TakeRequestedSwitch(void); // -1 when nothing is pending

// Copy a .sav/.dsv file into a slot (the old file is kept as .bak), or a slot out to a
// .sav file. err receives a short reason on failure.
BOOL SaveSlots_Import(int slot, const char *sourcePath, char *err, int errSize);
BOOL SaveSlots_Export(int slot, const char *destPath, char *err, int errSize);

// TRUE while the slot's save is loaded and in use by a running game (it can't be
// replaced by an import until the player is back at the main menu)
BOOL SaveSlots_IsInUse(int slot);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SAVE_SLOTS_H
