// See include/port/gear_dex.h
#include "port/gear_dex.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/narc.h"
#include "constants/species.h"
#include "generated/evolution_methods.h"
#include "generated/items.h"
#include "generated/map_headers.h"

#include "overlay006/wild_encounters.h"
#include "port/gear_data.h"
#include "struct_defs/species.h"

#include "item.h"
#include "map_header.h"
#include "map_header_data.h"
#include "narc.h"
#include "pokemon.h"
#include "tutor_movesets.h"

#include "res/pokemon/species_egg_moves.h"
#include "res/pokemon/species_learnsets_by_tutor.h"

// --- every map's wild Pokemon, by species (built once) ----------------------------------------

typedef struct PlaceRecord {
    u16 species;
    GearDexPlace place;
} PlaceRecord;

static PlaceRecord *sPlaces = NULL;
static int sPlaceCount = 0;
static int sPlaceCapacity = 0;

static GearDexEvolution sEvos[MAX_SPECIES + 1][GEAR_DEX_MAX_EVOS];
static BOOL sIndexBuilt = FALSE;

static const u8 sGrassRates[MAX_GRASS_ENCOUNTERS] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
static const u8 sWaterRates[MAX_WATER_ENCOUNTERS] = { 60, 30, 5, 4, 1 };
static const u8 sRodRates[MAX_WATER_ENCOUNTERS] = { 40, 40, 15, 4, 1 };

static void AddPlace(u16 species, u16 label, u8 method, u8 gbaGames, u8 minLevel, u8 maxLevel, const u8 rates[GEAR_TIME_COUNT])
{
    if (species == SPECIES_NONE || species > MAX_SPECIES) {
        return;
    }
    // Merge with the same species, place and method
    for (int i = sPlaceCount - 1; i >= 0 && i >= sPlaceCount - 64; i--) {
        PlaceRecord *record = &sPlaces[i];
        if (record->species == species && record->place.label == label && record->place.method == method) {
            GearDexPlace *place = &record->place;
            place->gbaGames |= gbaGames;
            place->minLevel = minLevel < place->minLevel ? minLevel : place->minLevel;
            place->maxLevel = maxLevel > place->maxLevel ? maxLevel : place->maxLevel;
            for (int t = 0; t < GEAR_TIME_COUNT; t++) {
                const int sum = place->rates[t] + rates[t];
                place->rates[t] = sum > 100 ? 100 : (u8)sum;
            }
            return;
        }
    }
    if (sPlaceCount == sPlaceCapacity) {
        const int capacity = sPlaceCapacity ? sPlaceCapacity * 2 : 2048;
        PlaceRecord *grown = realloc(sPlaces, sizeof(PlaceRecord) * capacity);
        if (grown == NULL) {
            return;
        }
        sPlaces = grown;
        sPlaceCapacity = capacity;
    }
    PlaceRecord *record = &sPlaces[sPlaceCount++];
    memset(record, 0, sizeof(*record));
    record->species = species;
    record->place.label = label;
    record->place.method = method;
    record->place.gbaGames = gbaGames;
    record->place.minLevel = minLevel;
    record->place.maxLevel = maxLevel;
    memcpy(record->place.rates, rates, GEAR_TIME_COUNT);
}

static void AddWaterPlaces(u16 label, u8 method, const WaterEncounters *water, const u8 *rates)
{
    if (water->encounterRate == 0) {
        return;
    }
    for (int i = 0; i < MAX_WATER_ENCOUNTERS; i++) {
        const WaterEncounter *enc = &water->encounters[i];
        const u8 all[GEAR_TIME_COUNT] = { rates[i], rates[i], rates[i] };
        AddPlace((u16)enc->species, label, method, 0, enc->minLevel, enc->maxLevel, all);
    }
}

static int ComparePlaceRecords(const void *a, const void *b)
{
    const PlaceRecord *pa = a;
    const PlaceRecord *pb = b;
    if (pa->species != pb->species) {
        return pa->species - pb->species;
    }
    if (pa->place.method != pb->place.method) {
        return pa->place.method - pb->place.method;
    }
    return (pb->place.rates[GEAR_TIME_DAY] + pb->place.rates[GEAR_TIME_NIGHT])
        - (pa->place.rates[GEAR_TIME_DAY] + pa->place.rates[GEAR_TIME_NIGHT]);
}

