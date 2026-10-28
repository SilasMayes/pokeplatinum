// The randomizer. See include/port/randomizer.h
//
// SIM_Prj_Randomize runs on libntr's worker thread while the "Add a game" screen builds a
// game: it only reads and writes the files in the new game's folder (no game state, no
// heap, nothing of the game being played), with its own deterministic random numbers, so a
// seed gives the same game on every device.
#include "port/randomizer.h"

#include <nitro.h>
#include <simulator/sim_carts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/charcode.h"
#include "constants/species.h"
#include "generated/map_headers.h"
#include "generated/text_banks.h"

#include "charcode_convert.h"

#include "map_header.h"

#define SETTINGS_FILE "randomizer.ini"
#define LOG_FILE "randomizer_log.txt"
#define NAME_LEN 24

static const SIM_RandomizerOption sOptions[RANDOMIZER_OPTION_COUNT] = {
    [RANDOMIZER_WILD] = { "Wild Pokemon", "Each area's Pokemon become others, the same ones all over the area, so how "
                                          "often each shows up stays the same.", TRUE },
    [RANDOMIZER_TRAINERS] = { "Trainers' Pokemon", "Their levels stay. Trainers who chose moves get the new Pokemon's "
                                                   "latest moves.", TRUE },
    [RANDOMIZER_STARTERS] = { "Starters", "Three Pokemon that evolve twice, in place of Turtwig, Chimchar and Piplup. "
                                          "Your rival and Dawn/Lucas take the other two.", TRUE },
    [RANDOMIZER_SIMILAR_STRENGTH] = { "Similar strength", "Each Pokemon becomes one with about the same base stats, so "
                                                          "the difficulty stays about the same.", TRUE },
    [RANDOMIZER_KEEP_TYPE] = { "Keep a type", "Each Pokemon becomes one sharing a type with it (gym leaders keep their "
                                              "theme). The starters stay Grass, Fire and Water.", FALSE },
    [RANDOMIZER_NO_LEGENDARIES] = { "No legendaries", "Legendary and mythical Pokemon don't show up.", TRUE },
};

static const u16 sLegendaries[] = {
    SPECIES_ARTICUNO, SPECIES_ZAPDOS, SPECIES_MOLTRES, SPECIES_MEWTWO, SPECIES_MEW, SPECIES_RAIKOU, SPECIES_ENTEI,
    SPECIES_SUICUNE, SPECIES_LUGIA, SPECIES_HO_OH, SPECIES_CELEBI, SPECIES_REGIROCK, SPECIES_REGICE, SPECIES_REGISTEEL,
    SPECIES_LATIAS, SPECIES_LATIOS, SPECIES_KYOGRE, SPECIES_GROUDON, SPECIES_RAYQUAZA, SPECIES_JIRACHI, SPECIES_DEOXYS,
    SPECIES_UXIE, SPECIES_MESPRIT, SPECIES_AZELF, SPECIES_DIALGA, SPECIES_PALKIA, SPECIES_HEATRAN, SPECIES_REGIGIGAS,
    SPECIES_GIRATINA, SPECIES_CRESSELIA, SPECIES_PHIONE, SPECIES_MANAPHY, SPECIES_DARKRAI, SPECIES_SHAYMIN,
    SPECIES_ARCEUS,
};

static const u16 sVanillaStarters[3] = { SPECIES_TURTWIG, SPECIES_CHIMCHAR, SPECIES_PIPLUP };

int SIM_Prj_RandomizerOptions(const SIM_RandomizerOption **options)
{
    *options = sOptions;
    return RANDOMIZER_OPTION_COUNT;
}

// --- random numbers (the same on every device) ----------------------------------------------

typedef struct Rng {
    u64 state;
} Rng;

static u32 Rng_Next(Rng *rng)
{
    // splitmix64
    u64 z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    return (u32)(z >> 32);
}

static u32 Rng_Below(Rng *rng, u32 count)
{
    return count > 0 ? Rng_Next(rng) % count : 0;
}

// --- NARC files, edited in memory and written back whole ------------------------------------

