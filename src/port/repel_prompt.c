// "Use another Repel?" See port/repel_prompt.h
#include "port/repel_prompt.h"

#include <nitro.h>
#include <string.h>

#include "constants/charcode.h"
#include "generated/items.h"

#include "field/field_system.h"
#include "charcode_convert.h"
#include "port/sim_config_prj.h"

#include "bag.h"
#include "bg_window.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "game_options.h"
#include "heap.h"
#include "item.h"
#include "map_object.h"
#include "menu.h"
#include "render_window.h"
#include "save_player.h"
#include "savedata.h"
#include "sound_playback.h"
#include "special_encounter.h"
#include "string_gf.h"
#include "system.h"
#include "text.h"
#include "trainer_info.h"

enum {
    STATE_ASK = 0,
    STATE_WAIT_QUESTION,
    STATE_YES_NO,
    STATE_WAIT_USED,
    STATE_WAIT_BUTTON,
    STATE_END,
};

typedef struct RepelPrompt {
    FieldSystem *fieldSystem;
    u16 item;
    int state;
    u8 printer;
    Window window;
    Menu *menu;
    String *text;
} RepelPrompt;

// Where the Yes/No box goes (as the Pal Pad's, ov5_021EA874.c)
static const WindowTemplate sYesNoTemplate = {
    .bgLayer = BG_LAYER_MAIN_3,
    .tilemapLeft = 25,
    .tilemapTop = 13,
    .width = 6,
    .height = 4,
    .palette = 13,
    .baseTile = 0x21F,
};

#define WINDOW_TILE_START (1024 - (18 + 12) - 9)
#define WINDOW_PALETTE 11

static u16 sLastUsed = ITEM_NONE;

void RepelPrompt_NoteUsed(u16 item)
{
    sLastUsed = item;
}

// The Repel to offer: the last one used if the bag still has one, else the weakest
static u16 RepelToOffer(Bag *bag)
{
    if (sLastUsed != ITEM_NONE && Bag_CanRemoveItem(bag, sLastUsed, 1, HEAP_ID_FIELD1)) {
        return sLastUsed;
    }
    static const u16 kRepels[] = { ITEM_REPEL, ITEM_SUPER_REPEL, ITEM_MAX_REPEL };
    for (int i = 0; i < (int)NELEMS(kRepels); i++) {
        if (Bag_CanRemoveItem(bag, kRepels[i], 1, HEAP_ID_FIELD1)) {
            return kRepels[i];
        }
    }
    return ITEM_NONE;
}

static void AppendAscii(String *string, const char *text)
{
    charcode_t chars[128];
    int len = CharCode_FromAscii(text, chars, NELEMS(chars));
    for (int i = 0; i < len; i++) {
        String_AppendChar(string, chars[i]);
    }
}

static void AppendItemName(String *string, u16 item)
{
    String *name = String_Init(32, HEAP_ID_FIELD1);
    Item_LoadName(name, item, HEAP_ID_FIELD1);
    String_Concat(string, name);
    String_Free(name);
}

static void ShowText(RepelPrompt *prompt)
{
    FieldSystem *fieldSystem = prompt->fieldSystem;
    if (!Window_IsInUse(&prompt->window)) {
        FieldMessage_AddWindow(fieldSystem->bgConfig, &prompt->window, BG_LAYER_MAIN_3);
        FieldMessage_DrawWindow(&prompt->window, SaveData_GetOptions(fieldSystem->saveData));
    } else {
        FieldMessage_ClearWindow(&prompt->window);
    }
    prompt->printer = FieldMessage_Print(&prompt->window, prompt->text, SaveData_GetOptions(fieldSystem->saveData), TRUE);
}