static void BuildIndex(void)
{
    if (sIndexBuilt) {
        return;
    }
    sIndexBuilt = TRUE;
    sPlaceCount = 0;

    static WildEncounters data;
    static u16 doneArchives[MAP_HEADER_COUNT];
    static u16 doneLabels[MAP_HEADER_COUNT];
    int doneCount = 0;

    for (int header = 0; header < MAP_HEADER_COUNT; header++) {
        if (!MapHeader_HasWildEncounters(header)) {
            continue;
        }
        const u16 archive = (u16)MapHeader_GetWildEncountersArchiveID(header);
        const u16 label = (u16)MapHeader_GetMapLabelTextID(header);
        BOOL done = FALSE;
        for (int i = 0; i < doneCount && !done; i++) {
            done = doneArchives[i] == archive && doneLabels[i] == label;
        }
        if (done) {
            continue;
        }
        doneArchives[doneCount] = archive;
        doneLabels[doneCount] = label;
        doneCount++;

        MapHeaderData_LoadWildEncounters(&data, header);

        if (data.grassEncounters.encounterRate != 0) {
            for (int i = 0; i < MAX_GRASS_ENCOUNTERS; i++) {
                const GrassEncounter *enc = &data.grassEncounters.encounters[i];
                const u8 rate = sGrassRates[i];
                if (i == 2 || i == 3) {
                    // Morning: the slot's own Pokemon; day and evening, night: replaced
                    const u8 morning[GEAR_TIME_COUNT] = { rate, 0, 0 };
                    const u8 day[GEAR_TIME_COUNT] = { 0, rate, 0 };
                    const u8 night[GEAR_TIME_COUNT] = { 0, 0, rate };
                    AddPlace((u16)enc->species, label, GEAR_ENC_GRASS, 0, enc->level, enc->level, morning);
                    AddPlace((u16)data.dayEncounters[i - 2], label, GEAR_ENC_GRASS, 0, enc->level, enc->level, day);
                    AddPlace((u16)data.nightEncounters[i - 2], label, GEAR_ENC_GRASS, 0, enc->level, enc->level, night);
                } else {
                    const u8 all[GEAR_TIME_COUNT] = { rate, rate, rate };
                    AddPlace((u16)enc->species, label, GEAR_ENC_GRASS, 0, enc->level, enc->level, all);
                }
            }
            const u8 none[GEAR_TIME_COUNT] = { 0, 0, 0 };
            for (int i = 0; i < MAX_RADAR_ENCOUNTERS; i++) {
                const GrassEncounter *slot = &data.grassEncounters.encounters[4 + (i & 1)];
                AddPlace((u16)data.radarEncounters[i], label, GEAR_ENC_RADAR, 0, slot->level, slot->level, none);
            }
            for (int i = 0; i < MAX_SWARM_ENCOUNTERS; i++) {
                const GrassEncounter *slot = &data.grassEncounters.encounters[i];
                AddPlace((u16)data.swarmEncounters[i], label, GEAR_ENC_SWARM, 0, slot->level, slot->level, none);
            }
            // GBA slot: a game in the DS's GBA slot adds its Pokemon to slots 8 and 9
            const int *gba[5] = { data.dualSlotRubyEncounters, data.dualSlotSapphireEncounters,
                data.dualSlotEmeraldEncouters, data.dualSlotFireredEncounters, data.dualSlotLeafgreenEncounters };
            for (int game = 0; game < 5; game++) {
                for (int i = 0; i < MAX_DUAL_SLOT_ENCOUNTERS; i++) {
                    const GrassEncounter *slot = &data.grassEncounters.encounters[8 + i];
                    AddPlace((u16)gba[game][i], label, GEAR_ENC_GBA, (u8)(1 << game), slot->level, slot->level, none);
                }
            }
        }
        AddWaterPlaces(label, GEAR_ENC_SURF, &data.surfEncounters, sWaterRates);
        AddWaterPlaces(label, GEAR_ENC_OLD_ROD, &data.oldRodEncounters, sWaterRates);
        AddWaterPlaces(label, GEAR_ENC_GOOD_ROD, &data.goodRodEncounters, sRodRates);
        AddWaterPlaces(label, GEAR_ENC_SUPER_ROD, &data.superRodEncounters, sRodRates);
    }
    qsort(sPlaces, sPlaceCount, sizeof(PlaceRecord), ComparePlaceRecords);

    // Every species' evolutions, to find pre-evolutions
    memset(sEvos, 0, sizeof(sEvos));
    for (int species = 1; species <= MAX_SPECIES; species++) {
        u32 size = 0;
        u8 *file = NARC_PortReadMember(NARC_INDEX_POKETOOL__PERSONAL__EVO, species, &size);
        if (file == NULL) {
            continue;
        }
        for (int i = 0; i < GEAR_DEX_MAX_EVOS && (u32)(i + 1) * 6 <= size; i++) {
            memcpy(&sEvos[species][i].method, file + i * 6, 2);
            memcpy(&sEvos[species][i].param, file + i * 6 + 2, 2);
            memcpy(&sEvos[species][i].species, file + i * 6 + 4, 2);
        }
        free(file);
    }
}

