// The Gear's map. See include/port/gear_map.h
#include "port/gear_map.h"

#include <nitro.h>
#include <stdlib.h>
#include <string.h>

#include "constants/field/map.h"
#include "constants/field/map_tile_behaviors.h"
#include "constants/map_object.h"
#include "constants/narc.h"
#include "generated/bg_event_types.h"
#include "generated/items.h"

#include "field/field_system.h"
#include "field_task.h"
#include "port/debug_field.h"

#include "location.h"
#include "map_header_data.h"
#include "map_matrix.h"
#include "map_tile_behavior.h"
#include "narc.h"
#include "player_avatar.h"
#include "script_manager.h"

#include "res/field/scripts/scr_seq.naix"

// At most this many blocks across (a long route shows the part around the player)
#define MAX_BLOCKS 12

static GearMap sMap;
static u8 *sTiles = NULL;
static u32 sKey = 0;   // what the tiles were built from
static int sOriginX;   // the tiles' origin, in the matrix's tiles
static int sOriginZ;

// --- the items in item balls ------------------------------------------------------------------

// The visible-item scripts start "SetVar 0x8008, item; SetVar 0x8009, quantity" (read from the
// ROM, so ROM hacks' items show)
#define SCRCMD_SETVARFROMVALUE 0x28
#define MAX_VISIBLE_ITEM_SCRIPTS (SCRIPT_ID_OFFSET_HIDDEN_ITEMS - SCRIPT_ID_OFFSET_VISIBLE_ITEMS)

static u16 sBallItems[MAX_VISIBLE_ITEM_SCRIPTS];
static u8 sBallQuantities[MAX_VISIBLE_ITEM_SCRIPTS];
static BOOL sBallItemsRead = FALSE;

static u16 Read16(const u8 *data, u32 offset)
{
    return (u16)(data[offset] | (data[offset + 1] << 8));
}

static void ReadBallItems(void)
{
    sBallItemsRead = TRUE;
    u32 size = 0;
    u8 *file = NARC_PortReadMember(NARC_INDEX_FIELDDATA__SCRIPT__SCR_SEQ, scripts_visible_items, &size);
    if (file == NULL) {
        return;
    }
    for (int i = 0; i < MAX_VISIBLE_ITEM_SCRIPTS && (u32)i * 4 + 4 <= size; i++) {
        // The entries' table ends with 0xFD13
        if (Read16(file, i * 4) == 0xFD13) {
            break;
        }
        const u32 offset = (u32)i * 4 + 4 + (file[i * 4] | (file[i * 4 + 1] << 8) | (file[i * 4 + 2] << 16) | ((u32)file[i * 4 + 3] << 24));
        if (offset + 12 > size || Read16(file, offset) != SCRCMD_SETVARFROMVALUE || Read16(file, offset + 2) != 0x8008) {
            continue;
        }
        sBallItems[i] = Read16(file, offset + 4);
        sBallQuantities[i] = 1;
        if (Read16(file, offset + 6) == SCRCMD_SETVARFROMVALUE && Read16(file, offset + 8) == 0x8009) {
            sBallQuantities[i] = (u8)Read16(file, offset + 10);
        }
    }
    free(file);
}

// --- tiles ------------------------------------------------------------------------------------

static BOOL IsWater(u8 behavior)
{
    return TileBehavior_IsSurfable(behavior) || behavior == TILE_BEHAVIOR_WATER_RIVER || behavior == TILE_BEHAVIOR_WATER_SEA
        || behavior == TILE_BEHAVIOR_WATERFALL;
}

static BOOL IsLedge(u8 behavior)
{
    return (behavior >= TILE_BEHAVIOR_JUMP_EAST && behavior <= TILE_BEHAVIOR_JUMP_SOUTH)
        || (behavior >= TILE_BEHAVIOR_JUMP_NORTH_TWICE && behavior <= TILE_BEHAVIOR_JUMP_EAST_TWICE);
}

static BOOL IsWarp(u8 behavior)
{
    return behavior >= TILE_BEHAVIOR_WARP_STAIRS_EAST && behavior <= TILE_BEHAVIOR_WARP_SOUTH;
}

static BOOL IsBridge(u8 behavior)
{
    return behavior >= TILE_BEHAVIOR_BRIDGE_START && behavior <= TILE_BEHAVIOR_BIKE_BRIDGE_E_W_OVER_SAND;
}