static BOOL FieldTask_RepelPrompt(FieldTask *task)
{
    RepelPrompt *prompt = FieldTask_GetEnv(task);
    FieldSystem *fieldSystem = prompt->fieldSystem;

    switch (prompt->state) {
    case STATE_ASK:
        LoadMessageBoxGraphics(fieldSystem->bgConfig, BG_LAYER_MAIN_3, 512 - (18 + 12), 10,
            Options_Frame(SaveData_GetOptions(fieldSystem->saveData)), HEAP_ID_FIELD1);
        LoadStandardWindowGraphics(fieldSystem->bgConfig, BG_LAYER_MAIN_3, WINDOW_TILE_START, WINDOW_PALETTE, 0,
            HEAP_ID_FIELD1);
        String_Clear(prompt->text);
        AppendAscii(prompt->text, "The Repel's effect wore off.\nUse another ");
        AppendItemName(prompt->text, prompt->item);
        AppendAscii(prompt->text, "?");
        ShowText(prompt);
        prompt->state = STATE_WAIT_QUESTION;
        break;

    case STATE_WAIT_QUESTION:
        if (!Text_IsPrinterActive(prompt->printer)) {
            prompt->menu = Menu_MakeYesNoChoice(fieldSystem->bgConfig, &sYesNoTemplate, WINDOW_TILE_START, WINDOW_PALETTE,
                HEAP_ID_FIELD1);
            prompt->state = STATE_YES_NO;
        }
        break;

    case STATE_YES_NO: {
        menu_selection_t choice = Menu_ProcessInputAndHandleExit(prompt->menu, HEAP_ID_FIELD1);
        if (choice == MENU_NOTHING_CHOSEN) {
            break;
        }
        prompt->menu = NULL;
        Bag *bag = SaveData_GetBag(fieldSystem->saveData);
        if (choice != 0 || !Bag_TryRemoveItem(bag, prompt->item, 1, HEAP_ID_FIELD1)) {
            prompt->state = STATE_END;
            break;
        }
        // As using it from the bag does (TryUseRepel)
        u8 *repelSteps = SpecialEncounter_GetRepelSteps(SaveData_GetSpecialEncounters(fieldSystem->saveData));
        *repelSteps = (u8)Item_LoadParam(prompt->item, ITEM_PARAM_EFFECT_PARAM, HEAP_ID_FIELD1);
        sLastUsed = prompt->item;
        Sound_PlayEffect(SEQ_SE_DP_CARD2_sseq);

        String_Clear(prompt->text);
        String *player = TrainerInfo_NameNewString(SaveData_GetTrainerInfo(fieldSystem->saveData), HEAP_ID_FIELD1);
        String_Concat(prompt->text, player);
        String_Free(player);
        AppendAscii(prompt->text, " used the\n");
        AppendItemName(prompt->text, prompt->item);
        AppendAscii(prompt->text, ".");
        ShowText(prompt);
        prompt->state = STATE_WAIT_USED;
        break;
    }

    case STATE_WAIT_USED:
        if (!Text_IsPrinterActive(prompt->printer)) {
            prompt->state = STATE_WAIT_BUTTON;
        }
        break;

    case STATE_WAIT_BUTTON:
        if (JOY_NEW(PAD_BUTTON_A | PAD_BUTTON_B) || gSystem.touchPressed) {
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
            prompt->state = STATE_END;
        }
        break;

    case STATE_END:
        if (Window_IsInUse(&prompt->window)) {
            Window_EraseMessageBox(&prompt->window, FALSE);
            Window_Remove(&prompt->window);
        }
        String_Free(prompt->text);
        MapObjectMan_UnpauseAllMovement(fieldSystem->mapObjMan);
        Heap_Free(prompt);
        return TRUE;
    }
    return FALSE;
}

BOOL RepelPrompt_Start(FieldSystem *fieldSystem)
{
    if (!SIM_Config_prj_GetConfig()->repelPrompt || fieldSystem->task != NULL) {
        return FALSE;
    }
    u16 item = RepelToOffer(SaveData_GetBag(fieldSystem->saveData));
    if (item == ITEM_NONE) {
        return FALSE;
    }

    RepelPrompt *prompt = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(RepelPrompt));
    memset(prompt, 0, sizeof(*prompt));
    prompt->fieldSystem = fieldSystem;
    prompt->item = item;
    prompt->state = STATE_ASK;
    prompt->text = String_Init(160, HEAP_ID_FIELD2);
    Window_Init(&prompt->window);

    MapObjectMan_PauseAllMovement(fieldSystem->mapObjMan);
    FieldSystem_CreateTask(fieldSystem, FieldTask_RepelPrompt, prompt);
    return TRUE;
}