// --- one species ---------------------------------------------------------------------------

BOOL Gear_GetSpecies(u16 species, GearSpecies *out)
{
    memset(out, 0, sizeof(*out));
    if (species == SPECIES_NONE || species > NATIONAL_DEX_COUNT) {
        return FALSE;
    }
    BuildIndex();
    out->species = species;
    out->sinnohNumber = Pokemon_SinnohDexNumber(species);

    u32 size = 0;
    u8 *file = NARC_PortReadMember(NARC_INDEX_POKETOOL__PERSONAL__PL_PERSONAL, species, &size);
    if (file == NULL || size < sizeof(SpeciesData)) {
        free(file);
        return FALSE;
    }
    SpeciesData data;
    memcpy(&data, file, sizeof(data));
    free(file);

    out->types[0] = data.types[0];
    out->types[1] = data.types[1];
    out->abilities[0] = data.abilities[0];
    out->abilities[1] = data.abilities[1];
    out->baseStats[0] = data.baseStats.hp;
    out->baseStats[1] = data.baseStats.attack;
    out->baseStats[2] = data.baseStats.defense;
    out->baseStats[3] = data.baseStats.spAttack;
    out->baseStats[4] = data.baseStats.spDefense;
    out->baseStats[5] = data.baseStats.speed;
    out->evYield[0] = data.evYields.hp;
    out->evYield[1] = data.evYields.attack;
    out->evYield[2] = data.evYields.defense;
    out->evYield[3] = data.evYields.spAttack;
    out->evYield[4] = data.evYields.spDefense;
    out->evYield[5] = data.evYields.speed;
    out->catchRate = data.catchRate;
    out->genderRatio = data.genderRatio;
    out->eggGroups[0] = data.eggGroups[0];
    out->eggGroups[1] = data.eggGroups[1];
    out->hatchCycles = data.hatchCycles;
    out->expRate = data.expRate;
    out->baseFriendship = data.baseFriendship;
    out->heldItems[0] = data.wildHeldItems.common;
    out->heldItems[1] = data.wildHeldItems.rare;

    // TMs and HMs
    for (int tm = 0; tm < 100; tm++) {
        if (data.tmLearnsetMasks[tm / 32] & (1u << (tm % 32))) {
            out->tms[out->tmCount++] = (u8)tm;
        }
    }

    // Evolutions, and what evolves into it
    for (int i = 0; i < GEAR_DEX_MAX_EVOS; i++) {
        if (sEvos[species][i].method != EVO_NONE && sEvos[species][i].species != SPECIES_NONE) {
            out->evos[out->evoCount++] = sEvos[species][i];
        }
    }
    for (int from = 1; from <= MAX_SPECIES && out->preEvoCount < 4; from++) {
        for (int i = 0; i < GEAR_DEX_MAX_EVOS; i++) {
            if (sEvos[from][i].method != EVO_NONE && sEvos[from][i].species == species) {
                GearDexEvolution *pre = &out->preEvos[out->preEvoCount++];
                *pre = sEvos[from][i];
                pre->species = (u16)from;
                break;
            }
        }
    }

    // Level-up moves (ROM hacks may have more than the vanilla 20)
    file = NARC_PortReadMember(NARC_INDEX_POKETOOL__PERSONAL__WOTBL, species, &size);
    if (file != NULL) {
        for (u32 i = 0; i + 1 < size && out->levelMoveCount < GEAR_DEX_MAX_LEVEL_MOVES; i += 2) {
            const u16 entry = (u16)(file[i] | (file[i + 1] << 8));
            if (entry == 0xFFFF) {
                break;
            }
            out->levelMoves[out->levelMoveCount].move = entry & 0x1FF;
            out->levelMoves[out->levelMoveCount].level = (u8)(entry >> 9);
            out->levelMoveCount++;
        }
        free(file);
    }

    // Egg moves
    for (int i = 0; i < (int)NELEMS(sEggMoves); i++) {
        if (sEggMoves[i] == EGG_MOVES_SPECIES_OFFSET + species) {
            for (int j = i + 1; j < (int)NELEMS(sEggMoves) && sEggMoves[j] < EGG_MOVES_SPECIES_OFFSET
                 && out->eggMoveCount < 16; j++) {
                out->eggMoves[out->eggMoveCount++] = sEggMoves[j];
            }
            break;
        }
    }

    // Move tutors (Route 212, Survival Area, Snowpoint)
    if (species <= MOVESET_MAX) {
        const MovesetMask *mask = &sSpeciesLearnsetsByTutor[species - 1];
        for (int i = 0; i < (int)NELEMS(sTeachableMoves) && out->tutorMoveCount < 40; i++) {
            if (mask->maskData[i / 8] & (1 << (i % 8))) {
                out->tutorMoves[out->tutorMoveCount++] = sTeachableMoves[i].moveID;
            }
        }
    }

    // Where to find it
    for (int i = 0; i < sPlaceCount && out->placeCount < GEAR_DEX_MAX_PLACES; i++) {
        if (sPlaces[i].species == species) {
            out->places[out->placeCount++] = sPlaces[i].place;
        }
    }
    return TRUE;
}

