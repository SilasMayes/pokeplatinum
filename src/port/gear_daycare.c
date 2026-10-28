// The Gear's breeding helper. See port/gear_daycare.h
#include "port/gear_daycare.h"

#include <nitro.h>
#include <string.h>

#include "constants/daycare.h"
#include "constants/items.h"
#include "constants/species.h"
#include "generated/abilities.h"
#include "generated/egg_groups.h"
#include "generated/genders.h"
#include "generated/items.h"
#include "generated/moves.h"

#include "field/field_system.h"
#include "port/debug_field.h"
#include "port/cheats.h"
#include "port/gear_dex.h"

#include "daycare_save.h"
#include "item.h"
#include "party.h"
#include "pokemon.h"
#include "savedata.h"
#include "unk_020559DC.h"

// As the Day Care's own table (daycare.c): babies that need an incense held
static const u16 sIncenseBabies[][3] = {
    { SPECIES_WYNAUT, ITEM_LAX_INCENSE, SPECIES_WOBBUFFET },
    { SPECIES_AZURILL, ITEM_SEA_INCENSE, SPECIES_MARILL },
    { SPECIES_MIME_JR, ITEM_ODD_INCENSE, SPECIES_MR_MIME },
    { SPECIES_BONSLY, ITEM_ROCK_INCENSE, SPECIES_SUDOWOODO },
    { SPECIES_MUNCHLAX, ITEM_FULL_INCENSE, SPECIES_SNORLAX },
    { SPECIES_MANTYKE, ITEM_WAVE_INCENSE, SPECIES_MANTINE },
    { SPECIES_BUDEW, ITEM_ROSE_INCENSE, SPECIES_ROSELIA },
    { SPECIES_HAPPINY, ITEM_LUCK_INCENSE, SPECIES_CHANSEY },
    { SPECIES_CHINGLING, ITEM_PURE_INCENSE, SPECIES_CHIMECHO },
};

// The special dates with shorter egg cycles (daycare.c)
static const u16 sShortCycleDates[] = { 112, 214, 303, 401, 501, 611, 707, 821, 907, 928, 1121, 1214 };

static void ReadMon(BoxPokemon *boxMon, u32 steps, GearDaycareMon *out)
{
    memset(out, 0, sizeof(*out));
    BOOL decrypted = BoxPokemon_EnterDecryptionContext(boxMon);
    out->species = BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES, NULL);
    out->form = BoxPokemon_GetValue(boxMon, MON_DATA_FORM, NULL);
    out->item = BoxPokemon_GetValue(boxMon, MON_DATA_HELD_ITEM, NULL);
    out->personality = BoxPokemon_GetValue(boxMon, MON_DATA_PERSONALITY, NULL);
    out->shiny = Pokemon_IsPersonalityShiny(BoxPokemon_GetValue(boxMon, MON_DATA_OT_ID, NULL), out->personality);
    for (int i = 0; i < 6; i++) {
        out->ivs[i] = BoxPokemon_GetValue(boxMon, MON_DATA_HP_IV + i, NULL);
    }
    for (int i = 0; i < 4; i++) {
        out->moves[i] = BoxPokemon_GetValue(boxMon, MON_DATA_MOVE1 + i, NULL);
    }
    BoxPokemon_ExitDecryptionContext(boxMon, decrypted);
    out->gender = BoxPokemon_GetGender(boxMon);
    out->nature = Pokemon_GetNatureOf(out->personality);
    out->steps = steps;

    // The level it will come back at (the Day Care gives its steps as EXP)
    u32 level = BoxPokemon_GetLevel(boxMon);
    out->level = (u8)level;
    u32 exp = BoxPokemon_GetValue(boxMon, MON_DATA_EXPERIENCE, NULL) + steps;
    u32 newLevel = Pokemon_GetSpeciesLevelAt(out->species, exp);
    if (newLevel > 100) {
        newLevel = 100;
    }
    out->levelsGained = newLevel > level ? (u8)(newLevel - level) : 0;
}

