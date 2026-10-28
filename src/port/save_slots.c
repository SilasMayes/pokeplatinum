// Save slots. See include/port/save_slots.h
#include "port/save_slots.h"

#include <nitro.h>
#include <simulator/sim_backup.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "port/autosave.h"
#include "port/save_sync.h"
#include "charcode_convert.h"
#include "port/sim_config_prj.h"

#include "constants/charcode.h"
#include "constants/string.h"

#include "play_time.h"
#include "pokedex.h"
#include "save_player.h"
#include "savedata.h"
#include "trainer_info.h"

static int sCurrentSlot = 0;
static BOOL sMainMenuActive = FALSE;
static int sRequestedSwitch = -1;

static int ClampSlot(int slot)
{
    if (slot < 0) {
        return 0;
    }
    if (slot >= SAVE_SLOT_COUNT) {
        return SAVE_SLOT_COUNT - 1;
    }
    return slot;
}

void SaveSlots_FilePath(int slot, char *out, int outSize)
{
    slot = ClampSlot(slot);
    if (slot == 0) {
        // The port's original save file, so saves from before slots existed are slot 1
        snprintf(out, outSize, "%s", SIM_BACKUP_DEFAULT_PATH);
    } else {
        snprintf(out, outSize, "save_slot%d.bin", slot + 1);
    }
}

static void InfoPath(int slot, char *out, int outSize)
{
    snprintf(out, outSize, "save_slot%d.info", ClampSlot(slot) + 1);
}

void SaveSlots_Init(void)
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    sCurrentSlot = ClampSlot(config->saveSlot - 1);

    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(sCurrentSlot, path, sizeof(path));
    SIM_Backup_SetPath(path);
}

int SaveSlots_Current(void)
{
    return sCurrentSlot;
}

// A slot has data when its file exists and isn't blank (an all-0x00 or all-0xFF image,
// which older builds created on first launch even before the player saved).
BOOL SaveSlots_HasData(int slot)
{
    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return FALSE;
    }

    static u8 buffer[0x1000];
    BOOL hasData = FALSE;
    size_t got;
    while (!hasData && (got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < got; i++) {
            if (buffer[i] != 0x00 && buffer[i] != 0xFF) {
                hasData = TRUE;
                break;
            }
        }
    }
    fclose(file);
    return hasData;
}