// --- text ----------------------------------------------------------------------------------

void Gear_EvolutionText(const GearDexEvolution *evo, char *out, int size)
{
    const char *item = Gear_ItemName(evo->param);
    switch (evo->method) {
    case EVO_LEVEL_HAPPINESS:
        snprintf(out, size, "High friendship");
        break;
    case EVO_LEVEL_HAPPINESS_DAY:
        snprintf(out, size, "High friendship, daytime");
        break;
    case EVO_LEVEL_HAPPINESS_NIGHT:
        snprintf(out, size, "High friendship, night");
        break;
    case EVO_LEVEL:
        snprintf(out, size, "Level %u", evo->param);
        break;
    case EVO_TRADE:
        snprintf(out, size, "Trade");
        break;
    case EVO_TRADE_WITH_HELD_ITEM:
        snprintf(out, size, "Trade holding %s", item);
        break;
    case EVO_USE_ITEM:
        snprintf(out, size, "Use %s", item);
        break;
    case EVO_LEVEL_ATK_GT_DEF:
        snprintf(out, size, "Level %u, Attack > Defense", evo->param);
        break;
    case EVO_LEVEL_ATK_EQ_DEF:
        snprintf(out, size, "Level %u, Attack = Defense", evo->param);
        break;
    case EVO_LEVEL_ATK_LT_DEF:
        snprintf(out, size, "Level %u, Attack < Defense", evo->param);
        break;
    case EVO_LEVEL_PID_LOW:
    case EVO_LEVEL_PID_HIGH:
        snprintf(out, size, "Level %u (depends on the Pokemon)", evo->param);
        break;
    case EVO_LEVEL_NINJASK:
        snprintf(out, size, "Level %u", evo->param);
        break;
    case EVO_LEVEL_SHEDINJA:
        snprintf(out, size, "Level %u, a free party slot and a Poke Ball", evo->param);
        break;
    case EVO_LEVEL_BEAUTY:
        snprintf(out, size, "Level up with Beauty %u+", evo->param);
        break;
    case EVO_USE_ITEM_MALE:
        snprintf(out, size, "Use %s (male)", item);
        break;
    case EVO_USE_ITEM_FEMALE:
        snprintf(out, size, "Use %s (female)", item);
        break;
    case EVO_LEVEL_WITH_HELD_ITEM_DAY:
        snprintf(out, size, "Level up holding %s, daytime", item);
        break;
    case EVO_LEVEL_WITH_HELD_ITEM_NIGHT:
        snprintf(out, size, "Level up holding %s, night", item);
        break;
    case EVO_LEVEL_KNOW_MOVE:
        snprintf(out, size, "Level up knowing %s", Gear_MoveName(evo->param));
        break;
    case EVO_LEVEL_SPECIES_IN_PARTY:
        snprintf(out, size, "Level up with %s in the party", Gear_SpeciesName(evo->param));
        break;
    case EVO_LEVEL_MALE:
        snprintf(out, size, "Level %u (male)", evo->param);
        break;
    case EVO_LEVEL_FEMALE:
        snprintf(out, size, "Level %u (female)", evo->param);
        break;
    case EVO_LEVEL_MAGNETIC_FIELD:
        snprintf(out, size, "Level up at Mt. Coronet");
        break;
    case EVO_LEVEL_MOSS_ROCK:
        snprintf(out, size, "Level up near the Moss Rock");
        break;
    case EVO_LEVEL_ICE_ROCK:
        snprintf(out, size, "Level up near the Ice Rock");
        break;
    default:
        snprintf(out, size, "Method %u (%u)", evo->method, evo->param);
        break;
    }
}