// BoxMon_GetPairDaycareCompatibilityScore (daycare.c)
static int Compatibility(BoxPokemon *pair[2])
{
    u16 species[2], groups[2][2];
    u32 ids[2], genders[2];
    for (int i = 0; i < 2; i++) {
        species[i] = BoxPokemon_GetValue(pair[i], MON_DATA_SPECIES, NULL);
        ids[i] = BoxPokemon_GetValue(pair[i], MON_DATA_OT_ID, NULL);
        genders[i] = Pokemon_GetGenderOf(species[i], BoxPokemon_GetValue(pair[i], MON_DATA_PERSONALITY, NULL));
        groups[i][0] = SpeciesData_GetSpeciesValue(species[i], SPECIES_DATA_EGG_GROUP_1);
        groups[i][1] = SpeciesData_GetSpeciesValue(species[i], SPECIES_DATA_EGG_GROUP_2);
    }
    if (groups[0][0] == EGG_GROUP_UNDISCOVERED || groups[1][0] == EGG_GROUP_UNDISCOVERED) {
        return 0;
    }
    if (groups[0][0] == EGG_GROUP_DITTO && groups[1][0] == EGG_GROUP_DITTO) {
        return 0;
    }
    if (groups[0][0] == EGG_GROUP_DITTO || groups[1][0] == EGG_GROUP_DITTO) {
        return ids[0] == ids[1] ? PARENTS_LOW_COMPATIBILITY : PARENTS_MED_COMPATIBILITY;
    }
    if (genders[0] == genders[1] || genders[0] == GENDER_NONE || genders[1] == GENDER_NONE) {
        return 0;
    }
    BOOL shared = FALSE;
    for (int a = 0; a < 2; a++) {
        for (int b = 0; b < 2; b++) {
            shared |= groups[0][a] == groups[1][b];
        }
    }
    if (!shared) {
        return 0;
    }
    if (species[0] == species[1]) {
        return ids[0] != ids[1] ? PARENTS_MAX_COMPATIBILITY : PARENTS_MED_COMPATIBILITY;
    }
    return ids[0] != ids[1] ? PARENTS_MED_COMPATIBILITY : PARENTS_LOW_COMPATIBILITY;
}

static BOOL Knows(const u16 moves[4], u16 move)
{
    for (int i = 0; i < 4; i++) {
        if (moves[i] == move && move != MOVE_NONE) {
            return TRUE;
        }
    }
    return FALSE;
}

static void AddMove(GearDaycare *out, u16 move, u8 how)
{
    for (int i = 0; i < out->inheritedCount; i++) {
        if (out->inheritedMoves[i] == move) {
            return;
        }
    }
    if (out->inheritedCount < GEAR_DAYCARE_MAX_MOVES) {
        out->inheritedMoves[out->inheritedCount] = move;
        out->inheritedHow[out->inheritedCount] = how;
        out->inheritedCount++;
    }
}

// Egg_BuildMoveset (daycare.c): egg moves and TMs the father knows, level-up moves both
// parents know; Volt Tackle with a Light Ball
static void InheritedMoves(GearDaycare *out, const GearDaycareMon *father, const GearDaycareMon *mother)
{
    static GearSpecies egg;
    if (!Gear_GetSpecies(out->eggSpecies, &egg)) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        const u16 move = father->moves[i];
        if (move == MOVE_NONE) {
            continue;
        }
        for (int j = 0; j < egg.eggMoveCount; j++) {
            if (egg.eggMoves[j] == move) {
                AddMove(out, move, 0);
            }
        }
        for (int j = 0; j < egg.tmCount; j++) {
            if (Gear_TMMove(egg.tms[j]) == move) {
                AddMove(out, move, 1);
            }
        }
        if (Knows(mother->moves, move)) {
            for (int j = 0; j < egg.levelMoveCount; j++) {
                if (egg.levelMoves[j].move == move) {
                    AddMove(out, move, 2);
                }
            }
        }
    }
    if (out->eggSpecies == SPECIES_PICHU && (father->item == ITEM_LIGHT_BALL || mother->item == ITEM_LIGHT_BALL)) {
        AddMove(out, MOVE_VOLT_TACKLE, 3);
    }
}

