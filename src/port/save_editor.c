// The save editor. See include/port/save_editor.h
#include "port/save_editor.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants/heap.h"
#include "constants/items.h"
#include "constants/savedata/vars_flags.h"
#include "constants/species.h"
#include "generated/abilities.h"
#include "generated/battle_terrains.h"
#include "generated/distribution_events.h"
#include "generated/evolution_methods.h"
#include "generated/game_records.h"
#include "generated/gender_ratios.h"
#include "generated/items.h"
#include "generated/move_attributes.h"

#include "field/field_system.h"
#include "port/debug_field.h"
#include "port/gear_data.h"
#include "port/save_slots.h"
#include "savedata/save_table.h"

#include "bag.h"
#include "field_task.h"
#include "game_records.h"
#include "heap.h"
#include "item.h"
#include "location.h"
#include "map_header.h"
#include "move_table.h"
#include "party.h"
#include "pc_boxes.h"
#include "play_time.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save_catchrecords.h"
#include "save_player.h"
#include "savedata.h"
#include "string_gf.h"
#include "system_flags.h"
#include "system_vars.h"
#include "trainer_info.h"
#include "vars_flags.h"
#include "wifi_battle_tower_save.h"

// --- the save ------------------------------------------------------------------------

const char *SaveEd_Unavailable(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return "Load your save first: continue your game from the title screen.";
    }
    // Battles and the game's menus (party, bag, PC...) work on copies of what's edited here
    if (!FieldSystem_IsRunningFieldMap(fieldSystem)) {
        return "Close the game's menu or finish the battle first: the editor works while you're walking around.";
    }
    return NULL;
}

static SaveData *EditableSave(void)
{
    return SaveEd_Unavailable() == NULL ? DEBUG_GetFieldSystem()->saveData : NULL;
}

// Random numbers of our own (the game's generator stays as it was)
static u32 Random(void)
{
    static u32 state = 0;
    if (state == 0) {
        state = (u32)time(NULL) ^ (u32)(uintptr_t)&state ^ 0x9E3779B9u;
        if (state == 0) {
            state = 1;
        }
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// --- Pokemon ---------------------------------------------------------------------------

static const enum PokemonDataParam sIvParams[SAVE_ED_STATS] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
static const enum PokemonDataParam sEvParams[SAVE_ED_STATS] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };
static const enum PokemonDataParam sStatParams[SAVE_ED_STATS] = { MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SP_ATK, MON_DATA_SP_DEF, MON_DATA_SPEED };

// A Pokemon being read or changed. PC Pokemon are worked on as a party Pokemon (which
// has its level and stats) and written back.
typedef struct MonAccess {
    Pokemon *mon;
    BoxPokemon *boxSlot;
    Pokemon temp;
} MonAccess;

static Pokemon *OpenMon(SaveEdRef ref, MonAccess *access)
{
    SaveData *saveData = EditableSave();
    access->mon = NULL;
    access->boxSlot = NULL;
    if (saveData == NULL) {
        return NULL;
    }

    if (ref.box == SAVE_ED_PARTY) {
        Party *party = SaveData_GetParty(saveData);
        if (ref.slot < 0 || ref.slot >= Party_GetCurrentCount(party)) {
            return NULL;
        }
        access->mon = Party_GetPokemonBySlotIndex(party, ref.slot);
        return access->mon;
    }

    if (ref.box < 0 || ref.box >= SAVE_ED_BOXES || ref.slot < 0 || ref.slot >= SAVE_ED_BOX_SLOTS) {
        return NULL;
    }
    BoxPokemon *boxMon = PCBoxes_GetBoxMonAt(SaveData_GetPCBoxes(saveData), ref.box, ref.slot);
    if (!BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES_EXISTS, NULL)) {
        return NULL;
    }
    memset(&access->temp, 0, sizeof(access->temp));
    Pokemon_FromBoxPokemon(boxMon, &access->temp);
    access->mon = &access->temp;
    access->boxSlot = boxMon;
    return access->mon;
}

static BOOL CloseMon(MonAccess *access, BOOL changed)
{
    if (changed && access->boxSlot != NULL) {
        *access->boxSlot = *Pokemon_GetBoxPokemon(&access->temp);
        SaveData_SetFullSaveRequired(); // the PC is saved apart from the rest
    }
    return changed;
}

int SaveEd_PartyCount(void)
{
    SaveData *saveData = EditableSave();
    return saveData != NULL ? Party_GetCurrentCount(SaveData_GetParty(saveData)) : 0;
}

int SaveEd_BoxCount(int box)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES) {
        return 0;
    }
    return PCBoxes_CountMonsInBox(SaveData_GetPCBoxes(saveData), box);
}

void SaveEd_BoxName(int box, char *out, int size)
{
    SaveData *saveData = EditableSave();
    snprintf(out, size, "Box %d", box + 1);
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES) {
        return;
    }
    String *name = String_Init(32, HEAP_ID_SYSTEM);
    PCBoxes_BufferBoxName(SaveData_GetPCBoxes(saveData), box, name);
    char ascii[SAVE_ED_NAME_LEN];
    Gear_TextToAscii(String_GetData(name), String_Length(name), ascii, sizeof(ascii));
    String_Free(name);
    if (ascii[0] != '\0') {
        snprintf(out, size, "%s", ascii);
    }
}

