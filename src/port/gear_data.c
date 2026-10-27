// Game data for the Gear companion. See include/port/gear_data.h
#include "port/gear_data.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/battle.h"
#include "constants/battle/condition.h"
#include "constants/charcode.h"
#include "constants/heap.h"
#include "constants/narc.h"
#include "constants/species.h"
#include "constants/string.h"
#include "constants/wild_encounters.h"
#include "generated/abilities.h"
#include "generated/badges.h"
#include "generated/battle_boot_states.h"
#include "generated/item_hold_effects.h"
#include "generated/move_attributes.h"
#include "generated/text_banks.h"
#include "generated/trainers.h"
#include "generated/time_of_day.h"

#include "battle/battle_context.h"
#include "battle/battle_display.h"
#include "battle/battle_lib.h"
#include "battle/battle_mon.h"
#include "battle/battle_subscreen.h"
#include "battle/battle_system.h"
#include "field/field_system.h"
#include "overlay006/wild_encounters.h"
#include "port/debug_field.h"

#include "charcode_convert.h"
#include "location.h"
#include "map_header.h"
#include "map_header_data.h"
#include "message.h"
#include "move_table.h"
#include "party.h"
#include "play_time.h"
#include "pokedex.h"
#include "pokeradar.h"
#include "pokemon.h"
#include "rtc.h"
#include "save_player.h"
#include "savedata.h"
#include "savedata_misc.h"
#include "string_gf.h"
#include "system.h"
#include "system_vars.h"
#include "trainer_data.h"
#include "trainer_info.h"

// Implemented in src/battle/battle_lib.c (port only)
int BattleLib_SingleTypeMultiplier(u8 attackType, u8 defenderType);
// Implemented in src/battle/battle_controller_player.c (port only)
int BattleController_PortChoosingBattler(BattleSystem *battleSys);
// Implemented in src/battle/battle_lib.c (port only)
u32 BattleLib_PortEffectiveSpeed(BattleSystem *battleSys, BattleContext *battleCtx, int battler);
BOOL BattleLib_PortEstimateDamage(BattleSystem *battleSys, BattleContext *battleCtx, int attacker, int defender, int move, int *minDamage, int *maxDamage, int *hits);

// --- names -------------------------------------------------------------------------

typedef struct NameCache {
    enum TextBank bank;
    int count;
    BOOL titleCase;
    BOOL loaded;
    char (*names)[GEAR_NAME_LEN];
} NameCache;