typedef struct Narc {
    u8 *data;
    size_t size;
    u32 count;
    u32 fatb;
    u32 fimg;
} Narc;

static void JoinPath(char *out, size_t outSize, const char *dir, const char *relative)
{
    snprintf(out, outSize, "%s/%s", dir, relative);
}

static BOOL Narc_Load(Narc *narc, const char *dir, const char *relative)
{
    memset(narc, 0, sizeof(*narc));
    char path[1200];
    JoinPath(path, sizeof(path), dir, relative);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return FALSE;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 16 || size > 64 * 1024 * 1024) {
        fclose(file);
        return FALSE;
    }
    narc->data = malloc((size_t)size);
    if (narc->data == NULL || fread(narc->data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(narc->data);
        narc->data = NULL;
        return FALSE;
    }
    fclose(file);
    narc->size = (size_t)size;

    const u8 *d = narc->data;
    if (memcmp(d, "NARC", 4) != 0) {
        free(narc->data);
        narc->data = NULL;
        return FALSE;
    }
    narc->fatb = d[12] | (d[13] << 8);
    if (narc->fatb + 12 > narc->size) {
        free(narc->data);
        narc->data = NULL;
        return FALSE;
    }
    const u32 fatbSize = d[narc->fatb + 4] | (d[narc->fatb + 5] << 8) | (d[narc->fatb + 6] << 16) | ((u32)d[narc->fatb + 7] << 24);
    narc->count = d[narc->fatb + 8] | (d[narc->fatb + 9] << 8);
    const u32 fntb = narc->fatb + fatbSize;
    if (fntb + 8 > narc->size) {
        free(narc->data);
        narc->data = NULL;
        return FALSE;
    }
    const u32 fntbSize = d[fntb + 4] | (d[fntb + 5] << 8) | (d[fntb + 6] << 16) | ((u32)d[fntb + 7] << 24);
    narc->fimg = fntb + fntbSize + 8;
    return TRUE;
}

static u8 *Narc_Member(const Narc *narc, u32 index, u32 *size)
{
    *size = 0;
    if (narc->data == NULL || index >= narc->count) {
        return NULL;
    }
    const u8 *entry = narc->data + narc->fatb + 12 + index * 8;
    const u32 start = entry[0] | (entry[1] << 8) | (entry[2] << 16) | ((u32)entry[3] << 24);
    const u32 end = entry[4] | (entry[5] << 8) | (entry[6] << 16) | ((u32)entry[7] << 24);
    if (end < start || narc->fimg + (size_t)end > narc->size) {
        return NULL;
    }
    *size = end - start;
    return narc->data + narc->fimg + start;
}

static BOOL Narc_Save(const Narc *narc, const char *dir, const char *relative)
{
    char path[1200], tmpPath[1210];
    JoinPath(path, sizeof(path), dir, relative);
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    FILE *file = fopen(tmpPath, "wb");
    if (file == NULL) {
        return FALSE;
    }
    BOOL ok = fwrite(narc->data, 1, narc->size, file) == narc->size;
    ok = (fclose(file) == 0) && ok;
    if (!ok) {
        remove(tmpPath);
        return FALSE;
    }
    remove(path);
    if (rename(tmpPath, path) != 0) {
        remove(tmpPath);
        return FALSE;
    }
    return TRUE;
}

static void Narc_Free(Narc *narc)
{
    free(narc->data);
    memset(narc, 0, sizeof(*narc));
}

static u16 Get16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static void Put16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static u32 Get32(const u8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static void Put32(u8 *p, u32 value)
{
    Put16(p, (u16)value);
    Put16(p + 2, (u16)(value >> 16));
}

// --- names, from the game's text (for the log) ------------------------------------------------

typedef struct Names {
    int count;
    char (*names)[NAME_LEN];
} Names;

static void TitleCase(char *text)
{
    BOOL start = TRUE;
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'A' && *c <= 'Z' && !start) {
            *c = (char)(*c - 'A' + 'a');
        }
        start = !((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '\'');
    }
}