BOOL SaveEd_GetBoxSlot(int box, int slot, SaveEdSlot *out)
{
    memset(out, 0, sizeof(*out));
    SaveData *saveData = EditableSave();
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES || slot < 0 || slot >= SAVE_ED_BOX_SLOTS) {
        return FALSE;
    }
    BoxPokemon *boxMon = PCBoxes_GetBoxMonAt(SaveData_GetPCBoxes(saveData), box, slot);
    if (!BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES_EXISTS, NULL)) {
        return FALSE;
    }
    out->species = BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES, NULL);
    out->isEgg = BoxPokemon_GetValue(boxMon, MON_DATA_IS_EGG, NULL);
    out->level = BoxPokemon_GetLevel(boxMon);
    out->shiny = Pokemon_IsPersonalityShiny(BoxPokemon_GetValue(boxMon, MON_DATA_OT_ID, NULL), BoxPokemon_GetValue(boxMon, MON_DATA_PERSONALITY, NULL));
    return TRUE;
}

static void Abilities(u16 species, int form, u8 out[2])
{
    out[0] = SpeciesData_GetFormValue(species, form, SPECIES_DATA_ABILITY_1);
    out[1] = SpeciesData_GetFormValue(species, form, SPECIES_DATA_ABILITY_2);
    if (out[1] == out[0]) {
        out[1] = ABILITY_NONE;
    }
}

static BOOL GenderFixed(u16 species)
{
    int ratio = SpeciesData_GetSpeciesValue(species, SPECIES_DATA_GENDER_RATIO);
    return ratio == GENDER_RATIO_MALE_ONLY || ratio == GENDER_RATIO_FEMALE_ONLY || ratio == GENDER_RATIO_NO_GENDER;
}

BOOL SaveEd_GetMon(SaveEdRef ref, SaveEdMon *out)
{
    memset(out, 0, sizeof(*out));
    MonAccess access;
    Pokemon *mon = OpenMon(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }

    out->exists = TRUE;
    out->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL);
    out->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    out->form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    if (out->isEgg) {
        snprintf(out->name, sizeof(out->name), "Egg");
    } else if (Pokemon_GetValue(mon, MON_DATA_HAS_NICKNAME, NULL)) {
        charcode_t nickname[MON_NAME_LEN + 1];
        Pokemon_GetValue(mon, MON_DATA_NICKNAME, nickname);
        Gear_TextToAscii(nickname, MON_NAME_LEN + 1, out->name, sizeof(out->name));
    } else {
        snprintf(out->name, sizeof(out->name), "%s", Gear_SpeciesName(out->species));
    }

    out->level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
    out->nature = Pokemon_GetNature(mon);
    out->gender = Pokemon_GetGender(mon);
    out->genderFixed = GenderFixed(out->species);
    out->shiny = Pokemon_IsShiny(mon);
    out->ability = Pokemon_GetValue(mon, MON_DATA_ABILITY, NULL);
    Abilities(out->species, out->form, out->abilities);
    out->item = Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
    out->friendship = Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);
    out->ball = Pokemon_GetValue(mon, MON_DATA_POKEBALL, NULL);
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        out->ivs[s] = Pokemon_GetValue(mon, sIvParams[s], NULL);
        out->evs[s] = Pokemon_GetValue(mon, sEvParams[s], NULL);
        out->stats[s] = Pokemon_GetValue(mon, sStatParams[s], NULL);
    }
    for (int m = 0; m < 4; m++) {
        out->moves[m] = Pokemon_GetValue(mon, MON_DATA_MOVE1 + m, NULL);
        out->pp[m] = Pokemon_GetValue(mon, MON_DATA_MOVE1_PP + m, NULL);
        out->maxPp[m] = Pokemon_GetValue(mon, MON_DATA_MOVE1_MAX_PP + m, NULL);
    }
    out->hp = Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    out->maxHp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    out->status = Pokemon_GetValue(mon, MON_DATA_STATUS, NULL);
    out->type1 = Pokemon_GetValue(mon, MON_DATA_TYPE_1, NULL);
    out->type2 = Pokemon_GetValue(mon, MON_DATA_TYPE_2, NULL);
    CloseMon(&access, FALSE);
    return TRUE;
}

// An editable Pokemon (not an egg)
static Pokemon *OpenEditable(SaveEdRef ref, MonAccess *access)
{
    Pokemon *mon = OpenMon(ref, access);
    if (mon != NULL && Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
        return NULL;
    }
    return mon;
}

BOOL SaveEd_SetLevel(SaveEdRef ref, int level)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    level = level < 1 ? 1 : (level > SAVE_ED_MAX_LEVEL ? SAVE_ED_MAX_LEVEL : level);
    if (level == (int)Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL)) {
        return FALSE;
    }
    u32 exp = Pokemon_GetSpeciesBaseExpAt(Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL), level);
    Pokemon_SetValue(mon, MON_DATA_EXPERIENCE, &exp);
    Pokemon_CalcLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

// A personality value with the nature (-1: any), gender (-1: any), shininess and ability
// slot (-1: any) asked for, for a Pokemon of this trainer
static BOOL FindPersonality(u16 species, int form, u32 otID, int nature, int gender, BOOL shiny, int abilitySlot, u32 *out)
{
    u8 abilities[2];
    Abilities(species, form, abilities);
    if (abilities[1] == ABILITY_NONE) {
        abilitySlot = -1;
    }
    if (GenderFixed(species)) {
        gender = -1;
    }
    const u32 trainerXor = (otID >> 16) ^ (otID & 0xFFFF);

    for (int i = 0; i < 1000000; i++) {
        u32 r = Random();
        u32 low = r & 0xFFFF;
        u32 high;
        if (shiny) {
            high = (trainerXor ^ low ^ (Random() & 7)) & 0xFFFF;
        } else {
            high = r >> 16;
            if ((trainerXor ^ low ^ high) < 8) {
                continue;
            }
        }
        u32 personality = (high << 16) | low;
        if (nature >= 0 && (int)(personality % 25) != nature) {
            continue;
        }
        if (gender >= 0 && Pokemon_GetGenderOf(species, personality) != gender) {
            continue;
        }
        if (abilitySlot >= 0 && (int)(personality & 1) != abilitySlot) {
            continue;
        }
        *out = personality;
        return TRUE;
    }
    return FALSE;
}