#define NAME_CACHE(var, textBank, size, titleCase) \
    static char var##Names[size][GEAR_NAME_LEN];    \
    static NameCache var = { textBank, size, titleCase, FALSE, var##Names }

NAME_CACHE(sSpeciesNames, TEXT_BANK_SPECIES_NAME, MAX_SPECIES + 1, TRUE);
NAME_CACHE(sMoveNames, TEXT_BANK_MOVE_NAMES, 512, FALSE);
NAME_CACHE(sItemNames, TEXT_BANK_ITEM_NAMES, 600, FALSE);
NAME_CACHE(sAbilityNames, TEXT_BANK_ABILITY_NAMES, 128, FALSE);
NAME_CACHE(sNatureNames, TEXT_BANK_NATURE_NAMES, 32, FALSE);
NAME_CACHE(sTypeNames, TEXT_BANK_POKEMON_TYPE_NAMES, 32, TRUE);
NAME_CACHE(sLocationNames, TEXT_BANK_LOCATION_NAMES, 160, FALSE);
NAME_CACHE(sTrainerNames, TEXT_BANK_NPC_TRAINER_NAMES, 960, FALSE);

static void TitleCase(char *name);

// Game text to ASCII, stopping at the end of the string
void Gear_TextToAscii(const u16 *chars, int maxLen, char *out, int outSize)
{
    int pos = 0;
    for (int i = 0; i < maxLen && chars[i] != CHAR_EOS && pos < outSize - 1; i++) {
        // Nidoran's symbols (the ASCII conversion drops them)
        if (chars[i] == CHAR_MALE || chars[i] == CHAR_WIDE_MALE || chars[i] == CHAR_FEMALE || chars[i] == CHAR_WIDE_FEMALE) {
            if (pos < outSize - 3) {
                out[pos++] = ' ';
                out[pos++] = (chars[i] == CHAR_MALE || chars[i] == CHAR_WIDE_MALE) ? 'M' : 'F';
            }
            continue;
        }
        char c = 0;
        CharCode_ToAsciiString((charcode_t *)&chars[i], &c, 1);
        if (c != 0) {
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
}

// The whole bank is read the first time one of its names is needed
static const char *CachedName(NameCache *cache, int id)
{
    if (id < 0 || id >= cache->count) {
        return "?";
    }
    if (!cache->loaded) {
        MessageLoader *loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG, cache->bank, HEAP_ID_SYSTEM);
        int count = MessageBank_NARCEntryCount(NARC_INDEX_MSGDATA__PL_MSG, cache->bank);
        for (int i = 0; i < cache->count; i++) {
            if (i >= count) {
                snprintf(cache->names[i], GEAR_NAME_LEN, "?");
                continue;
            }
            String *string = MessageLoader_GetNewString(loader, i);
            if (String_IsTrainerName(string)) {
                // Trainer names are packed (see String_ConcatTrainerName)
                String *unpacked = String_Init(64, HEAP_ID_SYSTEM);
                String_ConcatTrainerName(unpacked, string);
                String_Free(string);
                string = unpacked;
            }
            Gear_TextToAscii(String_GetData(string), String_Length(string), cache->names[i], GEAR_NAME_LEN);
            String_Free(string);
            if (cache->titleCase) {
                TitleCase(cache->names[i]);
            }
        }
        MessageLoader_Free(loader);
        cache->loaded = TRUE;
    }
    return cache->names[id];
}

// The game spells species in capitals ("BIDOOF"); the Gear uses "Bidoof", "Mr. Mime",
// "Ho-Oh", "Farfetch'd"
static void TitleCase(char *name)
{
    BOOL wordStart = TRUE;
    for (char *c = name; *c != '\0'; c++) {
        if (*c >= 'A' && *c <= 'Z') {
            if (!wordStart) {
                *c = *c - 'A' + 'a';
            }
            wordStart = FALSE;
        } else if (*c >= 'a' && *c <= 'z') {
            wordStart = FALSE;
        } else {
            wordStart = *c != '\'';
        }
    }
}

const char *Gear_SpeciesName(u16 species)
{
    return CachedName(&sSpeciesNames, species);
}

// A Pokemon's name: its nickname, or the (title-cased) species name if it has none
static void MonDisplayName(const charcode_t *nickname, BOOL hasNickname, u16 species, char *out)
{
    if (hasNickname) {
        Gear_TextToAscii(nickname, MON_NAME_LEN + 1, out, GEAR_NAME_LEN);
    } else {
        snprintf(out, GEAR_NAME_LEN, "%s", Gear_SpeciesName(species));
    }
}

const char *Gear_MoveName(u16 move)
{
    return CachedName(&sMoveNames, move);
}

const char *Gear_ItemName(u16 item)
{
    return CachedName(&sItemNames, item);
}

const char *Gear_AbilityName(u8 ability)
{
    return CachedName(&sAbilityNames, ability);
}

const char *Gear_NatureName(u8 nature)
{
    return CachedName(&sNatureNames, nature);
}

const char *Gear_TypeName(u8 type)
{
    return CachedName(&sTypeNames, type);
}

const char *Gear_LocationName(int mapHeaderID)
{
    return CachedName(&sLocationNames, MapHeader_GetMapLabelTextID(mapHeaderID));
}

const char *Gear_LocationLabelName(int labelTextID)
{
    return CachedName(&sLocationNames, labelTextID);
}

int Gear_CurrentLocationLabel(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->location == NULL) {
        return -1;
    }
    return MapHeader_GetMapLabelTextID(fieldSystem->location->mapHeaderID);
}

// --- moves ---------------------------------------------------------------------------

typedef struct CachedMove {
    u8 loaded;
    u8 type;
    u8 power;
    u8 accuracy;
    u8 category;
} CachedMove;

static CachedMove sMoves[512];

static const CachedMove *MoveData(u16 move)
{
    if (move >= NELEMS(sMoves)) {
        static const CachedMove none = { 1, 0, 0, 0, 2 };
        return &none;
    }
    CachedMove *entry = &sMoves[move];
    if (!entry->loaded) {
        entry->type = MoveTable_LoadParam(move, MOVEATTRIBUTE_TYPE);
        entry->power = MoveTable_LoadParam(move, MOVEATTRIBUTE_POWER);
        entry->accuracy = MoveTable_LoadParam(move, MOVEATTRIBUTE_ACCURACY);
        entry->category = MoveTable_LoadParam(move, MOVEATTRIBUTE_CLASS);
        entry->loaded = 1;
    }
    return entry;
}

// --- game state ----------------------------------------------------------------------

BOOL Gear_IsGameRunning(void)
{
    return DEBUG_GetFieldSystem() != NULL;
}

static SaveData *RunningSave(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    return fieldSystem != NULL ? fieldSystem->saveData : NULL;
}

BOOL Gear_SpeciesSeen(u16 species)
{
    SaveData *saveData = RunningSave();
    return saveData != NULL && Pokedex_HasSeenSpecies(SaveData_GetPokedex(saveData), species);
}

BOOL Gear_SpeciesCaught(u16 species)
{
    SaveData *saveData = RunningSave();
    return saveData != NULL && Pokedex_HasCaughtSpecies(SaveData_GetPokedex(saveData), species);
}

// A Pokemon (party or not) for the Gear
static void FillMon(Pokemon *mon, GearMon *gm)
{
    memset(gm, 0, sizeof(*gm));

    gm->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL);
    gm->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    gm->personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);

    charcode_t nickname[MON_NAME_LEN + 1];
    Pokemon_GetValue(mon, MON_DATA_NICKNAME, nickname);
    MonDisplayName(nickname, Pokemon_GetValue(mon, MON_DATA_HAS_NICKNAME, NULL), gm->species, gm->nickname);

    if (gm->isEgg) {
        return; // nothing else is meaningful yet
    }

    gm->shiny = Pokemon_IsShiny(mon);
    gm->level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
    gm->gender = Pokemon_GetGender(mon);
    gm->hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    gm->maxHp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    gm->status = Pokemon_GetValue(mon, MON_DATA_STATUS, NULL);
    gm->type1 = Pokemon_GetValue(mon, MON_DATA_TYPE_1, NULL);
    gm->type2 = Pokemon_GetValue(mon, MON_DATA_TYPE_2, NULL);
    gm->heldItem = Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
    gm->ability = Pokemon_GetValue(mon, MON_DATA_ABILITY, NULL);
    gm->nature = Pokemon_GetNature(mon);
    gm->exp = Pokemon_GetValue(mon, MON_DATA_EXPERIENCE, NULL);
    gm->expLevelStart = Pokemon_GetCurrentLevelBaseExp(mon);
    gm->expNextLevel = gm->level < 100 ? gm->exp + Pokemon_GetExpToNextLevel(mon) : gm->exp;
    gm->friendship = Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);

    static const enum PokemonDataParam statParams[6] = { MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SP_ATK, MON_DATA_SP_DEF, MON_DATA_SPEED };
    static const enum PokemonDataParam ivParams[6] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
    static const enum PokemonDataParam evParams[6] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };
    for (int s = 0; s < 6; s++) {
        gm->stats[s] = Pokemon_GetValue(mon, statParams[s], NULL);
        gm->ivs[s] = Pokemon_GetValue(mon, ivParams[s], NULL);
        gm->evs[s] = Pokemon_GetValue(mon, evParams[s], NULL);
    }

    for (int m = 0; m < 4; m++) {
        GearMove *move = &gm->moves[m];
        move->id = Pokemon_GetValue(mon, MON_DATA_MOVE1 + m, NULL);
        if (move->id == 0) {
            continue;
        }
        move->pp = Pokemon_GetValue(mon, MON_DATA_MOVE1_PP + m, NULL);
        move->maxPp = Pokemon_GetValue(mon, MON_DATA_MOVE1_MAX_PP + m, NULL);
        const CachedMove *data = MoveData(move->id);
        move->type = data->type;
        move->power = data->power;
        move->accuracy = data->accuracy;
        move->category = data->category;
    }
}

