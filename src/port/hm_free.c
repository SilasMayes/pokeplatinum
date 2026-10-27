// See include/port/hm_free.h
#include "port/hm_free.h"

#include <nitro.h>

#include "constants/items.h"

#include "port/sim_config_prj.h"

#include "bag.h"
#include "item.h"
#include "party.h"
#include "pokemon.h"
#include "savedata.h"

BOOL HmFree_Enabled(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    return config != NULL && config->hmFree;
}

// The HM item that teaches a move, ITEM_NONE if none does
static u16 HmForMove(u16 move)
{
    for (u16 item = FIRST_HM_IDX; item <= ITEM_HM08; item++) {
        if (Item_MoveForTMHM(item) == move) {
            return item;
        }
    }

    return ITEM_NONE;
}

BOOL HmFree_CanUse(Party *party, u16 move)
{
    if (!HmFree_Enabled() || party == NULL || move == MOVE_NONE) {
        return FALSE;
    }

    const u16 hm = HmForMove(move);
    SaveData *saveData = SaveData_Ptr();
    if (hm == ITEM_NONE || saveData == NULL) {
        return FALSE;
    }

    // In the bag (straight from the TM/HM pocket: this runs while walking)
    Bag *bag = SaveData_GetBag(saveData);
    BOOL owned = FALSE;
    for (int i = 0; i < TMHM_POCKET_SIZE; i++) {
        if (bag->tmHms[i].item == hm && bag->tmHms[i].quantity > 0) {
            owned = TRUE;
            break;
        }
    }
    if (!owned) {
        return FALSE;
    }

    const int count = Party_GetCurrentCount(party);
    for (int slot = 0; slot < count; slot++) {
        if (!Pokemon_GetValue(Party_GetPokemonBySlotIndex(party, slot), MON_DATA_IS_EGG, NULL)) {
            return TRUE;
        }
    }

    return FALSE;
}

int HmFree_SubstituteSlot(Party *party, u16 move)
{
    if (!HmFree_CanUse(party, move)) {
        return -1;
    }

    const u8 tm = Item_TMHMNumber(HmForMove(move));
    const int count = Party_GetCurrentCount(party);
    int fallback = -1;
    for (int slot = 0; slot < count; slot++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, slot);
        if (Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }
        if (Pokemon_CanLearnTM(mon, tm)) {
            return slot;
        }
        if (fallback < 0) {
            fallback = slot;
        }
    }

    return fallback;
}