// Somewhere a trainer can be (blocked tiles that only stop some directions, or that a move
// gets you over, count)
static BOOL IsPassable(u16 attributes)
{
    const u8 behavior = attributes & TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK;
    if (!(attributes & TERRAIN_ATTRIBUTES_COLLISION_MASK)) {
        return TRUE;
    }
    return IsWater(behavior) || IsLedge(behavior) || IsWarp(behavior) || IsBridge(behavior)
        || (behavior >= TILE_BEHAVIOR_BLOCK_EASTWARD && behavior <= TILE_BEHAVIOR_BLOCK_SOUTH_AND_WEST)
        || behavior == TILE_BEHAVIOR_BLOCK_NORTH_AND_SOUTH || behavior == TILE_BEHAVIOR_BLOCK_EAST_AND_WEST
        || behavior == TILE_BEHAVIOR_ROCK_CLIMB_N_S || behavior == TILE_BEHAVIOR_ROCK_CLIMB_E_W;
}

static u8 Classify(u16 attributes)
{
    const u8 b = attributes & TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK;
    if (b == TILE_BEHAVIOR_WATERFALL) {
        return GEAR_MAP_WATERFALL;
    }
    if (IsWater(b)) {
        return GEAR_MAP_WATER;
    }
    if (TileBehavior_IsTallGrass(b) || TileBehavior_IsMudWithGrass(b)) {
        return GEAR_MAP_GRASS;
    }
    if (TileBehavior_IsVeryTallGrass(b) || TileBehavior_IsDeepMudWithGrass(b)) {
        return GEAR_MAP_TALL_GRASS;
    }
    switch (b) {
    case TILE_BEHAVIOR_JUMP_NORTH:
    case TILE_BEHAVIOR_JUMP_NORTH_TWICE:
        return GEAR_MAP_LEDGE_NORTH;
    case TILE_BEHAVIOR_JUMP_SOUTH:
    case TILE_BEHAVIOR_JUMP_SOUTH_TWICE:
        return GEAR_MAP_LEDGE_SOUTH;
    case TILE_BEHAVIOR_JUMP_WEST:
    case TILE_BEHAVIOR_JUMP_WEST_TWICE:
        return GEAR_MAP_LEDGE_WEST;
    case TILE_BEHAVIOR_JUMP_EAST:
    case TILE_BEHAVIOR_JUMP_EAST_TWICE:
        return GEAR_MAP_LEDGE_EAST;
    case TILE_BEHAVIOR_ROCK_CLIMB_N_S:
    case TILE_BEHAVIOR_ROCK_CLIMB_E_W:
        return GEAR_MAP_ROCK_CLIMB;
    case TILE_BEHAVIOR_BERRY_PATCH:
        return GEAR_MAP_SOIL;
    case TILE_BEHAVIOR_CAVE_FLOOR:
    case TILE_BEHAVIOR_MOUNTAIN_FLOOR:
        return GEAR_MAP_CAVE;
    default:
        break;
    }
    if (IsWarp(b)) {
        return GEAR_MAP_WARP;
    }
    if (IsBridge(b)) {
        return GEAR_MAP_BRIDGE;
    }
    if (TileBehavior_IsSand(b)) {
        return GEAR_MAP_SAND;
    }
    if (TileBehavior_IsIce(b)) {
        return GEAR_MAP_ICE;
    }
    if (TileBehavior_IsSnow(b) || TileBehavior_IsShallowSnow(b) || TileBehavior_IsDeepSnow(b) || TileBehavior_IsDeeperSnow(b)
        || TileBehavior_IsDeepestSnow(b) || b == TILE_BEHAVIOR_SNOW_WITH_SHADOWS) {
        return GEAR_MAP_SNOW;
    }
    if (TileBehavior_IsMud(b) || TileBehavior_IsDeepMud(b)) {
        return GEAR_MAP_MUD;
    }
    return GEAR_MAP_FLOOR;
}

static u32 Hash(u32 hash, u32 value)
{
    return (hash ^ value) * 16777619u;
}