// A message bank of pl_msg.narc decoded to ASCII (as message.c decodes it)
static void LoadNames(const Narc *msg, int bank, Names *out, BOOL titleCase)
{
    memset(out, 0, sizeof(*out));
    u32 size;
    const u8 *data = Narc_Member(msg, (u32)bank, &size);
    if (data == NULL || size < 4) {
        return;
    }
    const int count = Get16(data);
    const u16 seed = Get16(data + 2);
    if (4 + (u32)count * 8 > size) {
        return;
    }
    out->names = calloc((size_t)count, NAME_LEN);
    if (out->names == NULL) {
        return;
    }
    out->count = count;
    for (int i = 0; i < count; i++) {
        u32 key = (seed * 765u * (u32)(i + 1)) & 0xFFFF;
        key |= key << 16;
        const u32 offset = Get32(data + 4 + i * 8) ^ key;
        const u32 length = Get32(data + 4 + i * 8 + 4) ^ key;
        if (offset + length * 2 > size || length > 64) {
            continue;
        }
        u16 charKey = (u16)((u32)(i + 1) * 596947u);
        u16 raw[64];
        u32 rawLength = 0;
        for (u32 j = 0; j < length && rawLength < 64; j++) {
            raw[rawLength++] = Get16(data + offset + j * 2) ^ charKey;
            charKey = (u16)(charKey + 18749);
        }
        // Trainers' names are packed: 9 bits a character, 15 bits a word, after 0xF100
        // (String_ConcatTrainerName)
        u16 chars[64];
        u32 charCount = 0;
        if (rawLength > 0 && raw[0] == 0xF100) {
            u32 buffer = 0;
            int bits = 0;
            for (u32 j = 1; j < rawLength && charCount < 63; j++) {
                buffer |= (u32)(raw[j] & 0x7FFF) << bits;
                bits += 15;
                while (bits >= 9 && charCount < 63) {
                    const u16 c = buffer & 0x1FF;
                    buffer >>= 9;
                    bits -= 9;
                    if (c == 0x1FF) {
                        j = rawLength;
                        break;
                    }
                    chars[charCount++] = c;
                }
            }
        } else {
            memcpy(chars, raw, rawLength * sizeof(u16));
            charCount = rawLength;
        }
        int pos = 0;
        for (u32 j = 0; j < charCount && pos < NAME_LEN - 3; j++) {
            const u16 c = chars[j];
            if (c == CHAR_EOS) {
                break;
            }
            if (c == CHAR_MALE || c == CHAR_FEMALE || c == CHAR_WIDE_MALE || c == CHAR_WIDE_FEMALE) {
                out->names[i][pos++] = ' ';
                out->names[i][pos++] = c == CHAR_MALE || c == CHAR_WIDE_MALE ? 'M' : 'F';
                continue;
            }
            if (c == 0x1E0 || c == 0x1E1) { // the PK and MN of PKMN
                out->names[i][pos++] = c == 0x1E0 ? 'P' : 'M';
                out->names[i][pos++] = c == 0x1E0 ? 'K' : 'N';
                continue;
            }
            if (c == 0x1AE) {
                out->names[i][pos++] = '.';
                continue;
            }
            if (c >= 0x1E0) {
                continue; // formatting
            }
            char ascii = 0;
            charcode_t code = c;
            CharCode_ToAsciiString(&code, &ascii, 1);
            if (ascii != 0) {
                out->names[i][pos++] = ascii;
            }
        }
        out->names[i][pos] = '\0';
        if (titleCase) {
            TitleCase(out->names[i]);
        }
    }
}

static const char *NameOf(const Names *names, int index)
{
    return index >= 0 && index < names->count && names->names[index][0] != '\0' ? names->names[index] : "?";
}

static void FreeNames(Names *names)
{
    free(names->names);
    memset(names, 0, sizeof(*names));
}

// --- the Pokemon ---------------------------------------------------------------------------------