int Gear_GetParty(GearMon out[GEAR_PARTY_MAX])
{
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return 0;
    }

    Party *party = SaveData_GetParty(saveData);
    int count = Party_GetCurrentCount(party);
    if (count > GEAR_PARTY_MAX) {
        count = GEAR_PARTY_MAX;
    }

    for (int i = 0; i < count; i++) {
        FillMon(Party_GetPokemonBySlotIndex(party, i), &out[i]);
    }
    return count;
}

// --- bosses --------------------------------------------------------------------------

// Implemented in src/trainer_data.c (port only)
void Trainer_PortBuildParty(int trainerID, Party *party, enum HeapID heapID);

typedef struct BossDef {
    u16 trainerIDs[3]; // by the player's starter (Turtwig, Chimchar, Piplup); one ID for most
    u8 kind;
    u8 stage;
    const char *title;
    const char *place;
    s8 badge; // gym leaders: their badge (BADGE_ID_*; Platinum's gym order isn't the badges')
} BossDef;

#define ONE(id) { id, id, id }

// Platinum's story order; stage: badges in hand when the battle comes
static const BossDef sBossDefs[] = {
    { { TRAINER_RIVAL_ROUTE_201_TURTWIG, TRAINER_RIVAL_ROUTE_201_CHIMCHAR, TRAINER_RIVAL_ROUTE_201_PIPLUP }, GEAR_BOSS_RIVAL, 0, "Rival", "Route 201", -1 },
    { { TRAINER_RIVAL_ROUTE_203_TURTWIG, TRAINER_RIVAL_ROUTE_203_CHIMCHAR, TRAINER_RIVAL_ROUTE_203_PIPLUP }, GEAR_BOSS_RIVAL, 0, "Rival", "Route 203", -1 },
    { ONE(TRAINER_LEADER_ROARK), GEAR_BOSS_GYM, 0, "Leader", "Oreburgh City", BADGE_ID_COAL },
    { ONE(TRAINER_COMMANDER_MARS_VALLEY_WINDWORKS), GEAR_BOSS_GALACTIC, 1, "Commander", "Valley Windworks", -1 },
    { ONE(TRAINER_LEADER_GARDENIA), GEAR_BOSS_GYM, 1, "Leader", "Eterna City", BADGE_ID_FOREST },
    { ONE(TRAINER_COMMANDER_JUPITER_TEAM_GALACTIC_ETERNA_BUILDING), GEAR_BOSS_GALACTIC, 2, "Commander", "Galactic Eterna Building", -1 },
    { { TRAINER_RIVAL_ROUTE_209_TURTWIG, TRAINER_RIVAL_ROUTE_209_CHIMCHAR, TRAINER_RIVAL_ROUTE_209_PIPLUP }, GEAR_BOSS_RIVAL, 2, "Rival", "Hearthome City gate", -1 },
    { ONE(TRAINER_LEADER_FANTINA), GEAR_BOSS_GYM, 2, "Leader", "Hearthome City", BADGE_ID_RELIC },
    { ONE(TRAINER_GALACTIC_BOSS_CYRUS_CELESTIC_TOWN_RUINS), GEAR_BOSS_GALACTIC, 3, "Boss", "Celestic Town", -1 },
    { ONE(TRAINER_LEADER_MAYLENE), GEAR_BOSS_GYM, 3, "Leader", "Veilstone City", BADGE_ID_COBBLE },
    { { TRAINER_RIVAL_PASTORIA_CITY_TURTWIG, TRAINER_RIVAL_PASTORIA_CITY_CHIMCHAR, TRAINER_RIVAL_PASTORIA_CITY_PIPLUP }, GEAR_BOSS_RIVAL, 4, "Rival", "Pastoria City", -1 },
    { ONE(TRAINER_LEADER_WAKE), GEAR_BOSS_GYM, 4, "Leader", "Pastoria City", BADGE_ID_FEN },
    { { TRAINER_RIVAL_CANALAVE_CITY_TURTWIG, TRAINER_RIVAL_CANALAVE_CITY_CHIMCHAR, TRAINER_RIVAL_CANALAVE_CITY_PIPLUP }, GEAR_BOSS_RIVAL, 5, "Rival", "Canalave City", -1 },
    { ONE(TRAINER_LEADER_BYRON), GEAR_BOSS_GYM, 5, "Leader", "Canalave City", BADGE_ID_MINE },
    { ONE(TRAINER_COMMANDER_SATURN_VALOR_CAVERN), GEAR_BOSS_GALACTIC, 6, "Commander", "Lake Valor", -1 },
    { ONE(TRAINER_COMMANDER_MARS_LAKE_VERITY), GEAR_BOSS_GALACTIC, 6, "Commander", "Lake Verity", -1 },
    { ONE(TRAINER_LEADER_CANDICE), GEAR_BOSS_GYM, 6, "Leader", "Snowpoint City", BADGE_ID_ICICLE },
    { ONE(TRAINER_COMMANDER_SATURN_GALACTIC_HQ), GEAR_BOSS_GALACTIC, 7, "Commander", "Galactic HQ", -1 },
    { ONE(TRAINER_GALACTIC_BOSS_CYRUS_GALACTIC_HQ), GEAR_BOSS_GALACTIC, 7, "Boss", "Galactic HQ", -1 },
    { ONE(TRAINER_COMMANDER_MARS_SPEAR_PILLAR), GEAR_BOSS_GALACTIC, 7, "Commander", "Spear Pillar (double)", -1 },
    { ONE(TRAINER_COMMANDER_JUPITER_SPEAR_PILLAR), GEAR_BOSS_GALACTIC, 7, "Commander", "Spear Pillar (double)", -1 },
    { ONE(TRAINER_GALACTIC_BOSS_CYRUS_DISTORTION_WORLD), GEAR_BOSS_GALACTIC, 7, "Boss", "Distortion World", -1 },
    { ONE(TRAINER_LEADER_VOLKNER), GEAR_BOSS_GYM, 7, "Leader", "Sunyshore City", BADGE_ID_BEACON },
    { { TRAINER_RIVAL_POKEMON_LEAGUE_TURTWIG, TRAINER_RIVAL_POKEMON_LEAGUE_CHIMCHAR, TRAINER_RIVAL_POKEMON_LEAGUE_PIPLUP }, GEAR_BOSS_RIVAL, 8, "Rival", "Pokemon League", -1 },
    { ONE(TRAINER_ELITE_FOUR_AARON), GEAR_BOSS_ELITE_FOUR, 8, "Elite Four", "Pokemon League", -1 },
    { ONE(TRAINER_ELITE_FOUR_BERTHA), GEAR_BOSS_ELITE_FOUR, 8, "Elite Four", "Pokemon League", -1 },
    { ONE(TRAINER_ELITE_FOUR_FLINT), GEAR_BOSS_ELITE_FOUR, 8, "Elite Four", "Pokemon League", -1 },
    { ONE(TRAINER_ELITE_FOUR_LUCIAN), GEAR_BOSS_ELITE_FOUR, 8, "Elite Four", "Pokemon League", -1 },
    { ONE(TRAINER_CHAMPION_CYNTHIA), GEAR_BOSS_CHAMPION, 8, "Champion", "Pokemon League", -1 },
};

