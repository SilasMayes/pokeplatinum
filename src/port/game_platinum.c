// Platinum's side of port-common: the game adapter (port/port_game.h, in the
// port-common subproject). Everything port-common needs to know about Platinum's
// save data, overworld and settings goes through here.
#include "port/port_game.h"

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/charcode.h"
#include "constants/field/map_load.h"
#include "constants/player_avatar.h"
#include "constants/species.h"
#include "constants/string.h"
#include "generated/map_headers.h"

#include "field/field_system.h"
#include "overlay005/save_info_window.h"
#include "port/debug_field.h"
#include "port/gear_data.h"
#include "port/rom_patches.h"
#include "port/sim_config_prj.h"
#include "savedata/save_table.h"

#include "charcode_convert.h"
#include "comm_manager.h"
#include "field_task.h"
#include "location.h"
#include "map_header.h"
#include "map_object.h"
#include "party.h"
#include "pc_boxes.h"
#include "play_time.h"
#include "player_avatar.h"
#include "pokedex.h"
#include "pokemon.h"
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

BOOL PortGame_Config_GetCheats(PortCheatsConfig *out)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config == NULL) {
        return FALSE;
    }
    out->shinyOdds = config->shinyOdds;
    out->expPercent = config->expPercent;
    out->alwaysCatch = config->alwaysCatch;
    out->infinitePP = config->infinitePP;
    out->alwaysEscape = config->alwaysEscape;
    out->fastEggs = config->fastEggs;
    out->anyTM = config->anyTM;
    out->expShareAll = config->expShareAll;
    return TRUE;
}

BOOL PortGame_Config_GetNuzlocke(PortNuzlockeConfig *out)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config == NULL) {
        return FALSE;
    }
    out->enabled = config->nuzlocke;
    out->dupes = config->nuzlockeDupes;
    out->shiny = config->nuzlockeShiny;
    out->toPC = config->nuzlockeToPC;
    return TRUE;
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

// --- Pokémon

_Static_assert(PORT_SPECIES_MAX == MAX_SPECIES, "port-common's species count is Platinum's");

int PortGame_Party_Count(Party *party)
{
    return Party_GetCurrentCount(party);
}

Pokemon *PortGame_Party_Mon(Party *party, int index)
{
    return Party_GetPokemonBySlotIndex(party, index);
}

void PortGame_Mon_GetInfo(Pokemon *mon, PortMonInfo *out)
{
    out->species = (u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    out->level = (u8)Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
    out->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) ? TRUE : FALSE;
    out->shiny = Pokemon_IsShiny(mon) ? TRUE : FALSE;
    out->hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    out->personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
    out->otID = Pokemon_GetValue(mon, MON_DATA_OT_ID, NULL);
}

void PortGame_Mon_GetName(Pokemon *mon, char *out, int outSize)
{
    charcode_t nickname[MON_NAME_LEN + 1];
    Pokemon_GetValue(mon, MON_DATA_NICKNAME, nickname);
    Gear_TextToAscii(nickname, MON_NAME_LEN + 1, out, outSize);
}

void PortGame_Mon_SetPersonality(Pokemon *mon, u32 personality)
{
    Pokemon_PortSetPersonality(mon, personality);
}

BOOL PortGame_IsPersonalityShiny(u32 otID, u32 personality)
{
    return Pokemon_IsPersonalityShiny(otID, personality) ? TRUE : FALSE;
}

// Shiny when the shiny value is below the threshold (8 of 65536 in vanilla)
u32 PortGame_BaseShinyOdds(void)
{
    const int threshold = RomPatches_Info()->shinyThreshold;
    return threshold > 0 ? 65536u / (u32)threshold : 8192u;
}

BOOL PortGame_Party_MoveToPC(Party *party, int index)
{
    SaveData *saveData = SaveData_Ptr();
    if (saveData == NULL) {
        return FALSE;
    }
    PCBoxes *pcBoxes = SaveData_GetPCBoxes(saveData);
    int box = USE_CURRENT_BOX, slot = 0;
    if (!PCBoxes_TryGetNextAvailableSpace(pcBoxes, &box, &slot)
        || !PCBoxes_TryStoreBoxMonAt(pcBoxes, box, slot, Pokemon_GetBoxPokemon(Party_GetPokemonBySlotIndex(party, index)))) {
        return FALSE;
    }
    Party_RemovePokemonBySlotIndex(party, index);
    SaveData_SetFullSaveRequired();
    return TRUE;
}

BOOL PortGame_PlayerTrainerID(u32 *out)
{
    SaveData *saveData = SaveData_Ptr();
    if (saveData == NULL) {
        return FALSE;
    }
    *out = TrainerInfo_ID(SaveData_GetTrainerInfo(saveData));
    return TRUE;
}

BOOL PortGame_Pokedex_HasCaught(u16 species)
{
    return Gear_SpeciesCaught(species);
}

u16 PortGame_Field_LocationName(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->location == NULL) {
        return 0xFFFF;
    }
    return (u16)MapHeader_GetMapLabelTextID(fieldSystem->location->mapHeaderID);
}

BOOL PortGame_Battle_Captured(int resultMask)
{
    return (resultMask & 0x7) == BATTLE_RESULT_CAPTURED_MON;
}

BOOL PortGame_Battle_Lost(int resultMask)
{
    return (resultMask & BATTLE_RESULT_LOSE) != 0;
}

BOOL PortGame_Battle_IsWild(u32 battleType)
{
    return (battleType & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_CATCH_TUTORIAL)) == 0;
}
