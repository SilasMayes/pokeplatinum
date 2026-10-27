// See include/port/nuzlocke.h
#include "port/nuzlocke.h"

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/species.h"

#include "field/field_system.h"
#include "port/debug_field.h"
#include "port/gear_data.h"
#include "port/save_slots.h"
#include "port/sim_config_prj.h"

#include "location.h"
#include "map_header.h"
#include "party.h"
#include "pc_boxes.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"

static NuzlockeRun sRun;
static int sLoadedSlot = -1;
static int sCurrent = NUZLOCKE_ENC_NONE; // the wild battle going on

BOOL Nuzlocke_Enabled(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    return config != NULL && config->nuzlocke;
}

static void FilePath(int slot, char *out, size_t size)
{
    snprintf(out, size, "save_slot%d.nuzlocke", slot + 1);
}

static void Save(void)
{
    char path[64];
    FilePath(sLoadedSlot, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    for (int i = 0; i < sRun.areaCount; i++) {
        const NuzlockeArea *area = &sRun.areas[i];
        fprintf(file, "area=%u,%u,%u\n", area->location, area->species, area->result);
    }
    for (int i = 0; i < sRun.deathCount; i++) {
        const NuzlockeDeath *death = &sRun.deaths[i];
        fprintf(file, "death=%u,%u,%u,%u,%s\n", (unsigned)death->personality, death->species, death->level, death->location,
            death->name);
    }
    fclose(file);
}

static void Load(int slot)
{
    memset(&sRun, 0, sizeof(sRun));
    sLoadedSlot = slot;
    char path[64];
    FilePath(slot, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned a, b, c, d;
        char name[32] = "";
        if (sscanf(line, "area=%u,%u,%u", &a, &b, &c) == 3 && sRun.areaCount < NUZLOCKE_MAX_AREAS) {
            NuzlockeArea *area = &sRun.areas[sRun.areaCount++];
            area->location = (u16)a;
            area->species = (u16)b;
            area->result = (u8)c;
        } else if (sscanf(line, "death=%u,%u,%u,%u,%15[^\n]", &a, &b, &c, &d, name) >= 4
            && sRun.deathCount < NUZLOCKE_MAX_DEATHS) {
            NuzlockeDeath *death = &sRun.deaths[sRun.deathCount++];
            death->personality = a;
            death->species = (u16)b;
            death->level = (u8)c;
            death->location = (u16)d;
            snprintf(death->name, sizeof(death->name), "%s", name);
        }
    }
    fclose(file);
}

static void EnsureLoaded(void)
{
    if (SaveSlots_Current() != sLoadedSlot) {
        Load(SaveSlots_Current());
    }
}

const NuzlockeRun *Nuzlocke_Get(void)
{
    EnsureLoaded();
    return &sRun;
}

static NuzlockeArea *FindArea(u16 location)
{
    for (int i = 0; i < sRun.areaCount; i++) {
        if (sRun.areas[i].location == location) {
            return &sRun.areas[i];
        }
    }
    return NULL;
}

const NuzlockeArea *Nuzlocke_Area(u16 location)
{
    EnsureLoaded();
    return FindArea(location);
}

int Nuzlocke_CurrentEncounter(void)
{
    return Nuzlocke_Enabled() ? sCurrent : NUZLOCKE_ENC_NONE;
}

BOOL Nuzlocke_IsDead(u32 personality)
{
    EnsureLoaded();
    for (int i = 0; i < sRun.deathCount; i++) {
        if (sRun.deaths[i].personality == personality) {
            return TRUE;
        }
    }
    return FALSE;
}

void Nuzlocke_Reset(void)
{
    EnsureLoaded();
    memset(&sRun, 0, sizeof(sRun));
    Save();
}

static u16 CurrentLocation(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->location == NULL) {
        return 0xFFFF;
    }
    return (u16)MapHeader_GetMapLabelTextID(fieldSystem->location->mapHeaderID);
}

static BOOL IsWild(u32 battleType)
{
    return (battleType & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_CATCH_TUTORIAL)) == 0;
}