BOOL SaveSlots_AnyOtherSlotHasData(void)
{
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        if (i != sCurrentSlot && SaveSlots_HasData(i)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void WriteSummary(int slot, SaveData *saveData, BOOL justSaved)
{
    if (!SaveData_DataExists(saveData)) {
        return;
    }

    TrainerInfo *trainerInfo = SaveData_GetTrainerInfo(saveData);
    PlayTime *playTime = SaveData_GetPlayTime(saveData);
    Pokedex *pokedex = SaveData_GetPokedex(saveData);

    // The name ends at EOS (the converter itself doesn't stop there)
    const charcode_t *nameChars = TrainerInfo_Name(trainerInfo);
    charcode_t nameCopy[TRAINER_NAME_LEN + 1];
    int nameLen = 0;
    while (nameLen < TRAINER_NAME_LEN && nameChars[nameLen] != CHAR_EOS) {
        nameCopy[nameLen] = nameChars[nameLen];
        nameLen++;
    }
    char name[16];
    memset(name, 0, sizeof(name));
    CharCode_ToAsciiString(nameCopy, name, nameLen);

    // Keep the previous save time when only refreshing the summary of a loaded slot
    char savedAt[32] = "";
    if (justSaved) {
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        if (local != NULL) {
            strftime(savedAt, sizeof(savedAt), "%Y-%m-%d %H:%M", local);
        }
    } else {
        SaveSlotInfo old;
        SaveSlots_GetInfo(slot, &old);
        if (old.hasSummary) {
            snprintf(savedAt, sizeof(savedAt), "%s", old.savedAt);
        }
    }

    char path[64];
    InfoPath(slot, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "name=%s\n", name);
    fprintf(file, "hours=%d\n", PlayTime_GetHours(playTime));
    fprintf(file, "minutes=%d\n", PlayTime_GetMinutes(playTime));
    fprintf(file, "badges=%d\n", TrainerInfo_BadgeCount(trainerInfo));
    fprintf(file, "pokedex=%d\n", Pokedex_IsObtained(pokedex) ? 1 : 0);
    fprintf(file, "seen=%d\n", Pokedex_CountSeen(pokedex));
    fprintf(file, "saved=%s\n", savedAt);
    fclose(file);
}

void SaveSlots_GetInfo(int slot, SaveSlotInfo *out)
{
    memset(out, 0, sizeof(*out));
    out->hasData = SaveSlots_HasData(slot);
    if (!out->hasData) {
        return;
    }

    char path[64];
    InfoPath(slot, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return; // a save that hasn't been loaded by this build yet (imported, or older)
    }

    char line[96];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        if (strcmp(key, "name") == 0) {
            snprintf(out->name, sizeof(out->name), "%s", value);
        } else if (strcmp(key, "hours") == 0) {
            out->hours = atoi(value);
        } else if (strcmp(key, "minutes") == 0) {
            out->minutes = atoi(value);
        } else if (strcmp(key, "badges") == 0) {
            out->badges = atoi(value);
        } else if (strcmp(key, "pokedex") == 0) {
            out->pokedexObtained = atoi(value) != 0;
        } else if (strcmp(key, "seen") == 0) {
            out->seen = atoi(value);
        } else if (strcmp(key, "saved") == 0) {
            snprintf(out->savedAt, sizeof(out->savedAt), "%s", value);
        }
    }
    fclose(file);
    out->hasSummary = TRUE;
}

void SaveSlots_Select(SaveData *saveData, int slot)
{
    slot = ClampSlot(slot);
    sCurrentSlot = slot;

    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    config->saveSlot = slot + 1;
    SIM_Config_prj_SaveConfigFile(config);

    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));
    SIM_Backup_SetPath(path);

    SaveSync_BeforeLoad(slot);
    SaveData_Reload(saveData);
    SaveSlots_OnLoaded(saveData);
}

void SaveSlots_OnSaved(SaveData *saveData)
{
    WriteSummary(sCurrentSlot, saveData, TRUE);
    Autosave_NoteSaved();
    SaveSync_OnSaved(sCurrentSlot);
}

void SaveSlots_OnLoaded(SaveData *saveData)
{
    WriteSummary(sCurrentSlot, saveData, FALSE);
    Autosave_NoteSaved();
}

void SaveSlots_SetMainMenuActive(BOOL active)
{
    sMainMenuActive = active;
    if (!active) {
        sRequestedSwitch = -1;
    }
}

BOOL SaveSlots_IsMainMenuActive(void)
{
    return sMainMenuActive;
}

void SaveSlots_RequestSwitch(int slot)
{
    if (sMainMenuActive) {
        sRequestedSwitch = ClampSlot(slot);
    }
}

int SaveSlots_TakeRequestedSwitch(void)
{
    int slot = sRequestedSwitch;
    sRequestedSwitch = -1;
    return slot;
}

BOOL SaveSlots_IsInUse(int slot)
{
    return slot == sCurrentSlot && !sMainMenuActive;
}

static void SetError(char *err, int errSize, const char *message)
{
    if (err != NULL && errSize > 0) {
        snprintf(err, errSize, "%s", message);
    }
}

static BOOL WriteWholeFile(const char *path, const u8 *data, u32 size)
{
    // Write next to the target and rename over it, so a failed write never leaves a
    // half-written save behind
    char tmpPath[SIM_BACKUP_PATH_MAX + 8];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);

    FILE *file = fopen(tmpPath, "wb");
    if (file == NULL) {
        return FALSE;
    }
    BOOL ok = fwrite(data, 1, size, file) == size;
    ok = (fflush(file) == 0) && ok;
    ok = (fclose(file) == 0) && ok;
    if (!ok) {
        remove(tmpPath);
        return FALSE;
    }

    remove(path); // rename() doesn't replace existing files on Windows
    if (rename(tmpPath, path) != 0) {
        remove(tmpPath);
        return FALSE;
    }
    return TRUE;
}