BOOL SaveEd_SetPersonality(SaveEdRef ref, int nature, int gender, BOOL shiny, int abilitySlot)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u16 species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    int form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    u32 otID = Pokemon_GetValue(mon, MON_DATA_OT_ID, NULL);
    u32 personality;
    if (!FindPersonality(species, form, otID, nature, gender, shiny, abilitySlot, &personality)) {
        return FALSE;
    }

    sub_020780C4(mon, personality); // moves the data to the new personality's order
    u8 unused = 0;
    Pokemon_SetValue(mon, MON_DATA_GENDER, &unused); // worked out from the personality
    Pokemon_CalcAbility(mon);
    Pokemon_CalcLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetIV(SaveEdRef ref, enum SaveEdStat stat, int value)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || stat < 0 || stat >= SAVE_ED_STATS) {
        return FALSE;
    }
    u8 iv = value < 0 ? 0 : (value > SAVE_ED_MAX_IV ? SAVE_ED_MAX_IV : value);
    Pokemon_SetValue(mon, sIvParams[stat], &iv);
    Pokemon_CalcLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetEV(SaveEdRef ref, enum SaveEdStat stat, int value)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || stat < 0 || stat >= SAVE_ED_STATS) {
        return FALSE;
    }
    int others = 0;
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        if (s != (int)stat) {
            others += Pokemon_GetValue(mon, sEvParams[s], NULL);
        }
    }
    int max = SAVE_ED_MAX_EVS - others;
    max = max > SAVE_ED_MAX_EV ? SAVE_ED_MAX_EV : (max < 0 ? 0 : max);
    u8 ev = value < 0 ? 0 : (value > max ? max : value);
    Pokemon_SetValue(mon, sEvParams[stat], &ev);
    Pokemon_CalcLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetMove(SaveEdRef ref, int slot, u16 move)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || slot < 0 || slot >= 4 || move >= MAX_MOVES) {
        return FALSE;
    }
    u16 moves[4];
    int count = 0;
    for (int m = 0; m < 4; m++) {
        moves[m] = Pokemon_GetValue(mon, MON_DATA_MOVE1 + m, NULL);
        count += moves[m] != 0;
    }

    if (move != 0) {
        for (int m = 0; m < 4; m++) {
            if (moves[m] == move) {
                return FALSE; // it knows it already
            }
        }
        if (moves[slot] == 0) {
            slot = count; // no gaps
        }
        Pokemon_ResetMoveSlot(mon, move, slot);
        return CloseMon(&access, TRUE);
    }

    // Removing: the moves after it move up
    if (moves[slot] == 0 || count <= 1) {
        return FALSE;
    }
    for (int m = slot; m < 3; m++) {
        Pokemon_SwapMoveSlots(mon, m, m + 1);
    }
    u16 none = 0;
    u8 zero = 0;
    Pokemon_SetValue(mon, MON_DATA_MOVE4, &none);
    Pokemon_SetValue(mon, MON_DATA_MOVE4_PP, &zero);
    Pokemon_SetValue(mon, MON_DATA_MOVE4_PP_UPS, &zero);
    return CloseMon(&access, TRUE);
}

// Forms that come from the held item (Giratina's Griseous Orb, Arceus' plates)
static void UpdateItemForm(Pokemon *mon)
{
    if (Pokemon_SetGiratinaFormByHeldItem(mon) >= 0) {
        Pokemon_CalcAbility(mon);
    }
    Pokemon_SetArceusForm(mon);
    Pokemon_CalcLevelAndStats(mon);
}