typedef struct Context {
    Rng rng;
    u32 options;
    int bst[NATIONAL_DEX_COUNT + 1];
    u8 types[NATIONAL_DEX_COUNT + 1][2];
    BOOL allowed[NATIONAL_DEX_COUNT + 1];   // may be picked
    BOOL hasPreEvolution[NATIONAL_DEX_COUNT + 1];
    u16 evolvesInto[NATIONAL_DEX_COUNT + 1]; // the first evolution (0: none)
    Narc learnsets;                          // wotbl.narc
    u16 candidates[NATIONAL_DEX_COUNT + 1];
} Context;

static BOOL OptionOn(const Context *ctx, int option)
{
    return (ctx->options & (1u << option)) != 0;
}

static BOOL IsSpecies(u32 species)
{
    return species >= 1 && species <= NATIONAL_DEX_COUNT;
}

static BOOL ShareType(const Context *ctx, u16 a, u16 b)
{
    return ctx->types[a][0] == ctx->types[b][0] || ctx->types[a][0] == ctx->types[b][1]
        || ctx->types[a][1] == ctx->types[b][0] || ctx->types[a][1] == ctx->types[b][1];
}

static BOOL LoadSpecies(Context *ctx, const char *dir)
{
    Narc personal, evolutions;
    if (!Narc_Load(&personal, dir, "poketool/personal/pl_personal.narc")) {
        return FALSE;
    }
    if (!Narc_Load(&evolutions, dir, "poketool/personal/evo.narc")) {
        Narc_Free(&personal);
        return FALSE;
    }
    for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
        u32 size;
        const u8 *data = Narc_Member(&personal, (u32)species, &size);
        if (data == NULL || size < 8) {
            continue;
        }
        ctx->bst[species] = data[0] + data[1] + data[2] + data[3] + data[4] + data[5];
        ctx->types[species][0] = data[6];
        ctx->types[species][1] = data[7];
        ctx->allowed[species] = ctx->bst[species] > 0;
    }
    for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
        u32 size;
        const u8 *data = Narc_Member(&evolutions, (u32)species, &size);
        for (u32 at = 0; data != NULL && at + 6 <= size && at < 7 * 6; at += 6) {
            const u16 method = Get16(data + at);
            const u16 target = Get16(data + at + 4);
            if (method != 0 && IsSpecies(target)) {
                ctx->hasPreEvolution[target] = TRUE;
                if (ctx->evolvesInto[species] == 0) {
                    ctx->evolvesInto[species] = target;
                }
            }
        }
    }
    Narc_Free(&personal);
    Narc_Free(&evolutions);

    // Unown's letter comes from the area: never one picked at random
    ctx->allowed[SPECIES_UNOWN] = FALSE;
    if (OptionOn(ctx, RANDOMIZER_NO_LEGENDARIES)) {
        for (size_t i = 0; i < sizeof(sLegendaries) / sizeof(sLegendaries[0]); i++) {
            ctx->allowed[sLegendaries[i]] = FALSE;
        }
    }
    return TRUE;
}

// A Pokemon in place of original: about as strong and sharing a type when asked for
static u16 Replacement(Context *ctx, u16 original)
{
    if (!IsSpecies(original)) {
        return original;
    }
    const BOOL similar = OptionOn(ctx, RANDOMIZER_SIMILAR_STRENGTH) && ctx->bst[original] > 0;
    static const int kWindows[] = { 10, 15, 20, 30, 50, 1000 };
    for (int typed = OptionOn(ctx, RANDOMIZER_KEEP_TYPE) ? 1 : 0; typed >= 0; typed--) {
        for (size_t w = 0; w < sizeof(kWindows) / sizeof(kWindows[0]); w++) {
            int count = 0;
            for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
                if (!ctx->allowed[species] || species == original) {
                    continue;
                }
                if (typed && !ShareType(ctx, (u16)species, original)) {
                    continue;
                }
                if (similar) {
                    const int diff = abs(ctx->bst[species] - ctx->bst[original]);
                    if (diff * 100 > ctx->bst[original] * kWindows[w]) {
                        continue;
                    }
                }
                ctx->candidates[count++] = (u16)species;
            }
            if (count >= 4 || (count > 0 && (!similar || kWindows[w] >= 1000))) {
                return ctx->candidates[Rng_Below(&ctx->rng, (u32)count)];
            }
            if (!similar) {
                break;
            }
        }
    }
    return original;
}

