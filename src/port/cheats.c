// See include/port/cheats.h
#include "port/cheats.h"

#include <nitro.h>
#include <time.h>

#include "constants/battle.h"
#include "constants/species.h"

#include "port/rom_patches.h"
#include "port/sim_config_prj.h"

#include "party.h"
#include "pokemon.h"
#include "save_player.h"
#include "savedata.h"
#include "trainer_info.h"

static const SIM_Config_prj_type *Config(void)
{
    return SIM_Config_prj_GetConfig();
}

// The game's own odds: shiny when the shiny value is below the threshold (8 of 65536)
static u32 GameShinyOdds(void)
{
    const int threshold = RomPatches_Info()->shinyThreshold;
    return threshold > 0 ? 65536u / (u32)threshold : 8192u;
}

u32 Cheats_ShinyOdds(void)
{
    const u32 game = GameShinyOdds();
    const SIM_Config_prj_type *config = Config();
    if (config == NULL || config->shinyOdds <= 0 || (u32)config->shinyOdds >= game) {
        return game;
    }
    return (u32)config->shinyOdds;
}

BOOL Cheats_ShinyOddsBoosted(void)
{
    return Cheats_ShinyOdds() < GameShinyOdds();
}

u32 Cheats_ScaleExp(u32 exp)
{
    const SIM_Config_prj_type *config = Config();
    if (config == NULL || config->expPercent <= 0 || config->expPercent == 100) {
        return exp;
    }
    const u64 scaled = (u64)exp * (u32)config->expPercent / 100u;
    return scaled > 0xFFFFFFu ? 0xFFFFFFu : (u32)scaled;
}

BOOL Cheats_AlwaysCatch(void)
{
    return Config() != NULL && Config()->alwaysCatch;
}

BOOL Cheats_InfinitePP(void)
{
    return Config() != NULL && Config()->infinitePP;
}

BOOL Cheats_AlwaysEscape(void)
{
    return Config() != NULL && Config()->alwaysEscape;
}

BOOL Cheats_FastEggs(void)
{
    return Config() != NULL && Config()->fastEggs;
}

BOOL Cheats_AnyTM(void)
{
    return Config() != NULL && Config()->anyTM;
}

// --- shiny odds ----------------------------------------------------------------------------

// Our own random numbers: the game's RNG stays as it would have been
static u32 sRandom = 0;

static u32 Random(void)
{
    if (sRandom == 0) {
        sRandom = (u32)time(NULL) * 2654435761u | 1u;
    }
    // xorshift32
    sRandom ^= sRandom << 13;
    sRandom ^= sRandom >> 17;
    sRandom ^= sRandom << 5;
    return sRandom;
}

// A shiny personality as close to the old one as it gets: the low byte (gender, ability)
// stays, and so does the nature whenever it can (it nearly always can)
static u32 ShinyPersonality(u32 otID, u32 personality)
{
    const u32 trainerValue = (otID >> 16) ^ (otID & 0xFFFF);
    const u32 nature = personality % 25;
    const u32 low = personality & 0xFF;
    const u32 middle = (personality >> 8) & 0xFF;

    for (u32 i = 0; i < 256; i++) {
        const u32 lo = low | (((middle + i) & 0xFF) << 8);
        for (u32 x = 0; x < 8; x++) {
            const u32 hi = (trainerValue ^ lo ^ x) & 0xFFFF;
            const u32 candidate = (hi << 16) | lo;
            if (candidate % 25 == nature) {
                return candidate;
            }
        }
    }
    const u32 lo = personality & 0xFFFF;
    return (((trainerValue ^ lo) & 0xFFFF) << 16) | lo;
}

// Shiny at the boosted odds for the trainer otID: what's left after the game's own roll
static void MaybeMakeShiny(Pokemon *mon, u32 otID)
{
    if (!Cheats_ShinyOddsBoosted()
        || Pokemon_IsPersonalityShiny(otID, Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL))) {
        return;
    }
    const u32 odds = Cheats_ShinyOdds();
    const u32 game = GameShinyOdds();
    BOOL shiny;
    if (odds <= 1) {
        shiny = TRUE;
    } else {
        // P(shiny) = 1/odds overall, the game already gave 1/game: (1/odds - 1/game) / (1 - 1/game)
        const double wanted = (1.0 / odds - 1.0 / game) / (1.0 - 1.0 / game);
        shiny = (double)(Random() >> 8) / (double)(1u << 24) < wanted;
    }
    if (!shiny) {
        return;
    }

    const u32 personality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
    const u32 shinyPersonality = ShinyPersonality(otID, personality);
    if (Pokemon_IsPersonalityShiny(otID, shinyPersonality)) {
        Pokemon_PortSetPersonality(mon, shinyPersonality);
    }
}

void Cheats_OnBattleStart(u32 battleType, Party *wildParty)
{
    if (wildParty == NULL
        || (battleType & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_CATCH_TUTORIAL))) {
        return;
    }
    for (int i = 0; i < Party_GetCurrentCount(wildParty); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(wildParty, i);
        const u32 species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        if (species != SPECIES_NONE && species <= MAX_SPECIES) {
            MaybeMakeShiny(mon, Pokemon_GetValue(mon, MON_DATA_OT_ID, NULL));
        }
    }
}

void Cheats_OnEggHatched(Pokemon *mon)
{
    // The hatchling becomes the player's (egg_hatch.c), and that's whose shiny it is
    SaveData *saveData = SaveData_Ptr();
    if (mon != NULL && saveData != NULL) {
        MaybeMakeShiny(mon, TrainerInfo_ID(SaveData_GetTrainerInfo(saveData)));
    }
}