BOOL SaveEd_SetItem(SaveEdRef ref, u16 item)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || item >= MAX_ITEMS) {
        return FALSE;
    }
    Pokemon_SetValue(mon, MON_DATA_HELD_ITEM, &item);
    UpdateItemForm(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetFriendship(SaveEdRef ref, int value)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u8 friendship = value < 0 ? 0 : (value > 255 ? 255 : value);
    Pokemon_SetValue(mon, MON_DATA_FRIENDSHIP, &friendship);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_Heal(SaveEdRef ref)
{
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u16 hp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    u32 status = 0;
    Pokemon_SetValue(mon, MON_DATA_HP, &hp);
    Pokemon_SetValue(mon, MON_DATA_STATUS, &status);
    BoxPokemon_RestorePP(Pokemon_GetBoxPokemon(mon));
    return CloseMon(&access, TRUE);
}

static int CompareMoveNames(const void *a, const void *b)
{
    return strcmp(Gear_MoveName(*(const u16 *)a), Gear_MoveName(*(const u16 *)b));
}

int SaveEd_LearnableMoves(u16 species, int form, u16 *out, int max)
{
    int count = 0;
    u16 levelMoves[32];
    int levelCount = Pokemon_LoadLevelUpMoveIdsOf(species, form, levelMoves);

    for (int i = 0; i < levelCount + 100 && count < max; i++) {
        u16 move;
        if (i < levelCount) {
            move = levelMoves[i];
        } else {
            int tm = i - levelCount; // TM01..TM92, HM01..HM08
            if (!CanPokemonFormLearnTM(species, form, tm)) {
                continue;
            }
            move = Item_MoveForTMHM(ITEM_TM01 + tm);
        }
        if (move == 0 || move >= MAX_MOVES) {
            continue;
        }
        BOOL known = FALSE;
        for (int j = 0; j < count && !known; j++) {
            known = out[j] == move;
        }
        if (!known) {
            out[count++] = move;
        }
    }
    qsort(out, count, sizeof(u16), CompareMoveNames);
    return count;
}

int SaveEd_MoveType(u16 move)
{
    return MoveTable_LoadParam(move, MOVEATTRIBUTE_TYPE);
}

int SaveEd_MovePower(u16 move)
{
    return MoveTable_LoadParam(move, MOVEATTRIBUTE_POWER);
}

int SaveEd_MovePP(u16 move)
{
    return MoveTable_LoadParam(move, MOVEATTRIBUTE_PP);
}

void SaveEd_NatureStats(int nature, int *raised, int *lowered)
{
    // Natures go through Attack, Defense, Speed, Sp. Atk, Sp. Def
    static const int stats[5] = { SAVE_ED_ATK, SAVE_ED_DEF, SAVE_ED_SPEED, SAVE_ED_SPATK, SAVE_ED_SPDEF };
    nature = nature < 0 ? 0 : nature % 25;
    *raised = stats[nature / 5];
    *lowered = stats[nature % 5];
}

int SaveEd_GiveMon(const SaveEdNewMon *newMon, BOOL toParty, int *box, int *slot)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL || newMon->species < 1 || newMon->species > SAVE_ED_MAX_SPECIES) {
        return 0;
    }

    Party *party = SaveData_GetParty(saveData);
    PCBoxes *pcBoxes = SaveData_GetPCBoxes(saveData);
    const BOOL partyRoom = Party_GetCurrentCount(party) < MAX_PARTY_SIZE;
    int boxID = USE_CURRENT_BOX, boxSlot = 0;
    const BOOL boxRoom = PCBoxes_TryGetNextAvailableSpace(pcBoxes, &boxID, &boxSlot);
    toParty = toParty && partyRoom;
    if (!toParty && !boxRoom) {
        return 0;
    }

    TrainerInfo *trainerInfo = SaveData_GetTrainerInfo(saveData);
    const u32 otID = TrainerInfo_ID(trainerInfo);
    u32 personality;
    if (!FindPersonality(newMon->species, 0, otID, newMon->nature, -1, newMon->shiny, -1, &personality)) {
        return 0;
    }
    int level = newMon->level < 1 ? 1 : (newMon->level > SAVE_ED_MAX_LEVEL ? SAVE_ED_MAX_LEVEL : newMon->level);

    Pokemon *mon = Pokemon_New(HEAP_ID_SYSTEM);
    Pokemon_Init(mon);
    Pokemon_InitWith(mon, newMon->species, level, INIT_IVS_RANDOM, TRUE, personality, OTID_SET, otID);
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    int metLocation = MapHeader_GetMapLabelTextID(fieldSystem->location->mapHeaderID);
    Pokemon_SetCatchData(mon, trainerInfo, ITEM_POKE_BALL, metLocation, TERRAIN_MAX, HEAP_ID_SYSTEM);
    if (newMon->maxIVs) {
        u8 iv = SAVE_ED_MAX_IV;
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            Pokemon_SetValue(mon, sIvParams[s], &iv);
        }
    }
    u16 item = newMon->item < MAX_ITEMS ? newMon->item : ITEM_NONE;
    Pokemon_SetValue(mon, MON_DATA_HELD_ITEM, &item);
    UpdateItemForm(mon);

    int result;
    if (toParty) {
        Party_AddPokemon(party, mon);
        result = 1;
    } else {
        PCBoxes_TryStoreBoxMonAt(pcBoxes, boxID, boxSlot, Pokemon_GetBoxPokemon(mon));
        if (box != NULL) {
            *box = boxID;
        }
        if (slot != NULL) {
            *slot = boxSlot;
        }
        result = 2;
    }
    SaveData_UpdateCatchRecords(saveData, mon); // the Pokedex
    Heap_Free(mon);
    return result;
}

// --- .pk4 files ---------------------------------------------------------------------------

// Implemented in src/pokemon.c (port only)
void BoxPokemon_PortToPk4(const BoxPokemon *boxMon, u8 *out);
BOOL BoxPokemon_PortFromPk4(const u8 *pk4, BoxPokemon *out);

BOOL SaveEd_ExportPk4(SaveEdRef ref, u8 *out)
{
    MonAccess access;
    Pokemon *mon = OpenMon(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    BoxPokemon_PortToPk4(Pokemon_GetBoxPokemon(mon), out);
    CloseMon(&access, FALSE);
    return TRUE;
}

int SaveEd_ImportPk4(const u8 *data, int size, BOOL toParty, int *box, int *slot)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return 0;
    }
    // 136 bytes stored, 236 with the party stats (which are worked out again)
    if (size != SAVE_ED_PK4_SIZE && size != 236) {
        return -1;
    }

    BoxPokemon boxMon;
    if (!BoxPokemon_PortFromPk4(data, &boxMon)) {
        return -1;
    }
    Pokemon *mon = Pokemon_New(HEAP_ID_SYSTEM);
    Pokemon_FromBoxPokemon(&boxMon, mon);
    const u32 species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    if (species < 1 || species > SAVE_ED_MAX_SPECIES) {
        Heap_Free(mon);
        return -1;
    }
    u32 hp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    Pokemon_SetValue(mon, MON_DATA_HP, &hp);

    Party *party = SaveData_GetParty(saveData);
    PCBoxes *pcBoxes = SaveData_GetPCBoxes(saveData);
    int boxID = USE_CURRENT_BOX, boxSlot = 0;
    int result = 0;
    if (toParty && Party_GetCurrentCount(party) < MAX_PARTY_SIZE) {
        Party_AddPokemon(party, mon);
        result = 1;
    } else if (PCBoxes_TryGetNextAvailableSpace(pcBoxes, &boxID, &boxSlot)) {
        PCBoxes_TryStoreBoxMonAt(pcBoxes, boxID, boxSlot, Pokemon_GetBoxPokemon(mon));
        SaveData_SetFullSaveRequired();
        if (box != NULL) {
            *box = boxID;
        }
        if (slot != NULL) {
            *slot = boxSlot;
        }
        result = 2;
    }
    if (result != 0 && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
        SaveData_UpdateCatchRecords(saveData, mon); // the Pokedex
    }
    Heap_Free(mon);
    return result;
}

