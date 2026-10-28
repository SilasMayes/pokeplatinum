// PC boxes and bag for the Gear. See port/gear_pc.h
#include "port/gear_pc.h"

#include <nitro.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "constants/species.h"

#include "field/field_system.h"
#include "port/debug_field.h"
#include "port/gear_data.h"
#include "port/gear_dex.h"
#include "port/save_editor.h"

#include "bag.h"
#include "heap.h"
#include "item.h"
#include "pc_boxes.h"
#include "pokemon.h"
#include "savedata.h"
#include "savedata/save_table.h"
#include "string_gf.h"

const char *Gear_PCUnavailable(void)
{
    return SaveEd_Unavailable();
}

static SaveData *Save(void)
{
    return Gear_PCUnavailable() == NULL ? DEBUG_GetFieldSystem()->saveData : NULL;
}

static void ReadMon(BoxPokemon *boxMon, GearBoxMon *out)
{
    memset(out, 0, sizeof(*out));
    if (!BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES_EXISTS, NULL)) {
        return;
    }
    BOOL decrypted = BoxPokemon_EnterDecryptionContext(boxMon);
    out->species = BoxPokemon_GetValue(boxMon, MON_DATA_SPECIES, NULL);
    out->isEgg = BoxPokemon_GetValue(boxMon, MON_DATA_IS_EGG, NULL);
    out->form = BoxPokemon_GetValue(boxMon, MON_DATA_FORM, NULL);
    out->personality = BoxPokemon_GetValue(boxMon, MON_DATA_PERSONALITY, NULL);
    out->shiny = Pokemon_IsPersonalityShiny(BoxPokemon_GetValue(boxMon, MON_DATA_OT_ID, NULL), out->personality);
    BoxPokemon_ExitDecryptionContext(boxMon, decrypted);
    out->level = BoxPokemon_GetLevel(boxMon);
    out->gender = BoxPokemon_GetGender(boxMon);
}

BOOL Gear_PCReadBox(int box, GearBoxMon out[GEAR_PC_BOX_SLOTS])
{
    memset(out, 0, sizeof(GearBoxMon) * GEAR_PC_BOX_SLOTS);
    SaveData *saveData = Save();
    if (saveData == NULL || box < 0 || box >= GEAR_PC_BOXES) {
        return FALSE;
    }
    PCBoxes *boxes = SaveData_GetPCBoxes(saveData);
    for (int slot = 0; slot < GEAR_PC_BOX_SLOTS; slot++) {
        ReadMon(PCBoxes_GetBoxMonAt(boxes, box, slot), &out[slot]);
    }
    return TRUE;
}

void Gear_PCBoxName(int box, char *out, int size)
{
    SaveEd_BoxName(box, out, size);
}

int Gear_PCCurrentBox(void)
{
    SaveData *saveData = Save();
    return saveData != NULL ? (int)PCBoxes_GetCurrentBoxID(SaveData_GetPCBoxes(saveData)) : 0;
}

// --- sorting the boxes -------------------------------------------------------------------

typedef struct SortEntry {
    BoxPokemon mon;
    GearBoxMon info;
    int order;     // where it was, to keep ties in place
    u16 nameRank;
    u8 type;
} SortEntry;

static int sSortKey;
static u16 sNameRank[NATIONAL_DEX_COUNT + 1];
static BOOL sNameRankReady = FALSE;

static int CompareEntries(const void *a, const void *b)
{
    const SortEntry *ea = a;
    const SortEntry *eb = b;
    // Eggs last, in the order they were
    if (ea->info.isEgg != eb->info.isEgg) {
        return ea->info.isEgg ? 1 : -1;
    }
    if (!ea->info.isEgg) {
        int diff = 0;
        switch (sSortKey) {
        case GEAR_PC_SORT_NAME:
            diff = (int)ea->nameRank - (int)eb->nameRank;
            break;
        case GEAR_PC_SORT_LEVEL:
            diff = (int)eb->info.level - (int)ea->info.level;
            if (diff == 0) {
                diff = (int)ea->info.species - (int)eb->info.species;
            }
            break;
        case GEAR_PC_SORT_TYPE:
            diff = (int)ea->type - (int)eb->type;
            if (diff == 0) {
                diff = (int)ea->info.species - (int)eb->info.species;
            }
            break;
        default:
            break;
        }
        if (diff == 0) {
            diff = (int)ea->info.species - (int)eb->info.species;
        }
        if (diff == 0) {
            diff = (int)ea->info.form - (int)eb->info.form;
        }
        if (diff != 0) {
            return diff;
        }
    }
    return ea->order - eb->order;
}