static GearBoss sBosses[NELEMS(sBossDefs)];
static BOOL sBossBuilt[NELEMS(sBossDefs)];

int Gear_BossCount(void)
{
    return NELEMS(sBossDefs);
}

static int StarterIndex(SaveData *saveData)
{
    switch (SystemVars_GetPlayerStarter(SaveData_GetVarsFlags(saveData))) {
    case SPECIES_CHIMCHAR:
        return 1;
    case SPECIES_PIPLUP:
        return 2;
    default:
        return 0;
    }
}

static void BuildBoss(int index, u16 trainerID, SaveData *saveData)
{
    const BossDef *def = &sBossDefs[index];
    GearBoss *boss = &sBosses[index];
    memset(boss, 0, sizeof(*boss));
    boss->trainerID = trainerID;
    boss->kind = def->kind;
    boss->stage = def->stage;
    snprintf(boss->title, sizeof(boss->title), "%s", def->title);
    snprintf(boss->place, sizeof(boss->place), "%s", def->place);
    if (def->kind == GEAR_BOSS_RIVAL) {
        Gear_TextToAscii(MiscSaveBlock_RivalName(SaveData_MiscSaveBlockConst(saveData)), TRAINER_NAME_LEN + 1, boss->name,
            GEAR_NAME_LEN);
    } else {
        snprintf(boss->name, sizeof(boss->name), "%s", CachedName(&sTrainerNames, trainerID));
    }
    boss->doubles = Trainer_LoadParam(trainerID, TRDATA_BATTLE_TYPE) != BATTLE_TYPE_SINGLES;

    Party *party = Party_New(HEAP_ID_SYSTEM);
    Trainer_PortBuildParty(trainerID, party, HEAP_ID_SYSTEM);
    boss->partyCount = Party_GetCurrentCount(party);
    if (boss->partyCount > GEAR_PARTY_MAX) {
        boss->partyCount = GEAR_PARTY_MAX;
    }
    for (int i = 0; i < boss->partyCount; i++) {
        FillMon(Party_GetPokemonBySlotIndex(party, i), &boss->party[i]);
        if (boss->party[i].level >= boss->aceLevel) {
            boss->aceLevel = boss->party[i].level;
            boss->aceSpecies = boss->party[i].species;
        }
    }
    Heap_Free(party);
}

