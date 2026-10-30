// Platinum's side of port-common: the game adapter (port/port_game.h, in the
// port-common subproject). Everything port-common needs to know about Platinum's
// save data, overworld and settings goes through here.
#include "port/port_game.h"

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "constants/charcode.h"
#include "constants/field/map_load.h"
#include "constants/player_avatar.h"
#include "constants/string.h"
#include "generated/map_headers.h"

#include "field/field_system.h"
#include "overlay005/save_info_window.h"
#include "port/debug_field.h"
#include "port/nuzlocke.h"
#include "port/shiny_hunt.h"
#include "port/sim_config_prj.h"

#include "charcode_convert.h"
#include "comm_manager.h"
#include "field_task.h"
#include "location.h"
#include "map_object.h"
#include "play_time.h"
#include "player_avatar.h"
#include "pokedex.h"
#include "save_player.h"
#include "savedata.h"
#include "system_flags.h"
#include "trainer_info.h"
#include "unk_020655F4.h"
#include "unk_0206B9D8.h"
#include "vars_flags.h"

// --- Identity

const char *PortGame_Name(void)
{
    return "Pokemon Platinum";
}

const char *PortGame_ShortName(void)
{
    return "Platinum";
}

// --- Settings

int PortGame_Config_GetSaveSlot(void)
{
    return SIM_Config_prj_GetConfig()->saveSlot;
}

void PortGame_Config_SetSaveSlot(int slot)
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    config->saveSlot = slot;
    SIM_Config_prj_SaveConfigFile(config);
}

BOOL PortGame_Config_GetAutosave(PortAutosaveConfig *out)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config == NULL) {
        return FALSE;
    }
    out->enabled = config->autosave;
    out->minutes = config->autosaveMinutes;
    out->onExit = config->autosaveOnExit;
    return TRUE;
}

// --- Save data

BOOL PortGame_Save_GetSummary(SaveData *saveData, PortSaveSummary *out)
{
    if (!SaveData_DataExists(saveData)) {
        return FALSE;
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
    memset(out, 0, sizeof(*out));
    CharCode_ToAsciiString(nameCopy, out->name, nameLen);

    out->hours = PlayTime_GetHours(playTime);
    out->minutes = PlayTime_GetMinutes(playTime);
    out->badges = TrainerInfo_BadgeCount(trainerInfo);
    out->pokedexObtained = Pokedex_IsObtained(pokedex) ? TRUE : FALSE;
    out->seen = Pokedex_CountSeen(pokedex);
    return TRUE;
}

void PortGame_Save_Reload(SaveData *saveData)
{
    SaveData_Reload(saveData);
}

BOOL PortGame_Save_IsValidImage(const u8 *image, u32 size)
{
    return SaveData_IsValidBackupImage(image, size);
}

// --- Overworld

BOOL PortGame_Field_IsRunning(void)
{
    return DEBUG_GetFieldSystem() != NULL;
}

static BOOL PlayerIsStill(FieldSystem *fieldSystem)
{
    PlayerAvatar *playerAvatar = fieldSystem->playerAvatar;
    if (playerAvatar == NULL) {
        return FALSE;
    }

    MapObject *mapObj = PlayerAvatar_GetMapObject(playerAvatar);
    if (mapObj == NULL) {
        return FALSE;
    }

    // "Animation set" here means ready for a new movement: FALSE while mid-step,
    // turning, jumping, sliding on ice or a slope...
    if (!LocalMapObj_IsAnimationSet(mapObj)) {
        return FALSE;
    }

    return PlayerAvatar_GetPlayerMoveState(playerAvatar) == PLAYER_MOVE_STATE_NONE;
}

BOOL PortGame_Field_CanSave(const char **reason)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    const char *why = NULL;

    if (fieldSystem == NULL) {
        why = "No game is running";
    } else if (fieldSystem->processManager == NULL || fieldSystem->processManager->pause
        || fieldSystem->processManager->child != NULL || !fieldSystem->runningFieldMap) {
        why = "Not on the overworld";
    } else if (FieldSystem_IsRunningTask(fieldSystem)) {
        why = "Something is happening (event, menu or warp)";
    } else if (fieldSystem->mapLoadType != MAP_LOAD_TYPE_OVERWORLD
        || fieldSystem->location->mapHeaderID == MAP_HEADER_BATTLE_TOWER
        || FieldSystem_IsInBattleTowerSalon(fieldSystem)) {
        why = "Saving isn't allowed here";
    } else if (SystemFlag_CheckSafariGameActive(SaveData_GetVarsFlags(fieldSystem->saveData))
        || SystemFlag_CheckInPalPark(SaveData_GetVarsFlags(fieldSystem->saveData))) {
        why = "Saving isn't allowed here";
    } else if (CommManager_IsInitializedNotPoketch()) {
        why = "Wireless session active";
    } else if (!SaveData_DataExists(fieldSystem->saveData) || SaveData_IsNewGameData(fieldSystem->saveData)) {
        // Never autosave a new game until the player has chosen to save it (and, if the
        // slot held another game, agreed to overwrite it)
        why = "Save once from the menu first";
    } else if (!PlayerIsStill(fieldSystem)) {
        why = "Player is moving";
    }

    if (reason != NULL) {
        *reason = why;
    }
    return why == NULL;
}

BOOL PortGame_Field_Save(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    return fieldSystem != NULL && FieldSystem_Save(fieldSystem);
}

// --- Versions (Renegade Platinum's Complete and Classic; see port/save_version.h)

// The files a version may differ in (libntr keeps the versions' copies of them)
static const char *const sVersionFiles[] = {
    "poketool/personal/pl_personal.narc",
};

int SIM_Prj_VersionFiles(const char *const **paths)
{
    *paths = sVersionFiles;
    return (int)(sizeof(sVersionFiles) / sizeof(sVersionFiles[0]));
}

// Renegade Platinum: the game without a version patch is Complete, ClassicVersion makes Classic
void SIM_Prj_VersionName(const char *patchName, char *name, int nameSize)
{
    if (patchName == NULL) {
        snprintf(name, nameSize, "Complete");
        return;
    }
    char lower[64];
    snprintf(lower, sizeof(lower), "%s", patchName);
    for (char *c = lower; *c != '\0'; c++) {
        if (*c >= 'A' && *c <= 'Z') {
            *c = (char)(*c - 'A' + 'a');
        }
    }
    if (strstr(lower, "classic") != NULL) {
        snprintf(name, nameSize, "Classic");
        return;
    }
    // "SomethingVersion" -> "Something"
    snprintf(name, nameSize, "%s", patchName);
    size_t length = strlen(name);
    if (length > 7 && strcmp(name + length - 7, "Version") == 0) {
        name[length - 7] = '\0';
    }
}

const char *PortGame_VersionDescription(const char *name)
{
    if (strcmp(name, "Complete") == 0) {
        return "Renegade Platinum as Drayano made it: many Pokemon have new types, higher base stats and new "
               "abilities.";
    }
    if (strcmp(name, "Classic") == 0) {
        return "The official types and base stats, as in the newer games (Fairy included). Trainers, wild Pokemon, "
               "moves and items are the same as Complete; a few abilities differ.";
    }
    return NULL;
}

// --- Save slot events for the port modules still here

void PortGame_OnSlotSidecarsReplaced(int slot)
{
    (void)slot;
    Nuzlocke_ForgetLoaded();
    ShinyHunt_ForgetLoaded();
}