BOOL Gear_PCSort(int box, int key)
{
    SaveData *saveData = Save();
    if (saveData == NULL || box < -1 || box >= GEAR_PC_BOXES) {
        return FALSE;
    }
    if (!sNameRankReady) {
        static u16 order[NATIONAL_DEX_COUNT];
        int count = Gear_SpeciesByName(order, NATIONAL_DEX_COUNT);
        for (int i = 0; i < count; i++) {
            sNameRank[order[i]] = (u16)(i + 1);
        }
        sNameRankReady = TRUE;
    }

    PCBoxes *boxes = SaveData_GetPCBoxes(saveData);
    const int first = box < 0 ? 0 : box;
    const int last = box < 0 ? GEAR_PC_BOXES - 1 : box;
    const int capacity = (last - first + 1) * GEAR_PC_BOX_SLOTS;
    SortEntry *entries = malloc(sizeof(SortEntry) * capacity);
    if (entries == NULL) {
        return FALSE;
    }

    int count = 0;
    for (int b = first; b <= last; b++) {
        for (int slot = 0; slot < GEAR_PC_BOX_SLOTS; slot++) {
            BoxPokemon *boxMon = PCBoxes_GetBoxMonAt(boxes, b, slot);
            SortEntry *entry = &entries[count];
            ReadMon(boxMon, &entry->info);
            if (entry->info.species == SPECIES_NONE) {
                continue;
            }
            entry->mon = *boxMon;
            entry->order = count;
            entry->nameRank = entry->info.species <= NATIONAL_DEX_COUNT ? sNameRank[entry->info.species] : 0xFFFF;
            u8 type1, type2;
            Gear_SpeciesTypes(entry->info.species, &type1, &type2);
            entry->type = type1;
            count++;
        }
    }

    sSortKey = key;
    qsort(entries, count, sizeof(SortEntry), CompareEntries);

    // Back in order from the first slot; the slots after them empty
    for (int i = 0; i < capacity; i++) {
        BoxPokemon *boxMon = PCBoxes_GetBoxMonAt(boxes, first + i / GEAR_PC_BOX_SLOTS, i % GEAR_PC_BOX_SLOTS);
        if (i < count) {
            *boxMon = entries[i].mon;
        } else {
            BoxPokemon_Init(boxMon);
        }
    }
    free(entries);
    SaveData_SetFullSaveRequired();
    return TRUE;
}

// --- sorting the bag ----------------------------------------------------------------------

typedef struct BagSortEntry {
    BagItem item;
    char name[24];
    int order;
} BagSortEntry;

static int sBagKey;

static int CompareBagEntries(const void *a, const void *b)
{
    const BagSortEntry *ea = a;
    const BagSortEntry *eb = b;
    int diff = 0;
    if (sBagKey == GEAR_BAG_SORT_NAME) {
        diff = strcasecmp(ea->name, eb->name);
    }
    if (diff == 0) {
        diff = (int)ea->item.item - (int)eb->item.item;
    }
    return diff != 0 ? diff : ea->order - eb->order;
}

static void SortPocket(BagItem *items, int size)
{
    BagSortEntry *entries = malloc(sizeof(BagSortEntry) * size);
    if (entries == NULL) {
        return;
    }
    int count = 0;
    String *name = String_Init(32, HEAP_ID_SYSTEM);
    for (int i = 0; i < size; i++) {
        if (items[i].item == ITEM_NONE || items[i].quantity == 0) {
            continue;
        }
        BagSortEntry *entry = &entries[count];
        entry->item = items[i];
        entry->order = count;
        entry->name[0] = '\0';
        if (sBagKey == GEAR_BAG_SORT_NAME) {
            Item_LoadName(name, items[i].item, HEAP_ID_SYSTEM);
            Gear_TextToAscii(String_GetData(name), String_Length(name), entry->name, sizeof(entry->name));
        }
        count++;
    }
    String_Free(name);
    qsort(entries, count, sizeof(BagSortEntry), CompareBagEntries);
    for (int i = 0; i < size; i++) {
        if (i < count) {
            items[i] = entries[i].item;
        } else {
            items[i].item = ITEM_NONE;
            items[i].quantity = 0;
        }
    }
    free(entries);
}

BOOL Gear_BagSort(int key)
{
    SaveData *saveData = Save();
    if (saveData == NULL) {
        return FALSE;
    }
    Bag *bag = SaveData_GetBag(saveData);
    sBagKey = key;
    SortPocket(bag->items, ITEM_POCKET_SIZE);
    SortPocket(bag->keyItems, KEY_ITEM_POCKET_SIZE);
    SortPocket(bag->medicine, MEDICINE_POCKET_SIZE);
    SortPocket(bag->pokeballs, POKEBALL_POCKET_SIZE);
    SortPocket(bag->battleItems, BATTLE_ITEM_POCKET_SIZE);
    SortPocket(bag->mail, MAIL_POCKET_SIZE);
    // TMs and berries go by their numbers either way (the game keeps them so)
    sBagKey = GEAR_BAG_SORT_NUMBER;
    SortPocket(bag->tmHms, TMHM_POCKET_SIZE);
    SortPocket(bag->berries, BERRY_POCKET_SIZE);
    return TRUE;
}