const GearBoss *Gear_Boss(int index)
{
    SaveData *saveData = RunningSave();
    if (saveData == NULL || index < 0 || index >= (int)NELEMS(sBossDefs)) {
        return NULL;
    }

    const u16 trainerID = sBossDefs[index].trainerIDs[StarterIndex(saveData)];
    if (!sBossBuilt[index] || sBosses[index].trainerID != trainerID) {
        BuildBoss(index, trainerID, saveData);
        sBossBuilt[index] = TRUE;
    }

    // Gym leaders are beaten once their badge is in
    GearBoss *boss = &sBosses[index];
    boss->beaten = FALSE;
    if (sBossDefs[index].badge >= 0) {
        boss->beaten = TrainerInfo_HasBadge(SaveData_GetTrainerInfo(saveData), sBossDefs[index].badge);
    }
    return boss;
}

int Gear_NextGymBoss(void)
{
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return -1;
    }

    // The first gym whose badge is missing, in Platinum's order
    const TrainerInfo *info = SaveData_GetTrainerInfo(saveData);
    for (int i = 0; i < (int)NELEMS(sBossDefs); i++) {
        if (sBossDefs[i].badge >= 0 && !TrainerInfo_HasBadge(info, sBossDefs[i].badge)) {
            return i;
        }
    }
    for (int i = 0; i < (int)NELEMS(sBossDefs); i++) {
        if (sBossDefs[i].kind == GEAR_BOSS_CHAMPION) {
            return i;
        }
    }
    return -1;
}

int Gear_LevelCap(void)
{
    const GearBoss *boss = Gear_Boss(Gear_NextGymBoss());
    return boss != NULL ? boss->aceLevel : 0;
}

void Gear_GetTrainer(GearTrainer *out)
{
    memset(out, 0, sizeof(*out));
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return;
    }

    TrainerInfo *info = SaveData_GetTrainerInfo(saveData);
    Gear_TextToAscii(TrainerInfo_Name(info), TRAINER_NAME_LEN + 1, out->name, GEAR_NAME_LEN);
    out->gender = TrainerInfo_Gender(info);
    out->id = TrainerInfo_ID(info) & 0xFFFF;
    out->money = TrainerInfo_Money(info);
    for (int i = 0; i < 8; i++) {
        if (TrainerInfo_HasBadge(info, i)) {
            out->badgeMask |= 1 << i;
            out->badgeCount++;
        }
    }

    PlayTime *playTime = SaveData_GetPlayTime(saveData);
    out->hours = PlayTime_GetHours(playTime);
    out->minutes = PlayTime_GetMinutes(playTime);

    Pokedex *pokedex = SaveData_GetPokedex(saveData);
    out->pokedexObtained = Pokedex_IsObtained(pokedex);
    out->nationalDex = Pokedex_IsNationalDexObtained(pokedex);
    out->seenLocal = Pokedex_CountSeen_Local(pokedex);
    out->caughtLocal = Pokedex_CountCaught_Local(pokedex);
    out->seenNational = Pokedex_CountSeen_National(pokedex);
    out->caughtNational = Pokedex_CountCaught_National(pokedex);
}

// --- wild encounters -------------------------------------------------------------------

static const u8 sGrassRates[MAX_GRASS_ENCOUNTERS] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
static const u8 sWaterRates[MAX_WATER_ENCOUNTERS] = { 60, 30, 5, 4, 1 };
static const u8 sRodRates[MAX_WATER_ENCOUNTERS] = { 40, 40, 15, 4, 1 }; // Good and Super Rod

// Adds an encounter, merging it with the same species from the same method
static void AddEncounter(GearArea *area, u8 method, u16 species, u8 minLevel, u8 maxLevel, u8 rate)
{
    if (species == SPECIES_NONE) {
        return;
    }
    for (int i = 0; i < area->count; i++) {
        GearEncounter *entry = &area->entries[i];
        if (entry->method == method && entry->species == species) {
            entry->rate += rate;
            entry->minLevel = minLevel < entry->minLevel ? minLevel : entry->minLevel;
            entry->maxLevel = maxLevel > entry->maxLevel ? maxLevel : entry->maxLevel;
            return;
        }
    }
    if (area->count < GEAR_AREA_MAX_ENTRIES) {
        GearEncounter *entry = &area->entries[area->count++];
        entry->method = method;
        entry->species = species;
        entry->minLevel = minLevel;
        entry->maxLevel = maxLevel;
        entry->rate = rate;
    }
}

