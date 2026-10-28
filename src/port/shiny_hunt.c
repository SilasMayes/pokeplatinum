// See include/port/shiny_hunt.h
#include "port/shiny_hunt.h"

#include <nitro.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "constants/battle.h"
#include "constants/species.h"

#include "port/cheats.h"
#include "port/gear_data.h"
#include "port/save_slots.h"

#include "party.h"
#include "pokemon.h"

static ShinyHuntStats sStats;
static u32 sSpecies[MAX_SPECIES + 1]; // met since that species' last shiny
static int sLoadedSlot = -1;

static void FilePath(int slot, char *out, size_t size)
{
    snprintf(out, size, "save_slot%d.hunt", slot + 1);
}

static void Save(void)
{
    char path[64];
    FilePath(sLoadedSlot, path, sizeof(path));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }

    fprintf(file, "encounters=%u\neggs=%u\nresets=%u\n", sStats.encounters, sStats.eggs, sStats.resets);
    fprintf(file, "total_encounters=%u\ntotal_eggs=%u\ntotal_resets=%u\nshinies=%u\n", sStats.totalEncounters,
        sStats.totalEggs, sStats.totalResets, sStats.shinies);
    for (int i = sStats.findCount - 1; i >= 0; i--) {
        const ShinyHuntFind *find = &sStats.finds[i];
        fprintf(file, "find=%u,%u,%u,%u,%s\n", find->species, find->method, find->count, find->resets, find->date);
    }
    for (int species = 1; species <= MAX_SPECIES; species++) {
        if (sSpecies[species] != 0) {
            fprintf(file, "species=%d,%u\n", species, sSpecies[species]);
        }
    }
    fclose(file);
}

static void AddFind(u16 species, u8 method, u32 count, u32 resets);

static void Load(int slot)
{
    memset(&sStats, 0, sizeof(sStats));
    memset(sSpecies, 0, sizeof(sSpecies));
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
        char date[32] = "";
        if (sscanf(line, "encounters=%u", &a) == 1) {
            sStats.encounters = a;
        } else if (sscanf(line, "eggs=%u", &a) == 1) {
            sStats.eggs = a;
        } else if (sscanf(line, "resets=%u", &a) == 1) {
            sStats.resets = a;
        } else if (sscanf(line, "total_encounters=%u", &a) == 1) {
            sStats.totalEncounters = a;
        } else if (sscanf(line, "total_eggs=%u", &a) == 1) {
            sStats.totalEggs = a;
        } else if (sscanf(line, "total_resets=%u", &a) == 1) {
            sStats.totalResets = a;
        } else if (sscanf(line, "shinies=%u", &a) == 1) {
            sStats.shinies = a;
        } else if (sscanf(line, "find=%u,%u,%u,%u,%31[^\n]", &a, &b, &c, &d, date) >= 4) {
            // Oldest first in the file: each one goes on top
            AddFind((u16)a, (u8)b, c, d);
            snprintf(sStats.finds[0].date, sizeof(sStats.finds[0].date), "%s", date);
        } else if (sscanf(line, "species=%u,%u", &a, &b) == 2 && a <= MAX_SPECIES) {
            sSpecies[a] = b;
        }
    }
    fclose(file);
}

// The slot's file was replaced (save sync): read it again when next needed
void ShinyHunt_ForgetLoaded(void)
{
    sLoadedSlot = -1;
}

static void EnsureLoaded(void)
{
    const int slot = SaveSlots_Current();
    if (slot != sLoadedSlot) {
        Load(slot);
    }
}

static void AddFind(u16 species, u8 method, u32 count, u32 resets)
{
    if (sStats.findCount < SHINY_HUNT_LOG_MAX) {
        sStats.findCount++;
    }
    memmove(&sStats.finds[1], &sStats.finds[0], sizeof(ShinyHuntFind) * (sStats.findCount - 1));

    ShinyHuntFind *find = &sStats.finds[0];
    memset(find, 0, sizeof(*find));
    find->species = species;
    find->method = method;
    find->count = count;
    find->resets = resets;
    const time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    if (local != NULL) {
        strftime(find->date, sizeof(find->date), "%Y-%m-%d %H:%M", local);
    }
}

const ShinyHuntStats *ShinyHunt_Get(void)
{
    EnsureLoaded();
    return &sStats;
}

u32 ShinyHunt_SpeciesCount(u16 species)
{
    EnsureLoaded();
    return species <= MAX_SPECIES ? sSpecies[species] : 0;
}

double ShinyHunt_Chance(u32 tries)
{
    const double odds = (double)Cheats_ShinyOdds();
    return odds <= 1.0 ? 1.0 : 1.0 - pow((odds - 1.0) / odds, (double)tries);
}

void ShinyHunt_ResetCounters(void)
{
    EnsureLoaded();
    sStats.encounters = 0;
    sStats.eggs = 0;
    sStats.resets = 0;
    memset(sSpecies, 0, sizeof(sSpecies));
    Save();
}

void ShinyHunt_OnBattleStart(u32 battleType, Party *wildParty)
{
    if (wildParty == NULL
        || (battleType & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_CATCH_TUTORIAL))) {
        return;
    }
    EnsureLoaded();

    const int count = Party_GetCurrentCount(wildParty);
    for (int i = 0; i < count; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(wildParty, i);
        const u16 species = (u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        if (species == SPECIES_NONE || species > MAX_SPECIES) {
            continue;
        }

        sStats.encounters++;
        sStats.totalEncounters++;
        sSpecies[species]++;
        if (Pokemon_IsShiny(mon)) {
            AddFind(species, SHINY_HUNT_MET, sStats.encounters, sStats.resets);
            sStats.shinies++;
            sStats.encounters = 0;
            sStats.resets = 0;
            sSpecies[species] = 0;
        }
    }
    Save();
}

void ShinyHunt_OnEggHatched(Pokemon *mon)
{
    if (mon == NULL) {
        return;
    }
    EnsureLoaded();

    sStats.eggs++;
    sStats.totalEggs++;
    if (Pokemon_IsShiny(mon)) {
        AddFind((u16)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL), SHINY_HUNT_EGG, sStats.eggs, 0);
        sStats.shinies++;
        sStats.eggs = 0;
    }
    Save();
}

void ShinyHunt_OnSoftReset(void)
{
    // Only resets from a game in progress (not from the title screen or the menus before it)
    if (!Gear_IsGameRunning()) {
        return;
    }
    EnsureLoaded();
    sStats.resets++;
    sStats.totalResets++;
    Save();
}
