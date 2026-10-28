#ifndef POKEPLATINUM_PORT_SAVE_SYNC_H
#define POKEPLATINUM_PORT_SAVE_SYNC_H

// Save sync: the save slots kept in step between devices (the Thor and a computer, say)
// through a folder that something else copies between them: Syncthing, a network share, a
// homelab's NAS, a cloud drive's synced folder.
//
// The folder holds a folder per game (its name, e.g. "Renegade Platinum"), and in it each
// slot's save as slotN.sav (a 512 KB .sav emulators load as it is) with slotN.txt: who saved
// it, when, a summary and the save's hash. The Nuzlocke and shiny hunt records go along.
//
// Each device remembers the hash it last synced for each slot (save_slotN.sync), so it can
// tell which side changed:
//   - saved here since: copied to the folder, right after the save
//   - saved on the other device since: copied in, before the save is loaded at startup or
//     on the main menu (which then reloads it); during a game it waits for the main menu
//   - saved on both: a conflict, which the player settles in Settings > Saves (which one
//     to keep). Nothing is overwritten until then.
// Every replaced save is kept as .bak next to it.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SAVE_SYNC_OFF = 0,
    SAVE_SYNC_IN_STEP,      // the same on both
    SAVE_SYNC_NOTHING,      // neither has a save in this slot
    SAVE_SYNC_SENDING,      // saved here since: being copied to the folder
    SAVE_SYNC_NEWER_THERE,  // saved on the other device since: waits for the main menu
    SAVE_SYNC_ARRIVING,     // the other device's save is still being copied into the folder
    SAVE_SYNC_CONFLICT,     // saved on both since they were last in step
    SAVE_SYNC_NO_ACCESS,    // the folder can't be used (Android: no "All files access")
    SAVE_SYNC_ERROR,        // copying failed
};

typedef struct SaveSyncSlot {
    int state;              // SAVE_SYNC_*
    BOOL thereHasSave;      // the folder has a save for the slot
    char thereDevice[64];   // which device saved it
    char thereSavedAt[32];  // "YYYY-MM-DD HH:MM", that device's time
    char thereName[16];
    int thereHours;
    int thereMinutes;
    int thereBadges;
} SaveSyncSlot;

// Reads the settings. Call once, early.
void SaveSync_Init(void);

BOOL SaveSync_Enabled(void);
void SaveSync_SetEnabled(BOOL enabled);
const char *SaveSync_Folder(void); // "" when none is chosen
void SaveSync_SetFolder(const char *path);
// The game's folder in it
void SaveSync_GameFolder(char *out, int outSize);

// After the save is loaded at startup (SaveData_Init): brings in the slots saved on the
// other device. TRUE when the slot in use was replaced, which must then be loaded again.
BOOL SaveSync_AtStartup(void);

// After every save
void SaveSync_OnSaved(int slot);

// As the main menu (re)loads a slot, with nothing holding on to its save: brings in the
// other device's newer save the main menu was asked to reload it for
void SaveSync_BeforeLoad(int slot);

// Every frame: looks at the folder every few seconds
void SaveSync_Update(void);

// Look now (after the settings change, say)
void SaveSync_CheckNow(void);

void SaveSync_GetSlot(int slot, SaveSyncSlot *out);

// Settles a conflict (or takes the other device's newer save now): keepThis sends this
// device's save to the folder; otherwise the folder's is brought in, which only works
// while the slot's game isn't running. err receives a short reason on failure.
BOOL SaveSync_Resolve(int slot, BOOL keepThis, char *err, int errSize);

// The last thing it did ("Sent slot 1", "Brought in slot 2 from AYN Thor"), and when
// (SDL ticks); "" if nothing yet
const char *SaveSync_LastEvent(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SAVE_SYNC_H