// --- Trade box -------------------------------------------------------------------------

#define TRADE_BOX_PATH "trade_box.bin"
#define TRADE_BOX_MAGIC 0x31425450 // "PTB1"

typedef struct TradeBoxEntry {
    u8 fromSlot;
    u8 reserved[3];
    u8 pk4[SAVE_ED_PK4_SIZE];
} TradeBoxEntry;

typedef struct TradeBox {
    int count;
    TradeBoxEntry entries[SAVE_ED_TRADE_BOX_MAX];
} TradeBox;

static void LoadTradeBox(TradeBox *box)
{
    memset(box, 0, sizeof(*box));
    FILE *file = fopen(TRADE_BOX_PATH, "rb");
    if (file == NULL) {
        return;
    }
    u32 header[2] = { 0, 0 };
    if (fread(header, sizeof(header), 1, file) == 1 && header[0] == TRADE_BOX_MAGIC) {
        int count = header[1] > SAVE_ED_TRADE_BOX_MAX ? SAVE_ED_TRADE_BOX_MAX : (int)header[1];
        box->count = (int)fread(box->entries, sizeof(TradeBoxEntry), count, file);
    }
    fclose(file);
}

static BOOL StoreTradeBox(const TradeBox *box)
{
    // Written to a new file first, so a failed write can't lose the box
    FILE *file = fopen(TRADE_BOX_PATH ".tmp", "wb");
    if (file == NULL) {
        return FALSE;
    }
    const u32 header[2] = { TRADE_BOX_MAGIC, (u32)box->count };
    BOOL ok = fwrite(header, sizeof(header), 1, file) == 1
        && (box->count == 0 || fwrite(box->entries, sizeof(TradeBoxEntry), box->count, file) == (size_t)box->count);
    ok = fclose(file) == 0 && ok;
    if (ok) {
        remove(TRADE_BOX_PATH);
        ok = rename(TRADE_BOX_PATH ".tmp", TRADE_BOX_PATH) == 0;
    }
    return ok;
}

// What trading this Pokemon would make it, 0 for nothing (as the game's trades check)
static u16 TradeEvolution(Pokemon *mon, int *evoType)
{
    if (Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
        return 0;
    }
    return Pokemon_GetEvolutionTargetSpecies(NULL, mon, EVO_CLASS_BY_TRADE, 0, evoType);
}

int SaveEd_TradeBox(SaveEdTradeEntry *out, int max)
{
    TradeBox *box = Heap_Alloc(HEAP_ID_SYSTEM, sizeof(TradeBox));
    LoadTradeBox(box);
    Pokemon *mon = Pokemon_New(HEAP_ID_SYSTEM);
    int count = 0;
    for (int i = 0; i < box->count && count < max; i++) {
        BoxPokemon boxMon;
        SaveEdTradeEntry *entry = &out[count];
        memset(entry, 0, sizeof(*entry));
        if (!BoxPokemon_PortFromPk4(box->entries[i].pk4, &boxMon)) {
            snprintf(entry->name, sizeof(entry->name), "?");
            count++;
            continue;
        }
        Pokemon_FromBoxPokemon(&boxMon, mon);
        entry->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
        entry->level = Pokemon_GetValue(mon, MON_DATA_LEVEL, NULL);
        entry->isEgg = Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL);
        entry->shiny = Pokemon_IsShiny(mon);
        entry->fromSlot = box->entries[i].fromSlot;
        entry->heldItem = Pokemon_GetValue(mon, MON_DATA_HELD_ITEM, NULL);
        charcode_t text[MON_NAME_LEN + 1];
        Pokemon_GetValue(mon, MON_DATA_NICKNAME, text);
        Gear_TextToAscii(text, MON_NAME_LEN + 1, entry->name, sizeof(entry->name));
        if (!Pokemon_GetValue(mon, MON_DATA_HAS_NICKNAME, NULL)) {
            snprintf(entry->name, sizeof(entry->name), "%s", Gear_SpeciesName(entry->species));
        }
        charcode_t trainer[TRAINER_NAME_LEN + 1];
        Pokemon_GetValue(mon, MON_DATA_OT_NAME, trainer);
        Gear_TextToAscii(trainer, TRAINER_NAME_LEN + 1, entry->trainer, sizeof(entry->trainer));
        int evoType;
        entry->evolvesInto = TradeEvolution(mon, &evoType);
        count++;
    }
    Heap_Free(mon);
    Heap_Free(box);
    return count;
}

const char *SaveEd_SendToTradeBox(SaveEdRef ref)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return "The save can't be changed right now.";
    }
    TradeBox *box = Heap_Alloc(HEAP_ID_SYSTEM, sizeof(TradeBox));
    LoadTradeBox(box);
    const char *error = NULL;
    MonAccess access;
    Pokemon *mon = OpenMon(ref, &access);

    if (box->count >= SAVE_ED_TRADE_BOX_MAX) {
        error = "The trade box is full.";
    } else if (mon == NULL) {
        error = "There's no Pokemon there.";
    } else if (ref.box == SAVE_ED_PARTY) {
        // The party keeps a Pokemon that can battle
        Party *party = SaveData_GetParty(saveData);
        int others = 0;
        for (int i = 0; i < Party_GetCurrentCount(party); i++) {
            Pokemon *other = Party_GetPokemonBySlotIndex(party, i);
            if (i != ref.slot && !Pokemon_GetValue(other, MON_DATA_IS_EGG, NULL) && Pokemon_GetValue(other, MON_DATA_HP, NULL) > 0) {
                others++;
            }
        }
        if (others == 0) {
            error = "That's the last Pokemon in your party that can battle.";
        }
    }

    if (error == NULL) {
        TradeBoxEntry *entry = &box->entries[box->count];
        memset(entry, 0, sizeof(*entry));
        entry->fromSlot = (u8)SaveSlots_Current();
        BoxPokemon_PortToPk4(Pokemon_GetBoxPokemon(mon), entry->pk4);
        box->count++;
        if (!StoreTradeBox(box)) {
            error = "The trade box could not be written.";
        }
    }
    CloseMon(&access, FALSE);

    if (error == NULL) {
        if (ref.box == SAVE_ED_PARTY) {
            Party_RemovePokemonBySlotIndex(SaveData_GetParty(saveData), ref.slot);
        } else {
            PCBoxes_InitBoxMonAt(SaveData_GetPCBoxes(saveData), ref.box, ref.slot);
            SaveData_SetFullSaveRequired();
        }
    }
    Heap_Free(box);
    return error;
}