// --- wild Pokemon ----------------------------------------------------------------------------------

// Where the species are in an encounter table (overlay006/wild_encounters.h's WildEncounters)
static int EncounterSpeciesOffsets(int offsets[64])
{
    int count = 0;
    for (int i = 0; i < 12; i++) {
        offsets[count++] = 8 + i * 8; // grass
    }
    for (int i = 0; i < 2 + 2 + 2 + 4; i++) {
        offsets[count++] = 100 + i * 4; // swarm, day, night, Poke Radar
    }
    for (int i = 0; i < 10; i++) {
        offsets[count++] = 164 + i * 4; // dual-slot (GBA games)
    }
    for (int table = 0; table < 5; table++) {
        for (int i = 0; i < 5; i++) {
            offsets[count++] = 204 + table * 44 + 8 + i * 8; // surfing and the rods
        }
    }
    return count;
}

#define ENCOUNTER_TABLE_SIZE 424

static void LogAreaName(FILE *log, int table, const Names *locations)
{
    const char *names[2] = { NULL, NULL };
    for (int header = 0; header < MAP_HEADER_COUNT; header++) {
        if (!MapHeader_HasWildEncounters(header) || (int)MapHeader_GetWildEncountersArchiveID(header) != table) {
            continue;
        }
        const char *name = NameOf(locations, (int)MapHeader_GetMapLabelTextID(header));
        if (names[0] == NULL) {
            names[0] = name;
        } else if (names[1] == NULL && strcmp(names[0], name) != 0) {
            names[1] = name;
            break;
        }
    }
    if (names[0] == NULL) {
        fprintf(log, "Area %d", table);
    } else if (names[1] == NULL) {
        fprintf(log, "%s", names[0]);
    } else {
        fprintf(log, "%s, %s", names[0], names[1]);
    }
}

static BOOL RandomizeWild(Context *ctx, const char *dir, FILE *log, const Names *species, const Names *locations)
{
    Narc narc;
    if (!Narc_Load(&narc, dir, "fielddata/encountdata/pl_enc_data.narc")) {
        return FALSE;
    }
    int offsets[64];
    const int offsetCount = EncounterSpeciesOffsets(offsets);
    fprintf(log, "\nWILD POKEMON\n");
    for (u32 table = 0; table < narc.count; table++) {
        u32 size;
        u8 *data = Narc_Member(&narc, table, &size);
        if (data == NULL || size < ENCOUNTER_TABLE_SIZE) {
            continue;
        }
        // One for one within the area
        u16 from[64], to[64];
        int mapped = 0;
        for (int i = 0; i < offsetCount; i++) {
            const u32 value = Get32(data + offsets[i]);
            const u16 original = (u16)(value & 0x3FF);
            if (!IsSpecies(original)) {
                continue;
            }
            int found = -1;
            for (int m = 0; m < mapped; m++) {
                if (from[m] == original) {
                    found = m;
                }
            }
            if (found < 0) {
                found = mapped++;
                from[found] = original;
                to[found] = Replacement(ctx, original);
            }
            Put32(data + offsets[i], to[found]);
        }
        if (mapped > 0) {
            LogAreaName(log, (int)table, locations);
            fprintf(log, ":");
            for (int m = 0; m < mapped; m++) {
                fprintf(log, "%s %s > %s", m > 0 ? "," : "", NameOf(species, from[m]), NameOf(species, to[m]));
            }
            fprintf(log, "\n");
        }
    }
    BOOL ok = Narc_Save(&narc, dir, "fielddata/encountdata/pl_enc_data.narc");
    Narc_Free(&narc);
    return ok;
}

// --- trainers -------------------------------------------------------------------------------------