// What the tiles depend on: the map, its matrix (paths that open change it), the player's
// block when the map is too big to show whole
static u32 TilesKey(FieldSystem *fieldSystem, int blockX, int blockZ)
{
    const MapMatrix *matrix = fieldSystem->mapMatrix;
    u32 key = Hash(2166136261u, (u32)fieldSystem->location->mapHeaderID);
    key = Hash(key, matrix->matrixID);
    key = Hash(key, ((u32)matrix->width << 8) | matrix->height);
    for (int i = 0; i < matrix->width * matrix->height; i++) {
        key = Hash(key, matrix->data.landDataIDs[i] | ((u32)matrix->data.mapHeaderIDs[i] << 16));
    }
    key = Hash(key, (u32)(blockX / MAX_BLOCKS) | ((u32)(blockZ / MAX_BLOCKS) << 8));
    return key == 0 ? 1 : key;
}

static void BuildTiles(FieldSystem *fieldSystem, int playerX, int playerZ)
{
    const MapMatrix *matrix = fieldSystem->mapMatrix;
    const int width = matrix->width, height = matrix->height;
    const int header = fieldSystem->location->mapHeaderID;

    // The map's blocks, and one block around them
    int x0 = width, x1 = -1, z0 = height, z1 = -1;
    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {
            if (matrix->data.mapHeaderIDs[z * width + x] == header) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                z0 = z < z0 ? z : z0;
                z1 = z > z1 ? z : z1;
            }
        }
    }
    const int playerBlockX = playerX / MAP_TILES_COUNT_X, playerBlockZ = playerZ / MAP_TILES_COUNT_Z;
    if (x1 < 0) {
        x0 = x1 = playerBlockX;
        z0 = z1 = playerBlockZ;
    }
    x0 = x0 > 0 ? x0 - 1 : 0;
    z0 = z0 > 0 ? z0 - 1 : 0;
    x1 = x1 < width - 1 ? x1 + 1 : width - 1;
    z1 = z1 < height - 1 ? z1 + 1 : height - 1;
    // Too big: the part around the player
    if (x1 - x0 + 1 > MAX_BLOCKS) {
        x0 = playerBlockX - MAX_BLOCKS / 2;
        x0 = x0 < 0 ? 0 : (x0 + MAX_BLOCKS > width ? width - MAX_BLOCKS : x0);
        x1 = x0 + MAX_BLOCKS - 1;
    }
    if (z1 - z0 + 1 > MAX_BLOCKS) {
        z0 = playerBlockZ - MAX_BLOCKS / 2;
        z0 = z0 < 0 ? 0 : (z0 + MAX_BLOCKS > height ? height - MAX_BLOCKS : z0);
        z1 = z0 + MAX_BLOCKS - 1;
    }

    const int w = (x1 - x0 + 1) * MAP_TILES_COUNT_X, h = (z1 - z0 + 1) * MAP_TILES_COUNT_Z;
    u16 *attributes = malloc(sizeof(u16) * w * h);
    u8 *other = calloc(w * h, 1);   // a neighbouring map's tile
    u8 *reached = calloc(w * h, 1); // you can get there
    int *queue = malloc(sizeof(int) * w * h);
    free(sTiles);
    sTiles = calloc(w * h, 1);
    if (attributes == NULL || other == NULL || reached == NULL || queue == NULL || sTiles == NULL) {
        free(attributes);
        free(other);
        free(reached);
        free(queue);
        free(sTiles);
        sTiles = NULL;
        sMap.width = sMap.height = 0;
        return;
    }

    // Each block's tiles (the land data starts with a 16-byte header, then the attributes)
    static u16 block[MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z];
    for (int bz = z0; bz <= z1; bz++) {
        for (int bx = x0; bx <= x1; bx++) {
            const u16 landDataID = matrix->data.landDataIDs[bz * width + bx];
            const BOOL isOther = matrix->data.mapHeaderIDs[bz * width + bx] != header;
            const BOOL valid = landDataID != INVALID_LAND_DATA_ID;
            if (valid) {
                NARC_ReadFromMemberByIndexPair(block, NARC_INDEX_FIELDDATA__LAND_DATA__LAND_DATA, landDataID,
                    TERRAIN_ATTRIBUTES_OFFSET, TERRAIN_ATTRIBUTES_SIZE);
            }
            for (int t = 0; t < MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z; t++) {
                const int x = (bx - x0) * MAP_TILES_COUNT_X + t % MAP_TILES_COUNT_X;
                const int z = (bz - z0) * MAP_TILES_COUNT_Z + t / MAP_TILES_COUNT_X;
                // A block without land data: blocked, nothing to see
                attributes[z * w + x] = valid ? block[t] : (TERRAIN_ATTRIBUTES_COLLISION_MASK | 0x7F);
                other[z * w + x] = isOther;
            }
        }
    }
    sOriginX = x0 * MAP_TILES_COUNT_X;
    sOriginZ = z0 * MAP_TILES_COUNT_Z;

    // Where you can get to: from the player, the warps, the people and the items
    int head = 0, tail = 0;