// After a trade: what the evolution scene does (src/evolution.c), without the scene
static void EvolveTraded(SaveData *saveData, Pokemon *mon, u16 target, int evoType)
{
    u32 species = target;
    Pokemon_SetValue(mon, MON_DATA_SPECIES, &species);
    Pokemon_CalcAbility(mon);
    Pokemon_CalcLevelAndStats(mon);
    Pokedex_Capture(SaveData_GetPokedex(saveData), mon);
    GameRecords_IncrementRecordValue(SaveData_GetGameRecords(saveData), RECORD_POKEMON_EVOLVED);
    if (!Pokemon_GetValue(mon, MON_DATA_HAS_NICKNAME, NULL)) {
        Pokemon_SetValue(mon, MON_DATA_SPECIES_NAME, NULL);
    }
    if (evoType == EVO_TRADE_WITH_HELD_ITEM) {
        u32 none = ITEM_NONE;
        Pokemon_SetValue(mon, MON_DATA_HELD_ITEM, &none);
    }
}

int SaveEd_TakeFromTradeBox(int index, int *boxID, int *boxSlot, u16 *evolvedInto)
{
    *evolvedInto = 0;
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return 0;
    }
    TradeBox *box = Heap_Alloc(HEAP_ID_SYSTEM, sizeof(TradeBox));
    LoadTradeBox(box);
    if (index < 0 || index >= box->count) {
        Heap_Free(box);
        return -1;
    }

    BoxPokemon boxMon;
    if (!BoxPokemon_PortFromPk4(box->entries[index].pk4, &boxMon)) {
        Heap_Free(box);
        return -1;
    }
    Pokemon *mon = Pokemon_New(HEAP_ID_SYSTEM);
    Pokemon_FromBoxPokemon(&boxMon, mon);

    // Taken by another slot than the one that sent it: a trade
    if (box->entries[index].fromSlot != SaveSlots_Current()) {
        int evoType = 0;
        const u16 target = TradeEvolution(mon, &evoType);
        if (target != 0) {
            EvolveTraded(saveData, mon, target, evoType);
            *evolvedInto = target;
        }
    }
    u32 hp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
    Pokemon_SetValue(mon, MON_DATA_HP, &hp);

    Party *party = SaveData_GetParty(saveData);
    PCBoxes *pcBoxes = SaveData_GetPCBoxes(saveData);
    int pcBox = USE_CURRENT_BOX, pcSlot = 0;
    int result = 0;
    if (Party_GetCurrentCount(party) < MAX_PARTY_SIZE) {
        result = 1;
    } else if (PCBoxes_TryGetNextAvailableSpace(pcBoxes, &pcBox, &pcSlot)) {
        result = 2;
    }

    if (result != 0) {
        // Out of the box first: if that can't be written, nothing changes
        TradeBoxEntry taken = box->entries[index];
        memmove(&box->entries[index], &box->entries[index + 1], sizeof(TradeBoxEntry) * (box->count - index - 1));
        box->count--;
        if (!StoreTradeBox(box)) {
            result = 0;
            (void)taken;
        } else if (result == 1) {
            Party_AddPokemon(party, mon);
        } else {
            PCBoxes_TryStoreBoxMonAt(pcBoxes, pcBox, pcSlot, Pokemon_GetBoxPokemon(mon));
            SaveData_SetFullSaveRequired();
            *boxID = pcBox;
            *boxSlot = pcSlot;
        }
        if (result != 0 && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            SaveData_UpdateCatchRecords(saveData, mon); // the Pokedex
        }
    }
    Heap_Free(mon);
    Heap_Free(box);
    return result;
}

// --- Bag -------------------------------------------------------------------------------

const char *SaveEd_PocketName(int pocket)
{
    static const char *const names[SAVE_ED_POCKETS] = {
        [POCKET_ITEMS] = "Items",
        [POCKET_MEDICINE] = "Medicine",
        [POCKET_BALLS] = "Poke Balls",
        [POCKET_TMHMS] = "TMs & HMs",
        [POCKET_BERRIES] = "Berries",
        [POCKET_MAIL] = "Mail",
        [POCKET_BATTLE_ITEMS] = "Battle Items",
        [POCKET_KEY_ITEMS] = "Key Items",
    };
    return pocket >= 0 && pocket < SAVE_ED_POCKETS ? names[pocket] : "?";
}