// The latest four moves species learns by level (wotbl.narc: move | level << 9, to 0xFFFF)
static void LevelUpMoves(Context *ctx, u16 species, int level, u16 moves[4])
{
    memset(moves, 0, sizeof(u16) * 4);
    u32 size;
    const u8 *data = Narc_Member(&ctx->learnsets, species, &size);
    int count = 0;
    for (u32 at = 0; data != NULL && at + 2 <= size; at += 2) {
        const u16 entry = Get16(data + at);
        if (entry == 0xFFFF) {
            break;
        }
        const u16 move = entry & 0x1FF;
        const int learnLevel = entry >> 9;
        if (learnLevel > level || move == 0) {
            continue;
        }
        BOOL known = FALSE;
        for (int i = 0; i < count && i < 4; i++) {
            if (moves[i] == move) {
                known = TRUE;
            }
        }
        if (known) {
            continue;
        }
        if (count < 4) {
            moves[count++] = move;
        } else {
            memmove(moves, moves + 1, sizeof(u16) * 3);
            moves[3] = move;
        }
    }
}

static BOOL RandomizeTrainers(Context *ctx, const char *dir, FILE *log, const Names *species, const Names *trainerNames,
    const Names *classNames)
{
    Narc headers, parties;
    if (!Narc_Load(&headers, dir, "poketool/trainer/trdata.narc")) {
        return FALSE;
    }
    if (!Narc_Load(&parties, dir, "poketool/trainer/trpoke.narc")) {
        Narc_Free(&headers);
        return FALSE;
    }
    if (!Narc_Load(&ctx->learnsets, dir, "poketool/personal/wotbl.narc")) {
        Narc_Free(&headers);
        Narc_Free(&parties);
        return FALSE;
    }
    // struct_defs/trainer_data.h: bytes per Pokemon and where the moves are, by data type
    static const int kStride[4] = { 8, 16, 10, 18 };
    static const int kMovesAt[4] = { -1, 6, -1, 8 };

    fprintf(log, "\nTRAINERS\n");
    for (u32 trainer = 1; trainer < headers.count && trainer < parties.count; trainer++) {
        u32 headerSize, partySize;
        const u8 *header = Narc_Member(&headers, trainer, &headerSize);
        u8 *party = Narc_Member(&parties, trainer, &partySize);
        if (header == NULL || party == NULL || headerSize < 4) {
            continue;
        }
        const int type = header[0];
        const int count = header[3];
        if (type < 0 || type > 3 || count <= 0 || count > 6 || (u32)(count * kStride[type]) > partySize) {
            continue;
        }
        fprintf(log, "%s %s:", NameOf(classNames, header[1]), NameOf(trainerNames, (int)trainer));
        for (int i = 0; i < count; i++) {
            u8 *mon = party + i * kStride[type];
            const u16 value = Get16(mon + 4);
            const u16 original = value & 0x3FF;
            const int level = Get16(mon + 2);
            if (!IsSpecies(original)) {
                continue;
            }
            const u16 replacement = Replacement(ctx, original);
            Put16(mon + 4, replacement); // the original's form doesn't go with the new one
            if (kMovesAt[type] >= 0) {
                u16 moves[4];
                LevelUpMoves(ctx, replacement, level, moves);
                for (int m = 0; m < 4; m++) {
                    Put16(mon + kMovesAt[type] + m * 2, moves[m]);
                }
            }
            fprintf(log, "%s Lv%d %s > %s", i > 0 ? "," : "", level, NameOf(species, original), NameOf(species, replacement));
        }
        fprintf(log, "\n");
    }
    BOOL ok = Narc_Save(&parties, dir, "poketool/trainer/trpoke.narc");
    Narc_Free(&headers);
    Narc_Free(&parties);
    Narc_Free(&ctx->learnsets);
    return ok;
}

// --- starters ------------------------------------------------------------------------------------

static BOOL EvolvesTwice(const Context *ctx, u16 species)
{
    const u16 next = ctx->evolvesInto[species];
    return !ctx->hasPreEvolution[species] && next != 0 && IsSpecies(next) && ctx->evolvesInto[next] != 0;
}