const char *Gear_EggGroupName(u8 group)
{
    static const char *const kNames[] = { "-", "Monster", "Water 1", "Bug", "Flying", "Field", "Fairy", "Grass",
        "Human-Like", "Water 3", "Mineral", "Amorphous", "Water 2", "Ditto", "Dragon", "Undiscovered" };
    return group < NELEMS(kNames) ? kNames[group] : "?";
}

const char *Gear_ExpRateName(u8 rate)
{
    static const char *const kNames[] = { "Medium Fast", "Erratic", "Fluctuating", "Medium Slow", "Fast", "Slow" };
    return rate < NELEMS(kNames) ? kNames[rate] : "?";
}

u16 Gear_TMMove(int tm)
{
    return tm >= 0 && tm < 100 ? Item_MoveForTMHM((u16)(ITEM_TM01 + tm)) : 0;
}

void Gear_SpeciesTypes(u16 species, u8 *type1, u8 *type2)
{
    static u8 sTypes[MAX_SPECIES + 1][2];
    static BOOL sLoaded[MAX_SPECIES + 1];
    *type1 = *type2 = 0;
    if (species == SPECIES_NONE || species > MAX_SPECIES) {
        return;
    }
    if (!sLoaded[species]) {
        u32 size = 0;
        u8 *file = NARC_PortReadMember(NARC_INDEX_POKETOOL__PERSONAL__PL_PERSONAL, species, &size);
        if (file != NULL && size >= sizeof(SpeciesData)) {
            const SpeciesData *data = (const SpeciesData *)file;
            sTypes[species][0] = data->types[0];
            sTypes[species][1] = data->types[1];
        }
        free(file);
        sLoaded[species] = TRUE;
    }
    *type1 = sTypes[species][0];
    *type2 = sTypes[species][1];
}

static int CompareSpeciesNames(const void *a, const void *b)
{
    return strcmp(Gear_SpeciesName(*(const u16 *)a), Gear_SpeciesName(*(const u16 *)b));
}

int Gear_SpeciesByName(u16 *out, int max)
{
    int count = 0;
    for (int species = 1; species <= NATIONAL_DEX_COUNT && count < max; species++) {
        out[count++] = (u16)species;
    }
    qsort(out, count, sizeof(u16), CompareSpeciesNames);
    return count;
}