BOOL Gear_GetDaycare(GearDaycare *out)
{
    memset(out, 0, sizeof(*out));
    out->everstoneSlot = -1;
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return FALSE;
    }
    SaveData *saveData = fieldSystem->saveData;
    Daycare *daycare = SaveData_GetDaycare(saveData);

    // The eggs in the party and their steps
    Party *party = SaveData_GetParty(saveData);
    out->eggCycleSteps = 255;
    if (fieldSystem->location != NULL) {
        const int date = FieldSystem_GetMonth(fieldSystem) * 100 + FieldSystem_GetDayOfMonth(fieldSystem);
        for (int i = 0; i < (int)NELEMS(sShortCycleDates); i++) {
            if (sShortCycleDates[i] == date) {
                out->eggCycleSteps = 230;
            }
        }
    }
    for (int i = 0; i < Party_GetCurrentCount(party); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        if (!Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            const int ability = Pokemon_GetValue(mon, MON_DATA_ABILITY, NULL);
            out->fasterHatching |= ability == ABILITY_FLAME_BODY || ability == ABILITY_MAGMA_ARMOR;
        }
    }
    if (Cheats_FastEggs()) {
        out->eggCycleSteps = 4; // as Daycare_GetEggCycleLength with the cheat
    }
    const int stepInCycle = Daycare_GetStepCounter(daycare);
    for (int i = 0; i < Party_GetCurrentCount(party) && out->partyEggCount < 6; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        if (!Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }
        GearPartyEgg *egg = &out->partyEggs[out->partyEggCount++];
        egg->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        int cycles = Cheats_FastEggs() ? 0 : Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);
        if (out->fasterHatching) {
            cycles = (cycles + 1) / 2;
        }
        // It hatches at the end of the cycle after the one that takes it to 0
        int steps = (cycles + 1) * out->eggCycleSteps - stepInCycle;
        egg->steps = steps > 0 ? steps : 1;
    }

    // The Day Care
    BoxPokemon *pair[2];
    for (int i = 0; i < 2; i++) {
        DaycareMon *daycareMon = Daycare_GetDaycareMon(daycare, i);
        pair[i] = DaycareMon_GetBoxMon(daycareMon);
        if (BoxPokemon_GetValue(pair[i], MON_DATA_SPECIES_EXISTS, NULL)) {
            ReadMon(pair[i], DaycareMon_GetSteps(daycareMon), &out->mons[out->count++]);
        }
    }
    out->eggWaiting = Daycare_HasEgg(daycare);
    if (out->count < 2) {
        return TRUE;
    }

    out->compatibility = Compatibility(pair);
    // Daycare_Update checks on the step that takes the second one's count to xxFF
    out->stepsToNextCheck = 0xFF - (int)(DaycareMon_GetSteps(Daycare_GetDaycareMon(daycare, 1)) & 0xFF);
    if (out->stepsToNextCheck == 0) {
        out->stepsToNextCheck = 256;
    }
    out->differentLanguages = Daycare_AreParentLanguagesDifferent(daycare);
    if (out->compatibility == 0) {
        return TRUE;
    }

    // Which parent the egg's species comes from (Egg_DetermineEggSpeciesAndParentSlots)
    int mother = 0;
    for (int i = 0; i < 2; i++) {
        if (out->mons[i].species == SPECIES_DITTO) {
            mother = i ^ 1;
        } else if (out->mons[i].gender == GENDER_FEMALE) {
            mother = i;
        }
    }
    out->motherSlot = mother;
    u16 species = Pokemon_GetBaseSpeciesFromPersonalData(out->mons[mother].species);
    if (species == SPECIES_MANAPHY) {
        species = SPECIES_PHIONE;
    }
    for (int i = 0; i < (int)NELEMS(sIncenseBabies); i++) {
        if (species == sIncenseBabies[i][0] && out->mons[0].item != sIncenseBabies[i][1]
            && out->mons[1].item != sIncenseBabies[i][1]) {
            species = sIncenseBabies[i][2];
        }
    }
    out->eggSpecies = species;

    // Everstone: the mother (or Ditto) holding one passes its nature half the time
    int natureSlot = -1;
    for (int i = 0; i < 2; i++) {
        if (out->mons[i].gender == GENDER_FEMALE) {
            natureSlot = i;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (out->mons[i].species == SPECIES_DITTO) {
            natureSlot = i;
        }
    }
    if (out->mons[0].species == SPECIES_DITTO && out->mons[1].species == SPECIES_DITTO) {
        natureSlot = -1;
    }
    if (natureSlot >= 0 && out->mons[natureSlot].item == ITEM_EVERSTONE) {
        out->everstoneSlot = natureSlot;
    }

    InheritedMoves(out, &out->mons[mother ^ 1], &out->mons[mother]);
    return TRUE;
}