static void PickStarters(Context *ctx, u16 starters[3])
{
    for (int slot = 0; slot < 3; slot++) {
        starters[slot] = sVanillaStarters[slot];
        for (int pass = 0; pass < 3; pass++) {
            // Evolving twice and (asked for) the vanilla one's type; then less picky
            int count = 0;
            for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
                if (!ctx->allowed[species] || (pass < 2 && !EvolvesTwice(ctx, (u16)species))
                    || (pass == 2 && ctx->hasPreEvolution[species])) {
                    continue;
                }
                if (pass == 0 && OptionOn(ctx, RANDOMIZER_KEEP_TYPE)
                    && !ShareType(ctx, (u16)species, sVanillaStarters[slot])) {
                    continue;
                }
                BOOL taken = FALSE;
                for (int other = 0; other < slot; other++) {
                    taken |= starters[other] == species;
                }
                if (!taken) {
                    ctx->candidates[count++] = (u16)species;
                }
            }
            if (count > 0) {
                starters[slot] = ctx->candidates[Rng_Below(&ctx->rng, (u32)count)];
                break;
            }
        }
    }
}

// --- the hook ------------------------------------------------------------------------------------

BOOL SIM_Prj_Randomize(const char *dir, u32 seed, u32 options, char *summary, int summarySize, char *error, int errorSize)
{
    summary[0] = '\0';
    error[0] = '\0';
    Context *ctx = calloc(1, sizeof(Context));
    if (ctx == NULL) {
        snprintf(error, errorSize, "Out of memory.");
        return FALSE;
    }
    ctx->rng.state = 0x504C41544E554DULL ^ ((u64)seed << 17) ^ seed; // "PLATNUM"
    ctx->options = options;
    if (!LoadSpecies(ctx, dir)) {
        free(ctx);
        snprintf(error, errorSize, "The game's Pokemon data couldn't be read.");
        return FALSE;
    }

    Narc msg;
    Names speciesNames, locationNames, trainerNames, classNames;
    memset(&speciesNames, 0, sizeof(speciesNames));
    memset(&locationNames, 0, sizeof(locationNames));
    memset(&trainerNames, 0, sizeof(trainerNames));
    memset(&classNames, 0, sizeof(classNames));
    if (Narc_Load(&msg, dir, "msgdata/pl_msg.narc")) {
        LoadNames(&msg, TEXT_BANK_SPECIES_NAME, &speciesNames, TRUE);
        LoadNames(&msg, TEXT_BANK_LOCATION_NAMES, &locationNames, FALSE);
        LoadNames(&msg, TEXT_BANK_NPC_TRAINER_NAMES, &trainerNames, FALSE);
        LoadNames(&msg, TEXT_BANK_TRAINER_CLASS_NAMES, &classNames, FALSE);
        Narc_Free(&msg);
    }

    char path[1200];
    JoinPath(path, sizeof(path), dir, LOG_FILE);
    FILE *log = fopen(path, "w");
    if (log == NULL) {
        free(ctx);
        snprintf(error, errorSize, "There's no room to write the game's files.");
        return FALSE;
    }
    fprintf(log, "Randomized with seed %u\nOptions:", (unsigned)seed);
    for (int i = 0; i < RANDOMIZER_OPTION_COUNT; i++) {
        if (options & (1u << i)) {
            fprintf(log, " [%s]", sOptions[i].label);
        }
    }
    fprintf(log, "\n");

    // The order is part of the seed: always starters, wild, trainers
    BOOL ok = TRUE;
    u16 starters[3] = { 0, 0, 0 };
    if (options & (1u << RANDOMIZER_STARTERS)) {
        PickStarters(ctx, starters);
        fprintf(log, "\nSTARTERS\n");
        for (int i = 0; i < 3; i++) {
            fprintf(log, "%s > %s\n", NameOf(&speciesNames, sVanillaStarters[i]), NameOf(&speciesNames, starters[i]));
        }
    }
    if (ok && (options & (1u << RANDOMIZER_WILD))) {
        ok = RandomizeWild(ctx, dir, log, &speciesNames, &locationNames);
        if (!ok) {
            snprintf(error, errorSize, "The wild Pokemon couldn't be randomized.");
        }
    }
    if (ok && (options & (1u << RANDOMIZER_TRAINERS))) {
        ok = RandomizeTrainers(ctx, dir, log, &speciesNames, &trainerNames, &classNames);
        if (!ok) {
            snprintf(error, errorSize, "The trainers couldn't be randomized.");
        }
    }
    fclose(log);
    FreeNames(&speciesNames);
    FreeNames(&locationNames);
    FreeNames(&trainerNames);
    FreeNames(&classNames);
    free(ctx);
    if (!ok) {
        return FALSE;
    }

    JoinPath(path, sizeof(path), dir, SETTINGS_FILE);
    FILE *settings = fopen(path, "w");
    if (settings == NULL) {
        snprintf(error, errorSize, "There's no room to write the game's files.");
        return FALSE;
    }
    fprintf(settings, "[Randomizer]\nSeed=%u\nOptions=%u\n", (unsigned)seed, (unsigned)options);
    if (starters[0] != 0) {
        fprintf(settings, "Starters=%u,%u,%u\n", starters[0], starters[1], starters[2]);
    }
    fclose(settings);

    // "Randomized: wild, trainers, starters"
    int used = snprintf(summary, summarySize, "Randomized");
    const char *parts[3] = { NULL, NULL, NULL };
    int partCount = 0;
    if (options & (1u << RANDOMIZER_WILD)) {
        parts[partCount++] = "wild";
    }
    if (options & (1u << RANDOMIZER_TRAINERS)) {
        parts[partCount++] = "trainers";
    }
    if (options & (1u << RANDOMIZER_STARTERS)) {
        parts[partCount++] = "starters";
    }
    for (int i = 0; i < partCount && used < summarySize; i++) {
        used += snprintf(summary + used, summarySize - used, "%s%s", i == 0 ? ": " : ", ", parts[i]);
    }
    return TRUE;
}