static BagItem *Pocket(Bag *bag, int pocket, int *size)
{
    switch (pocket) {
    case POCKET_ITEMS:
        *size = ITEM_POCKET_SIZE;
        return bag->items;
    case POCKET_MEDICINE:
        *size = MEDICINE_POCKET_SIZE;
        return bag->medicine;
    case POCKET_BALLS:
        *size = POKEBALL_POCKET_SIZE;
        return bag->pokeballs;
    case POCKET_TMHMS:
        *size = TMHM_POCKET_SIZE;
        return bag->tmHms;
    case POCKET_BERRIES:
        *size = BERRY_POCKET_SIZE;
        return bag->berries;
    case POCKET_MAIL:
        *size = MAIL_POCKET_SIZE;
        return bag->mail;
    case POCKET_BATTLE_ITEMS:
        *size = BATTLE_ITEM_POCKET_SIZE;
        return bag->battleItems;
    case POCKET_KEY_ITEMS:
        *size = KEY_ITEM_POCKET_SIZE;
        return bag->keyItems;
    default:
        *size = 0;
        return NULL;
    }
}

int SaveEd_BagPocket(int pocket, SaveEdBagItem *out, int max)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return 0;
    }
    int size;
    BagItem *items = Pocket(SaveData_GetBag(saveData), pocket, &size);
    int count = 0;
    for (int i = 0; i < size && count < max; i++) {
        if (items[i].item != ITEM_NONE && items[i].quantity > 0) {
            out[count].item = items[i].item;
            out[count].quantity = items[i].quantity;
            count++;
        }
    }
    return count;
}

static s8 sItemPockets[MAX_ITEMS];
static BOOL sItemPocketsLoaded = FALSE;

static void LoadItemPockets(void)
{
    if (sItemPocketsLoaded) {
        return;
    }
    sItemPocketsLoaded = TRUE;
    sItemPockets[ITEM_NONE] = -1;
    for (int item = 1; item < MAX_ITEMS; item++) {
        const char *name = Gear_ItemName(item);
        // Unused entries are named "???"
        if (name[0] == '\0' || strcmp(name, "???") == 0 || strcmp(name, "?") == 0) {
            sItemPockets[item] = -1;
            continue;
        }
        sItemPockets[item] = Item_LoadParam(item, ITEM_PARAM_FIELD_POCKET, HEAP_ID_SYSTEM);
    }
}

int SaveEd_ItemPocket(u16 item)
{
    if (item == ITEM_NONE || item >= MAX_ITEMS) {
        return -1;
    }
    LoadItemPockets();
    return sItemPockets[item];
}

static int CompareItemNames(const void *a, const void *b)
{
    u16 itemA = *(const u16 *)a, itemB = *(const u16 *)b;
    // TMs and HMs keep their numbers' order
    BOOL tmA = itemA >= ITEM_TM01 && itemA <= ITEM_HM08, tmB = itemB >= ITEM_TM01 && itemB <= ITEM_HM08;
    if (tmA && tmB) {
        return (int)itemA - (int)itemB;
    }
    return strcmp(Gear_ItemName(itemA), Gear_ItemName(itemB));
}

int SaveEd_PocketItems(int pocket, u16 *out, int max)
{
    LoadItemPockets();
    int count = 0;
    for (int item = 1; item < MAX_ITEMS && count < max; item++) {
        if (sItemPockets[item] == pocket) {
            out[count++] = item;
        }
    }
    qsort(out, count, sizeof(u16), CompareItemNames);
    return count;
}

int SaveEd_ItemMaxQuantity(u16 item)
{
    switch (SaveEd_ItemPocket(item)) {
    case POCKET_KEY_ITEMS:
        return 1;
    case POCKET_TMHMS:
        return item >= ITEM_HM01 ? 1 : 99;
    case -1:
        return 0;
    default:
        return 999;
    }
}

int SaveEd_ItemQuantity(u16 item)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL || SaveEd_ItemPocket(item) < 0) {
        return 0;
    }
    return Bag_GetItemQuantity(SaveData_GetBag(saveData), item, HEAP_ID_SYSTEM);
}

BOOL SaveEd_SetItemQuantity(u16 item, int quantity)
{
    SaveData *saveData = EditableSave();
    const int max = SaveEd_ItemMaxQuantity(item);
    if (saveData == NULL || max == 0) {
        return FALSE;
    }
    quantity = quantity < 0 ? 0 : (quantity > max ? max : quantity);
    Bag *bag = SaveData_GetBag(saveData);
    int current = Bag_GetItemQuantity(bag, item, HEAP_ID_SYSTEM);
    if (quantity > current) {
        return Bag_TryAddItem(bag, item, quantity - current, HEAP_ID_SYSTEM);
    }
    if (quantity < current) {
        Bag_TryRemoveItem(bag, item, current - quantity, HEAP_ID_SYSTEM);
        if (quantity == 0 && Bag_GetRegisteredItem(bag) == item) {
            Bag_RegisterItem(bag, ITEM_NONE);
        }
        return TRUE;
    }
    return FALSE;
}

// --- Trainer ---------------------------------------------------------------------------

const char *SaveEd_BadgeName(int badge)
{
    static const char *const names[8] = { "Coal", "Forest", "Cobble", "Fen", "Relic", "Mine", "Icicle", "Beacon" };
    return badge >= 0 && badge < 8 ? names[badge] : "?";
}

void SaveEd_GetTrainer(SaveEdTrainer *out)
{
    memset(out, 0, sizeof(*out));
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return;
    }
    TrainerInfo *info = SaveData_GetTrainerInfo(saveData);
    Gear_TextToAscii(TrainerInfo_Name(info), TRAINER_NAME_LEN + 1, out->name, sizeof(out->name));
    out->id = TrainerInfo_ID_LowHalf(info);
    out->money = TrainerInfo_Money(info);
    out->battlePoints = WifiBattleTowerRecord_UpdateBattlePoints(SaveData_GetWifiBattleTowerRecord(saveData), 0, BATTLE_POINTS_FUNC_NONE);
    for (int badge = 0; badge < 8; badge++) {
        if (TrainerInfo_HasBadge(info, badge)) {
            out->badges |= 1 << badge;
        }
    }
    PlayTime *playTime = SaveData_GetPlayTime(saveData);
    out->hours = PlayTime_GetHours(playTime);
    out->minutes = PlayTime_GetMinutes(playTime);
    out->nationalDex = Pokedex_IsNationalDexObtained(SaveData_GetPokedex(saveData));
    out->gameCompleted = SystemFlag_CheckGameCompleted(SaveData_GetVarsFlags(saveData));
}