static void AddWater(GearArea *area, u8 method, const WaterEncounters *water, const u8 *rates)
{
    if (water->encounterRate == 0) {
        return;
    }
    for (int i = 0; i < MAX_WATER_ENCOUNTERS; i++) {
        const WaterEncounter *enc = &water->encounters[i];
        AddEncounter(area, method, enc->species, enc->minLevel, enc->maxLevel, rates[i]);
    }
}

static int CompareEncounters(const void *a, const void *b)
{
    const GearEncounter *ea = a;
    const GearEncounter *eb = b;
    if (ea->method != eb->method) {
        return ea->method - eb->method;
    }
    return eb->rate - ea->rate;
}

void Gear_GetArea(GearArea *out)
{
    memset(out, 0, sizeof(*out));
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->location == NULL || fieldSystem->mapHeaderData == NULL) {
        return;
    }

    out->valid = TRUE;
    out->mapHeaderID = fieldSystem->location->mapHeaderID;
    snprintf(out->name, sizeof(out->name), "%s", Gear_LocationName(out->mapHeaderID));
    out->timeOfDay = GetTimeOfDay();
    if (fieldSystem->chain != NULL && GetRadarChainActive(fieldSystem->chain)) {
        out->radarChain = GetChainCount(fieldSystem);
    }

    if (!MapHeader_HasWildEncounters(out->mapHeaderID)) {
        return;
    }
    const WildEncounters *data = MapHeaderData_GetWildEncounters(fieldSystem);

    if (data->grassEncounters.encounterRate != 0) {
        for (int i = 0; i < MAX_GRASS_ENCOUNTERS; i++) {
            const GrassEncounter *enc = &data->grassEncounters.encounters[i];
            int species = enc->species;

            // Slots 3 and 4 follow the time of day, like WildEncounters_ReplaceTimedEncounters
            if (i == 2 || i == 3) {
                if (out->timeOfDay == TIMEOFDAY_DAY || out->timeOfDay == TIMEOFDAY_TWILIGHT) {
                    species = data->dayEncounters[i - 2];
                } else if (out->timeOfDay == TIMEOFDAY_NIGHT || out->timeOfDay == TIMEOFDAY_LATE_NIGHT) {
                    species = data->nightEncounters[i - 2];
                }
            }
            AddEncounter(out, GEAR_ENC_GRASS, species, enc->level, enc->level, sGrassRates[i]);
        }

        // Poke Radar and swarms replace pairs of 10% / 20% slots; their levels follow
        // the slots they replace
        for (int i = 0; i < MAX_RADAR_ENCOUNTERS; i++) {
            const GrassEncounter *slot = &data->grassEncounters.encounters[4 + (i & 1)];
            AddEncounter(out, GEAR_ENC_RADAR, data->radarEncounters[i], slot->level, slot->level, 0);
        }
        for (int i = 0; i < MAX_SWARM_ENCOUNTERS; i++) {
            const GrassEncounter *slot = &data->grassEncounters.encounters[i];
            AddEncounter(out, GEAR_ENC_SWARM, data->swarmEncounters[i], slot->level, slot->level, 0);
        }
    }

    AddWater(out, GEAR_ENC_SURF, &data->surfEncounters, sWaterRates);
    AddWater(out, GEAR_ENC_OLD_ROD, &data->oldRodEncounters, sWaterRates);
    AddWater(out, GEAR_ENC_GOOD_ROD, &data->goodRodEncounters, sRodRates);
    AddWater(out, GEAR_ENC_SUPER_ROD, &data->superRodEncounters, sRodRates);

    // Radar/swarm species already in the normal table add nothing new
    for (int i = 0; i < out->count; i++) {
        GearEncounter *special = &out->entries[i];
        if (special->method != GEAR_ENC_RADAR && special->method != GEAR_ENC_SWARM) {
            continue;
        }
        for (int j = 0; j < out->count; j++) {
            if (out->entries[j].method == GEAR_ENC_GRASS && out->entries[j].species == special->species) {
                special->species = SPECIES_NONE;
                break;
            }
        }
    }
    int kept = 0;
    for (int i = 0; i < out->count; i++) {
        if (out->entries[i].species != SPECIES_NONE) {
            out->entries[kept++] = out->entries[i];
        }
    }
    out->count = kept;

    out->hasEncounters = out->count > 0;
    qsort(out->entries, out->count, sizeof(GearEncounter), CompareEncounters);
}

// --- types -------------------------------------------------------------------------------

int Gear_TypeEffectiveness(u8 attackType, u8 defenderType1, u8 defenderType2)
{
    int multiplier = BattleLib_SingleTypeMultiplier(attackType, defenderType1);
    if (defenderType2 != defenderType1) {
        multiplier = multiplier * BattleLib_SingleTypeMultiplier(attackType, defenderType2) / 10;
    }
    return multiplier;
}

// --- battles -----------------------------------------------------------------------------

static BattleSystem *sBattleSys = NULL;

void Gear_SetBattleSystem(struct BattleSystem *battleSys)
{
    sBattleSys = battleSys;
}