// --- the game being played -------------------------------------------------------------------------

static BOOL sLoaded = FALSE;
static u16 sStarters[3];
static BOOL sHasStarters = FALSE;

static void LoadSettings(void)
{
    if (sLoaded) {
        return;
    }
    sLoaded = TRUE;
    FILE *file = fopen(SETTINGS_FILE, "r"); // the game's folder is the working directory
    if (file == NULL) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned a, b, c;
        if (sscanf(line, "Starters=%u,%u,%u", &a, &b, &c) == 3 && IsSpecies(a) && IsSpecies(b) && IsSpecies(c)) {
            sStarters[0] = (u16)a;
            sStarters[1] = (u16)b;
            sStarters[2] = (u16)c;
            sHasStarters = TRUE;
        }
    }
    fclose(file);
}

BOOL Randomizer_HasStarters(void)
{
    LoadSettings();
    return sHasStarters;
}

u16 Randomizer_Starter(int index, u16 vanilla)
{
    LoadSettings();
    return sHasStarters && index >= 0 && index < 3 ? sStarters[index] : vanilla;
}

int Randomizer_StarterIndex(u16 species)
{
    LoadSettings();
    for (int i = 0; sHasStarters && i < 3; i++) {
        if (sStarters[i] == species) {
            return i;
        }
    }
    return -1;
}

BOOL Randomizer_FolderSeed(const char *dir, u32 *seed)
{
    char path[1200];
    JoinPath(path, sizeof(path), dir, SETTINGS_FILE);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FALSE;
    }
    BOOL found = FALSE;
    char line[128];
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned value;
        if (sscanf(line, "Seed=%u", &value) == 1) {
            *seed = value;
            found = TRUE;
        }
    }
    fclose(file);
    return found;
}

const char *Randomizer_LogPath(void)
{
    FILE *file = fopen(LOG_FILE, "r");
    if (file == NULL) {
        return NULL;
    }
    fclose(file);
    return LOG_FILE;
}