void SaveEd_SetMoney(u32 money)
{
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        TrainerInfo_SetMoney(SaveData_GetTrainerInfo(saveData), money > SAVE_ED_MAX_MONEY ? SAVE_ED_MAX_MONEY : money);
    }
}

void SaveEd_SetBattlePoints(int points)
{
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        points = points < 0 ? 0 : (points > SAVE_ED_MAX_BP ? SAVE_ED_MAX_BP : points);
        WifiBattleTowerRecord_UpdateBattlePoints(SaveData_GetWifiBattleTowerRecord(saveData), points, BATTLE_POINTS_FUNC_SET);
    }
}

void SaveEd_SetBadge(int badge, BOOL has)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL || badge < 0 || badge >= 8) {
        return;
    }
    TrainerInfo *info = SaveData_GetTrainerInfo(saveData);
    if (has) {
        TrainerInfo_SetBadge(info, badge);
    } else {
        info->badgeMask &= ~(1 << badge);
    }
}

void SaveEd_SetPlayTime(int hours, int minutes)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return;
    }
    PlayTime *playTime = SaveData_GetPlayTime(saveData);
    playTime->hours = hours < 0 ? 0 : (hours > PLAYTIME_MAX_HOURS ? PLAYTIME_MAX_HOURS : hours);
    playTime->minutes = minutes < 0 ? 0 : (minutes > PLAYTIME_MAX_MINUTES ? PLAYTIME_MAX_MINUTES : minutes);
}

void SaveEd_GiveNationalDex(void)
{
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        // As the game does (the GetSetNationalDexEnabled script command)
        Pokedex_ObtainNationalDex(SaveData_GetPokedex(saveData));
        TrainerInfo_GiveNationalDex(SaveData_GetTrainerInfo(saveData));
    }
}

// --- Mystery Gift events ---------------------------------------------------------------

static const struct {
    enum DistributionEvent event;
    u16 item;
    u16 caughtFlag; // 0: none
    u16 storyFlag;  // 0: none
    BOOL postgame;
} sEvents[SAVE_ED_EVENTS] = {
    [SAVE_ED_EVENT_DARKRAI] = { DISTRIBUTION_EVENT_DARKRAI, ITEM_MEMBER_CARD, FLAG_CAUGHT_DARKRAI, FLAG_WOKE_UP_CANALAVE_CITY_SAILOR_ELDRITCH_HOUSE_LITTLE_BOY, TRUE },
    [SAVE_ED_EVENT_SHAYMIN] = { DISTRIBUTION_EVENT_SHAYMIN, ITEM_OAKS_LETTER, FLAG_CAUGHT_SHAYMIN, 0, TRUE },
    [SAVE_ED_EVENT_ARCEUS] = { DISTRIBUTION_EVENT_ARCEUS, ITEM_AZURE_FLUTE, FLAG_CAUGHT_ARCEUS, 0, TRUE },
    [SAVE_ED_EVENT_ROTOM] = { DISTRIBUTION_EVENT_ROTOM, ITEM_SECRET_KEY, 0, FLAG_TEAM_GALACTIC_LEFT_ETERNA_BUILDING, FALSE },
};

void SaveEd_GetEvent(enum SaveEdEvent event, SaveEdEventState *out)
{
    memset(out, 0, sizeof(*out));
    SaveData *saveData = EditableSave();
    if (saveData == NULL || event < 0 || event >= SAVE_ED_EVENTS) {
        return;
    }
    VarsFlags *varsFlags = SaveData_GetVarsFlags(saveData);
    out->started = SystemVars_CheckDistributionEvent(varsFlags, sEvents[event].event);
    out->hasItem = Bag_GetItemQuantity(SaveData_GetBag(saveData), sEvents[event].item, HEAP_ID_SYSTEM) > 0;
    out->caught = sEvents[event].caughtFlag != 0 && VarsFlags_CheckFlag(varsFlags, sEvents[event].caughtFlag);
    out->needsPostgame = sEvents[event].postgame;
    out->gameCompleted = SystemFlag_CheckGameCompleted(varsFlags);
    out->nationalDex = Pokedex_IsNationalDexObtained(SaveData_GetPokedex(saveData));
    out->storyStep = sEvents[event].storyFlag == 0 || VarsFlags_CheckFlag(varsFlags, sEvents[event].storyFlag);
}

BOOL SaveEd_StartEvent(enum SaveEdEvent event)
{
    SaveData *saveData = EditableSave();
    if (saveData == NULL || event < 0 || event >= SAVE_ED_EVENTS) {
        return FALSE;
    }
    // As receiving the Mystery Gift does (src/scrcmd_mystery_gift.c)
    Bag *bag = SaveData_GetBag(saveData);
    VarsFlags *varsFlags = SaveData_GetVarsFlags(saveData);
    if (Bag_GetItemQuantity(bag, sEvents[event].item, HEAP_ID_SYSTEM) == 0
        && !Bag_TryAddItem(bag, sEvents[event].item, 1, HEAP_ID_SYSTEM)) {
        return FALSE;
    }
    SystemVars_SetDistributionEventMagic(varsFlags, sEvents[event].event);
    if (event == SAVE_ED_EVENT_SHAYMIN && SystemVars_GetShayminEventState(varsFlags) == 0) {
        SystemVars_SetShayminEventState(varsFlags, 1);
    }
    return TRUE;
}