static void AddBattlePartyMons(GearBattle *out, Party *party)
{
    if (party == NULL) {
        return;
    }
    int count = Party_GetCurrentCount(party);
    for (int i = 0; i < count && out->enemyPartyCount < (int)NELEMS(out->enemyParty); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        u16 species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        if (species == SPECIES_NONE) {
            continue;
        }
        GearBattleMon *entry = &out->enemyParty[out->enemyPartyCount++];
        entry->species = species;
        entry->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL);
        entry->level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
        entry->fainted = Pokemon_GetValue(mon, MON_DATA_HP, NULL) == 0;
    }
}

// --- the battle menu -------------------------------------------------------------------

static u32 sDsScreenTick = 0; // gSystem.vblankCounter when the battle last showed a DS-only screen
static BOOL sDsScreenNoted = FALSE;

void Gear_NoteBattleDsScreen(void)
{
    sDsScreenTick = gSystem.vblankCounter;
    sDsScreenNoted = TRUE;
}

BOOL Gear_BattleNeedsDsScreen(void)
{
    if (sBattleSys == NULL) {
        return FALSE;
    }
    if (BattleSystem_GetBattleType(sBattleSys) & BATTLE_TYPE_CATCH_TUTORIAL) {
        return TRUE;
    }
    // A little longer than the screens themselves, so the layout doesn't flip back and
    // forth while they open and close
    return sDsScreenNoted && gSystem.vblankCounter - sDsScreenTick < 20;
}

static void FillMove(GearMove *out, u16 id, int pp, int maxPp)
{
    memset(out, 0, sizeof(*out));
    out->id = id;
    if (id == 0) {
        return;
    }
    const CachedMove *data = MoveData(id);
    out->type = data->type;
    out->power = data->power;
    out->accuracy = data->accuracy;
    out->category = data->category;
    out->pp = pp;
    out->maxPp = maxPp;
}

BOOL Gear_GetBattleMenu(GearBattleMenu *out)
{
    memset(out, 0, sizeof(*out));
    out->cursor = -1;
    out->battler = -1;
    for (int i = 0; i < 4; i++) {
        out->targetBattler[i] = -1;
    }
    if (sBattleSys == NULL) {
        return FALSE;
    }

    BattleSubscreenPortMenu menu;
    if (!BattleSubscreen_PortGetMenu(&menu) || !menu.polled) {
        return FALSE;
    }

    const u32 battleType = BattleSystem_GetBattleType(sBattleSys);
    u8 battlersByType[6];
    BattleSystem_SetBattlersByType(sBattleSys, battlersByType);

    switch (menu.menuConfig) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 8:
        out->kind = GEAR_MENU_ACTION;
        out->battler = menu.data.actionData.battler;
        out->safari = (battleType & (BATTLE_TYPE_SAFARI | BATTLE_TYPE_PAL_PARK)) != 0;
        out->cancel = menu.hasCancelButton;
        break;
    case 5:
    case 6:
        out->kind = GEAR_MENU_FIRST;
        out->battler = menu.data.actionData.battler;
        break;
    case 9:
    case 10:
        out->kind = GEAR_MENU_PAL_PARK;
        break;
    case 11:
        out->kind = GEAR_MENU_MOVES;
        if (menu.data.moveData.battlerType < 6) {
            out->battler = battlersByType[menu.data.moveData.battlerType];
        }
        for (int m = 0; m < 4; m++) {
            FillMove(&out->moves[m], menu.data.moveData.moveIDs[m], menu.data.moveData.currPP[m],
                menu.data.moveData.maxPP[m]);
        }
        break;
    case 12:
        out->kind = GEAR_MENU_TARGETS;
        if (menu.data.targetSelectData.battlerType < 6) {
            out->battler = battlersByType[menu.data.targetSelectData.battlerType];
        }
        for (int i = 0; i < 4; i++) {
            out->targetOk[i] = menu.targetOk[i];
            out->targetBattler[i] = menu.targetBattler[i];
        }
        break;
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        out->kind = GEAR_MENU_YES_NO;
        switch (menu.menuConfig) {
        case 14:
            snprintf(out->yes, sizeof(out->yes), "Forget a move");
            snprintf(out->no, sizeof(out->no), "Keep the old moves");
            break;
        case 15:
            snprintf(out->yes, sizeof(out->yes), "Give up on %s", Gear_MoveName(menu.data.giveUpMoveData.move));
            snprintf(out->no, sizeof(out->no), "Don't give up");
            break;
        case 16:
            snprintf(out->yes, sizeof(out->yes), "Use next Pokemon");
            snprintf(out->no, sizeof(out->no), "Run away");
            break;
        case 17:
            snprintf(out->yes, sizeof(out->yes), "Switch Pokemon");
            snprintf(out->no, sizeof(out->no), "Keep battling");
            break;
        default:
            snprintf(out->yes, sizeof(out->yes), "Yes");
            snprintf(out->no, sizeof(out->no), "No");
            break;
        }
        break;
    case 18:
        out->kind = GEAR_MENU_STOP;
        break;
    default:
        return FALSE;
    }

    out->ready = menu.ready;
    out->cursor = menu.cursorButton;
    return TRUE;
}

void Gear_PressBattleButton(int button)
{
    BattleSubscreen_PortPress(button);
}

