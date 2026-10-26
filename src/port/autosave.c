// Autosave. See include/port/autosave.h
#include "port/autosave.h"

#include <nitro.h>
#include <SDL2/SDL.h>
#include <simulator/sim_lifecycle.h>
#include <stdio.h>

#include "constants/field/map_load.h"
#include "constants/player_avatar.h"
#include "generated/map_headers.h"

#include "field/field_system.h"
#include "overlay005/save_info_window.h"
#include "port/debug_field.h"
#include "port/sim_config_prj.h"

#include "comm_manager.h"
#include "field_task.h"
#include "location.h"
#include "map_object.h"
#include "player_avatar.h"
#include "savedata.h"
#include "system_flags.h"
#include "unk_020655F4.h"
#include "unk_0206B9D8.h"
#include "vars_flags.h"

#define NOTICE_SHOW_MS 1800
#define NOTICE_FADE_MS 400

static u64 sLastSaveMs = 0;
static BOOL sSaving = FALSE;
static char sNotice[64];
static u64 sNoticeStartMs = 0;

void Autosave_NoteSaved(void)
{
    sLastSaveMs = SDL_GetTicks64();
}

FieldSystem *Autosave_GetFieldSystem(void)
{
    return DEBUG_GetFieldSystem();
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

BOOL Autosave_CanSaveNow(FieldSystem *fieldSystem, const char **reason)
{
    const char *why = NULL;

    if (fieldSystem == NULL) {
        why = "No game is running";
    } else if (sSaving) {
        why = "Already saving";
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

BOOL Autosave_SaveNow(FieldSystem *fieldSystem, const char *notice)
{
    if (!Autosave_CanSaveNow(fieldSystem, NULL)) {
        return FALSE;
    }

    sSaving = TRUE;
    BOOL saved = FieldSystem_Save(fieldSystem);
    sSaving = FALSE;

    if (saved) {
        printf("Autosave: saved (%s)\n", notice != NULL ? notice : "silent");
        if (notice != NULL) {
            Autosave_ShowNotice(notice);
        }
    } else {
        printf("Autosave: save failed\n");
        Autosave_ShowNotice("Save failed");
    }
    return saved;
}

void Autosave_FieldTick(FieldSystem *fieldSystem)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (!config->autosave) {
        return;
    }

    int minutes = config->autosaveMinutes;
    if (minutes < 1) {
        minutes = 1;
    }

    u64 now = SDL_GetTicks64();
    if (now - sLastSaveMs < (u64)minutes * 60 * 1000) {
        return;
    }

    const char *reason = NULL;
    if (!Autosave_CanSaveNow(fieldSystem, &reason)) {
        // Not safe this frame; try again on the next one
        static u64 sLastReasonLogMs = 0;
        if (now - sLastReasonLogMs > 10000) {
            sLastReasonLogMs = now;
            printf("Autosave: due, waiting (%s)\n", reason);
        }
        return;
    }

    Autosave_SaveNow(fieldSystem, "Autosaved");
}

// libntr calls this between frames when the app is about to pause or exit
void SIM_Prj_OnLifecycleEvent(int event)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config == NULL || !config->autosaveOnExit) {
        return;
    }

    FieldSystem *fieldSystem = Autosave_GetFieldSystem();
    const char *reason = NULL;
    if (!Autosave_CanSaveNow(fieldSystem, &reason)) {
        if (fieldSystem != NULL) {
            printf("Autosave: not saving on %s (%s)\n",
                event == SIM_LIFECYCLE_QUIT ? "exit" : "background", reason);
        }
        return;
    }

    // Nobody sees a notice while the app goes away
    Autosave_SaveNow(fieldSystem, NULL);
}

void Autosave_ShowNotice(const char *text)
{
    snprintf(sNotice, sizeof(sNotice), "%s", text);
    sNoticeStartMs = SDL_GetTicks64();
}

const char *Autosave_GetNotice(float *alpha)
{
    if (sNotice[0] == '\0') {
        return NULL;
    }

    u64 elapsed = SDL_GetTicks64() - sNoticeStartMs;
    if (elapsed >= NOTICE_SHOW_MS + NOTICE_FADE_MS) {
        sNotice[0] = '\0';
        return NULL;
    }

    *alpha = elapsed <= NOTICE_SHOW_MS ? 1.0f : 1.0f - (float)(elapsed - NOTICE_SHOW_MS) / NOTICE_FADE_MS;
    return sNotice;
}