BOOL SaveSlots_ReplaceImage(int slot, const u8 *image, char *err, int errSize)
{
    slot = ClampSlot(slot);
    if (!SaveData_IsValidBackupImage((u8 *)image, SAVE_SLOT_IMAGE_SIZE)) {
        SetError(err, errSize, "This doesn't look like a Pokemon Platinum save.");
        return FALSE;
    }

    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));

    // Keep whatever was in the slot
    if (SaveSlots_HasData(slot)) {
        char backupPath[SIM_BACKUP_PATH_MAX + 8];
        snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
        remove(backupPath);
        rename(path, backupPath);
    }

    if (!WriteWholeFile(path, image, SAVE_SLOT_IMAGE_SIZE)) {
        SetError(err, errSize, "Could not write the save file.");
        return FALSE;
    }

    // The old summary belongs to the replaced save; it is rebuilt when the slot loads
    char infoPath[64];
    InfoPath(slot, infoPath, sizeof(infoPath));
    remove(infoPath);

    // Replacing the slot shown on the main menu: reload it there
    if (slot == sCurrentSlot) {
        SaveSlots_RequestSwitch(slot);
    }
    return TRUE;
}

BOOL SaveSlots_Import(int slot, const char *sourcePath, char *err, int errSize)
{
    slot = ClampSlot(slot);
    if (SaveSlots_IsInUse(slot)) {
        SetError(err, errSize, "That slot's game is running. Go back to the main menu first.");
        return FALSE;
    }

    FILE *source = fopen(sourcePath, "rb");
    if (source == NULL) {
        SetError(err, errSize, "Could not open the file.");
        return FALSE;
    }

    u8 *image = malloc(SAVE_SLOT_IMAGE_SIZE);
    if (image == NULL) {
        fclose(source);
        SetError(err, errSize, "Out of memory.");
        return FALSE;
    }

    // .sav files are exactly the flash image; DeSmuME's .dsv adds a footer after it
    size_t got = fread(image, 1, SAVE_SLOT_IMAGE_SIZE, source);
    fclose(source);

    if (got < SAVE_SLOT_IMAGE_SIZE) {
        free(image);
        SetError(err, errSize, "Not a Platinum save: Platinum saves are 512 KB.");
        return FALSE;
    }

    BOOL ok = SaveSlots_ReplaceImage(slot, image, err, errSize);
    free(image);
    return ok;
}

void SaveSlots_WriteInfo(int slot, const SaveSlotInfo *info)
{
    char path[64];
    InfoPath(slot, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "name=%s\n", info->name);
    fprintf(file, "hours=%d\n", info->hours);
    fprintf(file, "minutes=%d\n", info->minutes);
    fprintf(file, "badges=%d\n", info->badges);
    fprintf(file, "pokedex=%d\n", info->pokedexObtained ? 1 : 0);
    fprintf(file, "seen=%d\n", info->seen);
    fprintf(file, "saved=%s\n", info->savedAt);
    fclose(file);
}

BOOL SaveSlots_Export(int slot, const char *destPath, char *err, int errSize)
{
    slot = ClampSlot(slot);
    if (!SaveSlots_HasData(slot)) {
        SetError(err, errSize, "That slot is empty.");
        return FALSE;
    }

    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));

    u8 *image = malloc(SAVE_SLOT_IMAGE_SIZE);
    if (image == NULL) {
        SetError(err, errSize, "Out of memory.");
        return FALSE;
    }
    // Unwritten flash reads 0xFF on hardware and in emulators
    memset(image, 0xFF, SAVE_SLOT_IMAGE_SIZE);

    FILE *source = fopen(path, "rb");
    if (source == NULL) {
        free(image);
        SetError(err, errSize, "Could not read the slot.");
        return FALSE;
    }
    size_t got = fread(image, 1, SAVE_SLOT_IMAGE_SIZE, source);
    (void)got;
    fclose(source);

    FILE *dest = fopen(destPath, "wb");
    if (dest == NULL) {
        free(image);
        SetError(err, errSize, "Could not create the file.");
        return FALSE;
    }
    BOOL ok = fwrite(image, 1, SAVE_SLOT_IMAGE_SIZE, dest) == SAVE_SLOT_IMAGE_SIZE;
    ok = (fclose(dest) == 0) && ok;
    free(image);
    if (!ok) {
        SetError(err, errSize, "Could not write the file.");
    }
    return ok;
}