BOOL Gear_GetBattle(GearBattle *out)
{
    memset(out, 0, sizeof(*out));
    out->choosing = -1;
    if (sBattleSys == NULL) {
        return FALSE;
    }

    BattleContext *battleCtx = BattleSystem_GetBattleContext(sBattleSys);
    u32 battleType = BattleSystem_GetBattleType(sBattleSys);
    int maxBattlers = BattleSystem_GetMaxBattlers(sBattleSys);
    if (battleCtx == NULL) {
        return FALSE;
    }
    if (maxBattlers > GEAR_BATTLERS_MAX) {
        maxBattlers = GEAR_BATTLERS_MAX;
    }

    out->active = TRUE;
    out->trainer = (battleType & BATTLE_TYPE_TRAINER) != 0;
    out->doubles = (battleType & BATTLE_TYPE_DOUBLES) != 0;
    out->choosing = BattleController_PortChoosingBattler(sBattleSys);

    for (int i = 0; i < maxBattlers; i++) {
        const BattleMon *mon = &battleCtx->battleMons[i];
        GearBattler *gb = &out->battlers[i];
        if (mon->species == SPECIES_NONE || mon->isEgg) {
            continue;
        }

        gb->present = TRUE;
        gb->enemy = BattleSystem_GetBattlerSide(sBattleSys, i) != BATTLE_SIDE_PLAYER;
        gb->playerControlled = BattlerData_GetBootState(BattleSystem_GetBattlerData(sBattleSys, i)) == BATTLER_BOOT_STATE_NORMAL;
        gb->species = mon->species;
        gb->personality = mon->personality;
        MonDisplayName(mon->nickname, mon->hasNickname, mon->species, gb->nickname);
        gb->level = mon->level;
        gb->gender = mon->gender;
        gb->shiny = mon->isShiny;
        gb->hp = mon->curHP > 0 ? mon->curHP : 0;
        gb->maxHp = mon->maxHP;
        gb->status = mon->status;
        gb->confused = (mon->statusVolatile & VOLATILE_CONDITION_CONFUSION) != 0;
        gb->type1 = mon->type1;
        gb->type2 = mon->type2;
        gb->ability = mon->ability;
        gb->heldItem = mon->heldItem;
        for (int s = 0; s < NUM_BOOSTABLE_STATS && s < (int)NELEMS(gb->statStages); s++) {
            gb->statStages[s] = mon->statBoosts[s] - 6;
        }
        for (int m = 0; m < LEARNED_MOVES_MAX; m++) {
            GearMove *move = &gb->moves[m];
            move->id = mon->moves[m];
            if (move->id == 0) {
                continue;
            }
            const CachedMove *data = MoveData(move->id);
            move->type = data->type;
            move->power = data->power;
            move->accuracy = data->accuracy;
            move->category = data->category;
            move->pp = mon->ppCur[m];
            move->maxPp = MoveTable_CalcMaxPP(move->id, mon->ppUps[m]);
        }
    }

    // Turn order and damage
    out->trickRoom = (battleCtx->fieldConditionsMask & FIELD_CONDITION_TRICK_ROOM) != 0;
    for (int i = 0; i < maxBattlers; i++) {
        GearBattler *gb = &out->battlers[i];
        for (int m = 0; m < 4; m++) {
            for (int t = 0; t < GEAR_BATTLERS_MAX; t++) {
                gb->damageMin[m][t] = gb->damageMax[m][t] = -1;
            }
        }
        if (!gb->present || gb->hp == 0) {
            continue;
        }
        gb->speed = BattleLib_PortEffectiveSpeed(sBattleSys, battleCtx, i);
        gb->movesLast = Battler_HeldItemEffect(battleCtx, i) == HOLD_EFFECT_PRIORITY_DOWN
            ? 2
            : (Battler_Ability(battleCtx, i) == ABILITY_STALL ? 1 : 0);
        for (int m = 0; m < 4; m++) {
            gb->moveHits[m] = 1;
            if (gb->moves[m].id == 0) {
                continue;
            }
            gb->movePriority[m] = (s8)MoveTable_LoadParam(gb->moves[m].id, MOVEATTRIBUTE_PRIORTY);
            for (int t = 0; t < maxBattlers; t++) {
                const GearBattler *target = &out->battlers[t];
                if (t == i || !target->present || target->hp == 0 || target->enemy == gb->enemy) {
                    continue;
                }
                int low, high, hits;
                if (BattleLib_PortEstimateDamage(sBattleSys, battleCtx, i, t, gb->moves[m].id, &low, &high, &hits)) {
                    gb->damageMin[m][t] = (s16)(low > 32767 ? 32767 : low);
                    gb->damageMax[m][t] = (s16)(high > 32767 ? 32767 : high);
                    gb->moveHits[m] = (u8)hits;
                }
            }
        }
    }

    // The opposing trainers' whole teams
    if (out->trainer) {
        Party *first = BattleSystem_GetParty(sBattleSys, BATTLER_ENEMY_1);
        AddBattlePartyMons(out, first);
        if (maxBattlers > 2) {
            Party *second = BattleSystem_GetParty(sBattleSys, BATTLER_ENEMY_2);
            if (second != first) {
                AddBattlePartyMons(out, second);
            }
        }
    }
    return TRUE;
}