#define SEED(gx, gz, force)                                                                        \
    do {                                                                                           \
        const int sx = (gx) - sOriginX, sz = (gz) - sOriginZ;                                      \
        if (sx >= 0 && sx < w && sz >= 0 && sz < h && !reached[sz * w + sx]                        \
            && ((force) || IsPassable(attributes[sz * w + sx]))) {                                 \
            reached[sz * w + sx] = 1;                                                              \
            queue[tail++] = sz * w + sx;                                                           \
        }                                                                                          \
    } while (0)

    SEED(playerX, playerZ, TRUE);
    const MapHeaderData *events = fieldSystem->mapHeaderData;
    if (events != NULL) {
        for (u32 i = 0; i < events->numWarpEvents; i++) {
            SEED(events->warpEvents[i].x, events->warpEvents[i].z, FALSE);
        }
        for (u32 i = 0; i < events->numObjectEvents; i++) {
            SEED(events->objectEvents[i].x, events->objectEvents[i].z, FALSE);
        }
        for (u32 i = 0; i < events->numBgEvents; i++) {
            if (events->bgEvents[i].type == BG_EVENT_TYPE_HIDDEN_ITEM) {
                SEED(events->bgEvents[i].x, events->bgEvents[i].z, FALSE);
            }
        }
    }
#undef SEED
    while (head < tail) {
        const int index = queue[head++];
        const int x = index % w, z = index / w;
        static const int kSteps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int s = 0; s < 4; s++) {
            const int nx = x + kSteps[s][0], nz = z + kSteps[s][1];
            if (nx < 0 || nx >= w || nz < 0 || nz >= h) {
                continue;
            }
            const int next = nz * w + nx;
            if (!reached[next] && IsPassable(attributes[next])) {
                reached[next] = 1;
                queue[tail++] = next;
            }
        }
    }

    // What each tile shows: where you can go by its terrain, the blocked tiles next to it as
    // walls, the rest nothing
    int areaX0 = w, areaZ0 = h, areaX1 = -1, areaZ1 = -1;
    for (int z = 0; z < h; z++) {
        for (int x = 0; x < w; x++) {
            const int index = z * w + x;
            u8 tile = GEAR_MAP_VOID;
            if (reached[index]) {
                tile = (attributes[index] & TERRAIN_ATTRIBUTES_COLLISION_MASK) && !IsPassable(attributes[index])
                    ? GEAR_MAP_FLOOR // the player's tile, blocked (an event's)
                    : Classify(attributes[index]);
            } else if (!IsPassable(attributes[index])) {
                for (int dz = -1; dz <= 1 && tile == GEAR_MAP_VOID; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx, nz = z + dz;
                        if (nx >= 0 && nx < w && nz >= 0 && nz < h && reached[nz * w + nx]) {
                            tile = GEAR_MAP_WALL;
                            break;
                        }
                    }
                }
            }
            if (tile != GEAR_MAP_VOID && other[index]) {
                tile |= GEAR_MAP_OTHER_AREA;
            } else if (tile != GEAR_MAP_VOID) {
                areaX0 = x < areaX0 ? x : areaX0;
                areaX1 = x > areaX1 ? x : areaX1;
                areaZ0 = z < areaZ0 ? z : areaZ0;
                areaZ1 = z > areaZ1 ? z : areaZ1;
            }
            sTiles[index] = tile;
        }
    }

    free(attributes);
    free(other);
    free(reached);
    free(queue);
    if (areaX1 < 0) {
        areaX0 = areaZ0 = 0;
        areaX1 = w - 1;
        areaZ1 = h - 1;
    }
    sMap.areaX0 = areaX0;
    sMap.areaZ0 = areaZ0;
    sMap.areaX1 = areaX1;
    sMap.areaZ1 = areaZ1;
    sMap.width = w;
    sMap.height = h;
    sMap.tilesVersion++;
}

