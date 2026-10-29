// Tables and instructions of ROM hacks; see include/port/rom_patches.h
#include "port/rom_patches.h"
#include "port/randomizer.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/species.h"

#include <simulator/sim_carts.h>

// --- ROM files ---------------------------------------------------------------------------

typedef struct RomFile {
    BOOL loaded;
    u8 *data;
    u32 size;
} RomFile;

#define MAX_OVERLAYS 130

static RomFile sArm9;
static RomFile sOverlayTable;
static RomFile sOverlays[MAX_OVERLAYS];

static void LoadFile(RomFile *file, const char *path)
{
    file->loaded = TRUE;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && size < 16 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            file->data = malloc(size);
            if (file->data != NULL && fread(file->data, 1, size, f) == (size_t)size) {
                file->size = (u32)size;
            } else {
                free(file->data);
                file->data = NULL;
            }
        }
    }
    fclose(f);
}

static const RomFile *GetFile(int file)
{
    if (file == ROM_FILE_ARM9) {
        if (!sArm9.loaded) {
            LoadFile(&sArm9, "arm9.bin");
        }
        return &sArm9;
    }
    if (file < 0 || file >= MAX_OVERLAYS) {
        return NULL;
    }
    if (!sOverlays[file].loaded) {
        char path[64];
        snprintf(path, sizeof(path), "overlay9/overlay9_%d.bin", file);
        LoadFile(&sOverlays[file], path);
    }
    return &sOverlays[file];
}

