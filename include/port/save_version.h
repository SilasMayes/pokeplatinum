// Versions of the game. Renegade Platinum comes as Complete and Classic, which differ only in
// the Pokemon data (types, base stats, some abilities); added with its Classic patch, the
// game holds both (libntr's carts: versions.ini, versions/<name>/). Each save keeps the
// version it started with (save_slotN.version), starting a new game asks which one, and the
// game's files come from the version in use (SIM_Carts_UseVersion). A game without
// versions never notices any of this.
#ifndef POKEPLATINUM_PORT_SAVE_VERSION_H
#define POKEPLATINUM_PORT_SAVE_VERSION_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int SaveVersion_Count(void); // 0: the game has no versions
const char *SaveVersion_Name(int index);
const char *SaveVersion_Description(int index);
int SaveVersion_InUse(void);

// The slot's version: the one its save started with, or the game's own files' one for a
// save that doesn't say (imported, or from before the game had versions)
int SaveVersion_ForSlot(int slot);
// The game plays slot now (loaded at startup or switched to): its version's files
void SaveVersion_OnSlotLoaded(int slot);
// slot's save was just written: it is the game in progress, in the version in use
void SaveVersion_OnSaved(int slot);
// slot's save was replaced (imported, or synced: the sync brings the version after this)
void SaveVersion_ForgetSlot(int slot);
// Settings > Saves: a save from the other version (e.g. imported)
void SaveVersion_SetForSlot(int slot, int index);
// Changes whenever the files in use do (for caches of the Pokemon data)
u32 SaveVersion_Generation(void);

// Starting a new game: asks which version (the game waits meanwhile); then
// SaveVersion_TakeNewGameAnswer: -2 while asking, -1 if the player went back, else the
// version chosen, which is now in use
void SaveVersion_AskForNewGame(BOOL canCancel);
int SaveVersion_TakeNewGameAnswer(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_SAVE_VERSION_H