// --- the map ----------------------------------------------------------------------------------

static void AddItem(int x, int z, u16 item, u8 quantity, BOOL hidden)
{
    if (sMap.itemCount >= GEAR_MAP_MAX_ITEMS) {
        return;
    }
    GearMapItem *entry = &sMap.items[sMap.itemCount++];
    entry->x = (s16)(x - sOriginX);
    entry->z = (s16)(z - sOriginZ);
    entry->item = item;
    entry->quantity = quantity;
    entry->hidden = hidden ? 1 : 0;
}

const GearMap *Gear_GetMap(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->location == NULL || fieldSystem->mapMatrix == NULL
        || fieldSystem->mapHeaderData == NULL || fieldSystem->playerAvatar == NULL || fieldSystem->mapMatrix->width == 0) {
        sMap.valid = FALSE;
        return &sMap;
    }
    // Only while the overworld runs (a map change frees and remakes the player and the map's
    // data; menus stop it): meanwhile the map stays as it was
    if (!FieldSystem_IsRunningFieldMap(fieldSystem)) {
        return &sMap;
    }

    const int playerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
    const int playerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);
    const u32 key = TilesKey(fieldSystem, playerX / MAP_TILES_COUNT_X, playerZ / MAP_TILES_COUNT_Z);
    if (key != sKey || sTiles == NULL) {
        sKey = key;
        BuildTiles(fieldSystem, playerX, playerZ);
    }
    if (sTiles == NULL) {
        sMap.valid = FALSE;
        return &sMap;
    }
    sMap.valid = TRUE;
    sMap.mapHeaderID = fieldSystem->location->mapHeaderID;
    sMap.tiles = sTiles;
    sMap.playerX = playerX - sOriginX;
    sMap.playerZ = playerZ - sOriginZ;
    sMap.playerDir = PlayerAvatar_GetFacingDir(fieldSystem->playerAvatar);

    // Items still there: balls whose flag (set when picked up) isn't set, hidden items whose
    // flag isn't set
    if (!sBallItemsRead) {
        ReadBallItems();
    }
    sMap.itemCount = 0;
    const MapHeaderData *events = fieldSystem->mapHeaderData;
    for (u32 i = 0; i < events->numObjectEvents; i++) {
        const ObjectEvent *object = &events->objectEvents[i];
        if (object->script < SCRIPT_ID_OFFSET_VISIBLE_ITEMS || object->script >= SCRIPT_ID_OFFSET_HIDDEN_ITEMS) {
            continue;
        }
        if (object->hiddenFlag != 0 && FieldSystem_CheckFlag(fieldSystem, object->hiddenFlag)) {
            continue;
        }
        const int index = object->script - SCRIPT_ID_OFFSET_VISIBLE_ITEMS;
        AddItem(object->x, object->z, sBallItems[index], sBallQuantities[index], FALSE);
    }
    for (u32 i = 0; i < events->numBgEvents; i++) {
        const BgEvent *bg = &events->bgEvents[i];
        if (bg->type != BG_EVENT_TYPE_HIDDEN_ITEM || bg->script < SCRIPT_ID_OFFSET_HIDDEN_ITEMS
            || bg->script >= SCRIPT_ID_OFFSET_SAFARI_GAME) {
            continue;
        }
        if (FieldSystem_CheckFlag(fieldSystem, Script_GetHiddenItemFlag(bg->script))) {
            continue;
        }
        u8 quantity = 1;
        const u16 item = Script_GetHiddenItemItem(bg->script, &quantity);
        AddItem(bg->x, bg->z, item, quantity, TRUE);
    }

    sMap.warpCount = 0;
    for (u32 i = 0; i < events->numWarpEvents && sMap.warpCount < GEAR_MAP_MAX_WARPS; i++) {
        sMap.warps[sMap.warpCount][0] = (s16)(events->warpEvents[i].x - sOriginX);
        sMap.warps[sMap.warpCount][1] = (s16)(events->warpEvents[i].z - sOriginZ);
        sMap.warpCount++;
    }
    return &sMap;
}
