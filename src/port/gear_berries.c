// The Gear's berry timers. See port/gear_berries.h
#include "port/gear_berries.h"

#include <nitro.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "constants/items.h"
#include "generated/berry_growth_stages.h"
#include "generated/map_headers.h"
#include "generated/movement_types.h"

#include "field/field_system.h"
#include "port/debug_field.h"

#include "berry_patches.h"
#include "heap.h"
#include "map_header.h"
#include "map_header_data.h"
#include "narc.h"
#include "savedata.h"
#include "savedata_misc.h"

// Which map each patch is on: from every map's events, the berry soil objects (their
// first data value is the patch)
static u16 sPatchMaps[MAX_BERRY_PATCHES];
static BOOL sPatchMapsReady = FALSE;

static void IndexPatchMaps(void)
{
    sPatchMapsReady = TRUE;
    static u8 doneEvents[2048];
    memset(doneEvents, 0, sizeof(doneEvents));
    for (int header = 0; header < MAP_HEADER_COUNT; header++) {
        const u32 eventsID = MapHeader_GetEventsArchiveID(header);
        if (eventsID < sizeof(doneEvents) && doneEvents[eventsID]) {
            continue;
        }
        if (eventsID < sizeof(doneEvents)) {
            doneEvents[eventsID] = 1;
        }
        u32 size = 0;
        u8 *events = NARC_PortReadMember(NARC_INDEX_FIELDDATA__EVENTDATA__ZONE_EVENT, eventsID, &size);
        if (events == NULL) {
            continue;
        }
        // u32 count + BgEvents, then u32 count + ObjectEvents (MapHeaderData_ParseEvents)
        u32 at = 0;
        if (size >= 4) {
            u32 bgCount = *(u32 *)(events + at);
            at += 4 + bgCount * sizeof(BgEvent);
        }
        if (at + 4 <= size) {
            u32 objectCount = *(u32 *)(events + at);
            at += 4;
            for (u32 i = 0; i < objectCount && at + sizeof(ObjectEvent) <= size; i++, at += sizeof(ObjectEvent)) {
                const ObjectEvent *object = (const ObjectEvent *)(events + at);
                if (object->movementType == MOVEMENT_TYPE_BERRY_SOIL && object->data[0] < MAX_BERRY_PATCHES
                    && sPatchMaps[object->data[0]] == 0) {
                    sPatchMaps[object->data[0]] = (u16)header;
                }
            }
        }
        free(events);
    }
}

// The berries' growth data, once
static BerryGrowthData sGrowth[NUM_BERRIES];
static BOOL sGrowthReady = FALSE;

static void LoadGrowth(void)
{
    BerryGrowthData *growth = BerryGrowthData_Init(HEAP_ID_SYSTEM);
    memcpy(sGrowth, growth, sizeof(sGrowth));
    Heap_Free(growth);
    sGrowthReady = TRUE;
}

// berry_patches.c's CalcMinutesRemainingInStage
static int StageMinutes(int berryID, int mulch)
{
    int minutes = sGrowth[berryID - 1].stageDuration * 60;
    if (mulch == MULCH_TYPE_GROWTH) {
        minutes = minutes * 3 / 4;
    } else if (mulch == MULCH_TYPE_DAMP) {
        minutes = minutes + minutes / 2;
    }
    return minutes;
}

static int CompareReady(const void *a, const void *b)
{
    const GearBerryPatch *pa = a;
    const GearBerryPatch *pb = b;
    if (pa->growing != pb->growing) {
        return pa->growing ? -1 : 1;
    }
    if (pa->minutesToRipe != pb->minutesToRipe) {
        return pa->minutesToRipe - pb->minutesToRipe;
    }
    return pa->patchID - pb->patchID;
}

int Gear_GetBerryPatches(GearBerryPatch *out, int max)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return 0;
    }
    if (!sGrowthReady) {
        LoadGrowth();
    }
    if (!sPatchMapsReady) {
        IndexPatchMaps();
    }

    const BerryPatch *patches = MiscSaveBlock_GetBerryPatches(fieldSystem->saveData);
    int count = 0;
    for (int i = 0; i < MAX_BERRY_PATCHES && count < max; i++) {
        const BerryPatch *patch = &patches[i];
        if (patch->berryID == 0 || patch->berryID > NUM_BERRIES || patch->growthStage == BERRY_GROWTH_STAGE_NONE) {
            continue;
        }
        GearBerryPatch *entry = &out[count++];
        memset(entry, 0, sizeof(*entry));
        entry->patchID = i;
        entry->mapHeaderID = sPatchMaps[i];
        entry->berryItem = FIRST_BERRY_IDX + patch->berryID - 1;
        entry->stage = patch->growthStage;
        entry->minutesToNextStage = patch->stageMinutesRemaining;
        entry->moisture = patch->moistureRating;
        entry->mulch = patch->mulchType;
        entry->growing = patch->isGrowing;
        if (patch->growthStage == BERRY_GROWTH_STAGE_FRUIT) {
            entry->minutesToRipe = 0;
            entry->yield = patch->yield;
        } else {
            const int stagesAfter = BERRY_GROWTH_STAGE_FRUIT - patch->growthStage - 1;
            entry->minutesToRipe = patch->stageMinutesRemaining + stagesAfter * StageMinutes(patch->berryID, patch->mulchType);
            int yield = sGrowth[patch->berryID - 1].baseYield * patch->yieldRating;
            entry->yield = yield < 2 ? 2 : yield;
        }
    }
    qsort(out, count, sizeof(GearBerryPatch), CompareReady);
    return count;
}