void Nuzlocke_OnBattleStart(u32 battleType, Party *wildParty)
{
    sCurrent = NUZLOCKE_ENC_NONE;
    if (!Nuzlocke_Enabled() || !IsWild(battleType) || wildParty == NULL || Party_GetCurrentCount(wildParty) == 0) {
        return;
    }
    EnsureLoaded();
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    Pokemon *mon = Party_GetPokemonBySlotIndex(wildParty, 0);
    const u16 species = (u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    const u16 location = CurrentLocation();

    if (config->nuzlockeShiny && Pokemon_IsShiny(mon)) {
        sCurrent = NUZLOCKE_ENC_SHINY;
        return;
    }
    if (FindArea(location) != NULL) {
        sCurrent = NUZLOCKE_ENC_USED;
        return;
    }
    if (config->nuzlockeDupes && Gear_SpeciesCaught(species)) {
        sCurrent = NUZLOCKE_ENC_DUPE;
        return;
    }
    if (sRun.areaCount < NUZLOCKE_MAX_AREAS) {
        NuzlockeArea *area = &sRun.areas[sRun.areaCount++];
        area->location = location;
        area->species = species;
        area->result = NUZLOCKE_IN_BATTLE;
        Save();
    }
    sCurrent = NUZLOCKE_ENC_FIRST;
}

void Nuzlocke_OnBattleEnd(u32 battleType, int resultMask, Party *playerParty, Party *wildParty)
{
    const int encounter = sCurrent;
    sCurrent = NUZLOCKE_ENC_NONE;
    if (!Nuzlocke_Enabled()) {
        return;
    }
    EnsureLoaded();
    BOOL changed = FALSE;

    // The area's first encounter: caught or missed
    if (encounter == NUZLOCKE_ENC_FIRST) {
        NuzlockeArea *area = FindArea(CurrentLocation());
        if (area != NULL && area->result == NUZLOCKE_IN_BATTLE) {
            area->result = (resultMask & 0x7) == BATTLE_RESULT_CAPTURED_MON ? NUZLOCKE_CAUGHT : NUZLOCKE_MISSED;
            changed = TRUE;
        }
    }

    // Who fainted
    if (playerParty != NULL) {
        const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
        int alive = 0;
        for (int i = 0; i < Party_GetCurrentCount(playerParty); i++) {
            Pokemon *mon = Party_GetPokemonBySlotIndex(playerParty, i);
            if (!Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
                alive++;
            }
        }
        for (int i = Party_GetCurrentCount(playerParty) - 1; i >= 0; i--) {
            Pokemon *mon = Party_GetPokemonBySlotIndex(playerParty, i);
            if (Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL) || Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
                continue;
            }
            const u32 personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
            if (!Nuzlocke_IsDead(personality) && sRun.deathCount < NUZLOCKE_MAX_DEATHS) {
                NuzlockeDeath *death = &sRun.deaths[sRun.deathCount++];
                memset(death, 0, sizeof(*death));
                death->personality = personality;
                death->species = (u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
                death->level = (u8)Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
                death->location = CurrentLocation();
                charcode_t nickname[MON_NAME_LEN + 1];
                Pokemon_GetValue(mon, MON_DATA_NICKNAME, nickname);
                Gear_TextToAscii(nickname, MON_NAME_LEN + 1, death->name, sizeof(death->name));
                changed = TRUE;
            }
            // Off to the PC, while someone is left to carry on
            SaveData *saveData = SaveData_Ptr();
            if (config->nuzlockeToPC && alive > 0 && saveData != NULL && (resultMask & BATTLE_RESULT_LOSE) == 0) {
                PCBoxes *pcBoxes = SaveData_GetPCBoxes(saveData);
                int box = USE_CURRENT_BOX, slot = 0;
                if (PCBoxes_TryGetNextAvailableSpace(pcBoxes, &box, &slot)
                    && PCBoxes_TryStoreBoxMonAt(pcBoxes, box, slot, Pokemon_GetBoxPokemon(mon))) {
                    Party_RemovePokemonBySlotIndex(playerParty, i);
                    SaveData_SetFullSaveRequired();
                }
            }
        }
    }

    (void)wildParty;
    (void)battleType;
    if (changed) {
        Save();
    }
}