static u32 ReadU32(const RomFile *file, u32 offset)
{
    const u8 *p = file->data + offset;
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static u16 ReadU16(const RomFile *file, u32 offset)
{
    const u8 *p = file->data + offset;
    return p[0] | (p[1] << 8);
}

// The files a DS address can belong to: arm9, or any overlay loaded over that address
// (several overlays share each region). candidate counts up from 0; NULL when done
static const RomFile *FileOfAddress(u32 address, int candidate, u32 *offsetOut)
{
    if (!sOverlayTable.loaded) {
        LoadFile(&sOverlayTable, "arm9ovt.bin");
    }
    if (address >= 0x02000000 && sArm9.data != NULL && address < 0x02000000 + sArm9.size) {
        *offsetOut = address - 0x02000000;
        return candidate == 0 ? &sArm9 : NULL;
    }
    for (u32 entry = 0; entry + 32 <= sOverlayTable.size; entry += 32) {
        u32 id = ReadU32(&sOverlayTable, entry);
        u32 ram = ReadU32(&sOverlayTable, entry + 4);
        u32 size = ReadU32(&sOverlayTable, entry + 8);
        u32 flags = ReadU32(&sOverlayTable, entry + 28);
        if (address < ram || address >= ram + size || (flags & 0x01000000) != 0) {
            continue;
        }
        const RomFile *file = GetFile((int)id);
        if (file == NULL || file->data == NULL || address - ram >= file->size) {
            continue;
        }
        if (candidate-- == 0) {
            *offsetOut = address - ram;
            return file;
        }
    }
    return NULL;
}

// The bytes at offset are pattern, where 0x100 in the pattern matches anything
static BOOL Matches(const RomFile *file, u32 offset, const u16 *pattern, int length)
{
    if (file == NULL || file->data == NULL || offset + length > file->size) {
        return FALSE;
    }
    for (int i = 0; i < length; i++) {
        if (pattern[i] != 0x100 && file->data[offset + i] != pattern[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

#define ANY 0x100

// --- tables ------------------------------------------------------------------------------

typedef struct RomTable {
    const char *name;
    u8 *table;
    size_t size;
    int file;
    u32 hint;
    const char *group;
    s64 found; // offset in the file, -1 if not found
} RomTable;

#define MAX_TABLES 64

static RomTable sTables[MAX_TABLES];
static int sTableCount;

void RomPatches_RegisterTable(const char *name, void *table, size_t size, int file, u32 hint, const char *group)
{
    if (sTableCount < MAX_TABLES) {
        sTables[sTableCount++] = (RomTable) { name, table, size, file, hint, group, -1 };
    }
}

static size_t SameBytes(const RomFile *file, u32 offset, const u8 *table, size_t size)
{
    size_t same = 0;
    for (size_t i = 0; i < size; i++) {
        same += file->data[offset + i] == table[i];
    }
    return same;
}

// Looks for a table that moved: chunks of the vanilla table vote for where it starts
static s64 SearchTable(const RomFile *file, const u8 *table, size_t size)
{
    enum { CHUNK = 12, SAMPLES = 24 };
    if (size < 64 || file->size < size) {
        return -1;
    }

    s64 candidates[SAMPLES * 4];
    int votes[SAMPLES * 4];
    int candidateCount = 0;

    for (int sample = 0; sample < SAMPLES; sample++) {
        size_t chunk = (size - CHUNK) * sample / (SAMPLES - 1);
        int hits = 0;
        for (u32 pos = 0; pos + CHUNK <= file->size && hits < 4; pos++) {
            if (file->data[pos] != table[chunk] || memcmp(file->data + pos, table + chunk, CHUNK) != 0) {
                continue;
            }
            hits++;
            s64 start = (s64)pos - (s64)chunk;
            if (start < 0 || start + size > file->size) {
                continue;
            }
            int i;
            for (i = 0; i < candidateCount && candidates[i] != start; i++) { }
            if (i == candidateCount) {
                if (candidateCount == (int)NELEMS(candidates)) {
                    continue;
                }
                candidates[candidateCount] = start;
                votes[candidateCount++] = 0;
            }
            votes[i]++;
        }
    }

    int best = -1;
    for (int i = 0; i < candidateCount; i++) {
        if (best < 0 || votes[i] > votes[best]) {
            best = i;
        }
    }
    return best >= 0 && votes[best] >= SAMPLES / 4 ? candidates[best] : -1;
}

static void ApplyTables(RomPatchesInfo *info)
{
    // Find each table where the US Rev 1 ROM has it, or search for it
    for (int i = 0; i < sTableCount; i++) {
        RomTable *t = &sTables[i];
        const RomFile *file = GetFile(t->file);
        if (file == NULL || file->data == NULL) {
            continue;
        }
        if (t->hint + t->size <= file->size && (t->group != NULL || SameBytes(file, t->hint, t->table, t->size) * 2 >= t->size)) {
            t->found = t->hint;
        } else if (t->group == NULL) {
            t->found = SearchTable(file, t->table, t->size);
        }
    }

    // Small tables are only trusted when their group as a whole is recognised
    for (int i = 0; i < sTableCount; i++) {
        RomTable *t = &sTables[i];
        if (t->group == NULL || t->found < 0) {
            continue;
        }
        size_t same = 0, total = 0;
        for (int j = 0; j < sTableCount; j++) {
            RomTable *u = &sTables[j];
            if (u->group != NULL && strcmp(u->group, t->group) == 0 && u->found >= 0) {
                same += SameBytes(GetFile(u->file), (u32)u->found, u->table, u->size);
                total += u->size;
            }
        }
        if (same * 10 < total * 7) {
            printf("ROM patches: %s: the %s tables are not where expected, left as they are\n", t->name, t->group);
            t->found = -1;
        }
    }

    for (int i = 0; i < sTableCount; i++) {
        RomTable *t = &sTables[i];
        if (t->found < 0) {
            continue;
        }
        const RomFile *file = GetFile(t->file);
        size_t differ = t->size - SameBytes(file, (u32)t->found, t->table, t->size);
        if (differ > 0) {
            memcpy(t->table, file->data + t->found, t->size);
            info->tablesChanged++;
            printf("ROM patches: %s from the ROM (%zu bytes differ)\n", t->name, differ);
        }
    }
}

// --- instructions --------------------------------------------------------------------

#define TYPE_CHART_MAX 512

static u8 sTypeChart[TYPE_CHART_MAX][3];
static BOOL sHasTypeChart;

// The type chart: rows of attacker, defender, multiplier, with 0xFE rows before the
// Foresight-only part and 0xFF at the end
static BOOL LoadTypeChart(const RomFile *file, u32 offset)
{
    int rows = 0;
    while (rows < TYPE_CHART_MAX && offset + 3 <= file->size) {
        const u8 *row = file->data + offset;
        memcpy(sTypeChart[rows++], row, 3);
        offset += 3;
        if (row[0] == 0xFF) {
            return rows > 20;
        }
        if (row[0] != 0xFE && (row[0] >= 18 || row[1] >= 18 || (row[2] != 0 && row[2] != 5 && row[2] != 10 && row[2] != 20))) {
            return FALSE;
        }
    }
    return FALSE;
}

static void ApplyInstructions(RomPatchesInfo *info)
{
    const RomFile *arm9 = GetFile(ROM_FILE_ARM9);
    const RomFile *battle = GetFile(16);
    const RomFile *pokedex = GetFile(21);
    const RomFile *intro = GetFile(73);

    // Pokemon_IsPersonalityShiny: eors r0, r1; eors r0, r2; cmp r0, #threshold; bhs
    static const u16 shiny[] = { 0x58, 0x40, 0x12, 0x0C, 0x48, 0x40, 0x50, 0x40, ANY, 0x28, 0x01, 0xD2 };
    if (Matches(arm9, 0x75E48, shiny, NELEMS(shiny))) {
        info->shinyThreshold = arm9->data[0x75E50];
    }

    // BattleControllerPlayer_CheckObedience: TrainerInfo_BadgeCount() >= 8 -> obeys
    static const u16 obedience[] = { 0x0A, 0x98, 0xD7, 0xF5, 0xC4, 0xFE, ANY, 0x28, 0x02, 0xDB };
    if (Matches(battle, 0x1308A, obedience, NELEMS(obedience))) {
        info->obeyAllBadges = battle->data[0x13090];
    }

    // The type chart's address in a literal pool of the battle overlay (vanilla: its own
    // sTypeMatchupMultipliers; Renegade Platinum: a bigger chart appended to the Pokedex
    // overlay, which battles keep loaded)
    static const u16 chartPool[] = { 0x3C, 0x21, 0x00, 0x00, 0xCC, 0x2D, 0x00, 0x00, ANY, ANY, ANY, 0x02, 0xB0, 0x2D, 0x00, 0x00 };
    if (Matches(battle, 0x1A184, chartPool, NELEMS(chartPool))) {
        u32 address = ReadU32(battle, 0x1A18C);
        u32 offset;
        const RomFile *file;
        for (int candidate = 0; address != 0x0226ECD4 && !sHasTypeChart && (file = FileOfAddress(address, candidate, &offset)) != NULL; candidate++) {
            sHasTypeChart = LoadTypeChart(file, offset);
        }
        if (sHasTypeChart) {
            for (int i = 0; sTypeChart[i][0] != 0xFF; i++) {
                if (sTypeChart[i][0] != 0xFE && (sTypeChart[i][0] == 9 || sTypeChart[i][1] == 9) && sTypeChart[i][2] != 10) {
                    info->extraType = TRUE;
                }
            }
            printf("ROM patches: the ROM's own type chart%s\n", info->extraType ? ", with type 9 as a real type" : "");
        }
    }

    // PokedexGraphics_GetAnimIDfromType: Renegade Platinum's jump table gives type 9 its
    // own icon. Case 9's target is a "movs r0, #icon"
    static const u16 pokedexSwitch[] = { 0x11, 0x28, 0x38, 0xD8, 0x01, 0x18, 0x79, 0x44, 0x49, 0x88, 0x8F, 0x44 };
    if (info->extraType && Matches(pokedex, 0xE400, pokedexSwitch, NELEMS(pokedexSwitch))) {
        u32 tableOffset = 0xE40C; // halfword offsets from the add pc's pc (0xE40E)
        u32 target = 0xE40E + ReadU16(pokedex, tableOffset + 9 * 2);
        if (target + 2 <= pokedex->size && pokedex->data[target + 1] == 0x20) {
            info->extraTypePokedexIcon = pokedex->data[target];
        }
    }

    // Professor Rowan's introduction: the Pokemon's species in two literal pools
    static const u16 introPool1[] = { 0x08, 0xB0, 0xF8, 0xBD, ANY, ANY, 0x00, 0x00, 0xD4, 0x39, 0x1D, 0x02 };
    static const u16 introPool2[] = { 0xF8, 0xBD, 0xC0, 0x46, ANY, ANY, 0x00, 0x00, 0x01, 0x4B, 0x02, 0x20 };
    if (Matches(intro, 0xF50, introPool1, NELEMS(introPool1)) && Matches(intro, 0x1584, introPool2, NELEMS(introPool2))) {
        u16 species = ReadU16(intro, 0xF54);
        if (species == ReadU16(intro, 0x1588) && species > 0 && species <= NATIONAL_DEX_COUNT) {
            info->introSpecies = species;
        }
    }
}

// --- public ------------------------------------------------------------------------------

static RomPatchesInfo sInfo = {
    .shinyThreshold = 8,
    .obeyAllBadges = 8,
    .extraTypePokedexIcon = 7,
    .introSpecies = SPECIES_BUNEARY,
};

void RomPatches_Apply(void)
{
    if (sInfo.applied) {
        return;
    }
    if (GetFile(ROM_FILE_ARM9)->data == NULL) {
        printf("ROM patches: no arm9.bin, using the built-in tables\n");
        return;
    }
    sInfo.applied = TRUE;

    ApplyTables(&sInfo);
    ApplyInstructions(&sInfo);

    sInfo.modified = sInfo.tablesChanged > 0 || sInfo.shinyThreshold != 8 || sInfo.obeyAllBadges != 8
        || sHasTypeChart || sInfo.introSpecies != SPECIES_BUNEARY;
    sInfo.renegadePlatinum = sInfo.extraType;

    if (sInfo.modified) {
        printf("ROM patches: shiny below %d, every Pokemon obeys from %d badges, intro Pokemon %d%s\n",
            sInfo.shinyThreshold, sInfo.obeyAllBadges, sInfo.introSpecies,
            sInfo.renegadePlatinum ? " (looks like Renegade Platinum)" : "");
    }

    // The code files are only needed once
    free(sArm9.data);
    sArm9.data = NULL;
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        free(sOverlays[i].data);
        sOverlays[i].data = NULL;
    }
    free(sOverlayTable.data);
    sOverlayTable.data = NULL;
}

const RomPatchesInfo *RomPatches_Info(void)
{
    return &sInfo;
}

const u8 (*RomPatches_TypeChart(const u8 (*vanillaChart)[3]))[3]
{
    return sHasTypeChart ? (const u8(*)[3])sTypeChart : vanillaChart;
}

// --- naming a ROM ------------------------------------------------------------------------

static BOOL ReadFileAt(const char *path, u32 offset, void *out, u32 size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return FALSE;
    }
    BOOL ok = fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
    fclose(f);
    return ok;
}

// Types of one species from a pl_personal.narc (NARC: BTAF file table, BTNF, GMIF data)
static BOOL ReadSpeciesTypes(const char *path, int species, u8 types[2])
{
    u8 header[28];
    if (!ReadFileAt(path, 0, header, sizeof(header)) || memcmp(header, "NARC", 4) != 0 || memcmp(header + 16, "BTAF", 4) != 0) {
        return FALSE;
    }
    u32 btafSize = header[20] | (header[21] << 8) | (header[22] << 16) | ((u32)header[23] << 24);
    u8 entry[8], btnf[8];
    if (!ReadFileAt(path, 28 + species * 8, entry, 8) || !ReadFileAt(path, 16 + btafSize, btnf, 8)) {
        return FALSE;
    }
    u32 start = entry[0] | (entry[1] << 8) | (entry[2] << 16) | ((u32)entry[3] << 24);
    u32 btnfSize = btnf[4] | (btnf[5] << 8) | (btnf[6] << 16) | ((u32)btnf[7] << 24);
    u32 gmifData = 16 + btafSize + btnfSize + 8;
    return ReadFileAt(path, gmifData + start + 6, types, 2);
}

void RomPatches_DescribeRom(const char *dir, char *name, int nameSize)
{
    char path[1024];
    u8 pool[16];

    // Renegade Platinum moves the battle type chart out of the battle overlay
    snprintf(path, sizeof(path), "%s/overlay9/overlay9_16.bin", dir);
    BOOL renegade = ReadFileAt(path, 0x1A184, pool, sizeof(pool)) && memcmp(pool, "\x3C\x21\x00\x00\xCC\x2D\x00\x00", 8) == 0
        && (pool[8] | (pool[9] << 8) | (pool[10] << 16) | ((u32)pool[11] << 24)) != 0x0226ECD4;

    if (renegade) {
        // Classic keeps the official typings: Charizard stays Fire/Flying (Dragon otherwise).
        // With both versions (versions.ini, chosen per save) it's just Renegade Platinum.
        u8 types[2];
        snprintf(path, sizeof(path), "%s/versions.ini", dir);
        FILE *versions = fopen(path, "r");
        if (versions != NULL) {
            fclose(versions);
            snprintf(name, nameSize, "Renegade Platinum");
            return;
        }
        snprintf(path, sizeof(path), "%s/poketool/personal/pl_personal.narc", dir);
        BOOL classic = ReadSpeciesTypes(path, SPECIES_CHARIZARD, types) && types[1] == 2;
        snprintf(name, nameSize, "%s", classic ? "Renegade Platinum Classic" : "Renegade Platinum");
        return;
    }

    u8 shiny[12];
    snprintf(path, sizeof(path), "%s/arm9.bin", dir);
    // (no arm9.bin: game files from a desktop build's output, i.e. vanilla)
    BOOL vanillaShiny = !ReadFileAt(path, 0x75E48, shiny, sizeof(shiny)) || shiny[8] == 0x08;
    snprintf(name, nameSize, "%s", vanillaShiny ? "Pokemon Platinum" : "Pokemon Platinum (modified)");
}

// libntr hook: names the games in the game picker
void SIM_Prj_CartName(const char *dir, char *name, int nameSize)
{
    RomPatches_DescribeRom(dir, name, nameSize);
    // A randomized game: its seed tells it apart (and names its save sync folder)
    u32 seed;
    if (Randomizer_FolderSeed(dir, &seed)) {
        const size_t used = strlen(name);
        snprintf(name + used, nameSize - used, " (randomized %u)", (unsigned)seed);
    }
}
