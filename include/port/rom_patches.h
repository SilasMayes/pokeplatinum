#ifndef POKEPLATINUM_PORT_ROM_PATCHES_H
#define POKEPLATINUM_PORT_ROM_PATCHES_H

// ROM hacks of Platinum (Renegade Platinum and others built on the US ROM) are mostly
// new data files, which the port already loads from the player's ROM. They also change
// a few tables that the DS game keeps in its code (map headers, shop stock, TM moves...)
// and a handful of instructions (shiny odds, obedience, where the type chart lives).
//
// The port has those tables in C. At startup RomPatches_Apply() reads the extracted
// arm9.bin and overlay binaries, finds each registered table in them and copies the
// ROM's version over the C one, and reads the known patched instructions. With a
// vanilla ROM nothing changes.

#include <nitro/types.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROM_FILE_ARM9 (-1) // otherwise an overlay ID

typedef struct RomPatchesInfo {
    BOOL applied;            // the ROM's code files were found and checked
    BOOL modified;           // the ROM differs from vanilla Platinum in what the port checks
    BOOL renegadePlatinum;   // looks like Renegade Platinum (Fairy type, its type chart)
    int shinyThreshold;      // shiny when the shiny value is below this (vanilla 8: 1/8192)
    int obeyAllBadges;       // badges from which every Pokemon obeys (vanilla 8)
    BOOL extraType;          // type 9 ("???" in vanilla) is a real type, e.g. Fairy
    int extraTypePokedexIcon; // Pokedex icon animation for type 9 (vanilla: Ghost's, 7)
    u16 introSpecies;        // the Pokemon in Professor Rowan's introduction (Buneary)
    int tablesChanged;       // registered tables the ROM changes
} RomPatchesInfo;

// A name for the ROM extracted in dir ("Pokemon Platinum", "Renegade Platinum Classic"...)
void RomPatches_DescribeRom(const char *dir, char *name, int nameSize);

// Once at startup, when the ROM files are present (after the fonts are loaded)
void RomPatches_Apply(void);
const RomPatchesInfo *RomPatches_Info(void);

// The battle type chart (rows of attacker, defender, multiplier; 0xFE separates the
// Foresight-only rows, 0xFF ends it): the ROM's when it has its own, else vanillaChart
const u8 (*RomPatches_TypeChart(const u8 (*vanillaChart)[3]))[3];

// Tables read from the ROM. hint: offset of the table in the US Rev 1 ROM's file, where
// it is looked for first (it is searched for elsewhere if it isn't there). Tables of one
// group are checked together, for tables too small to be recognised on their own.
void RomPatches_RegisterTable(const char *name, void *table, size_t size, int file, u32 hint, const char *group);

// Registers a writable table (defined without const in port builds)
#define ROM_PATCHES_TABLE(table, file, hint, group)                                               \
    static void __attribute__((constructor)) RomPatchesRegister_##table(void)                     \
    {                                                                                             \
        RomPatches_RegisterTable(#table, (void *)(table), sizeof(table), (file), (hint), (group)); \
    }

// For a const table that can't be made writable (generated headers): defines a writable
// copy, table##_rom, registered in its place. Follow with #define table table##_rom.
#define ROM_PATCHES_TABLE_COPY(type, table, file, hint, group)                                  \
    static type table##_rom[sizeof(table) / sizeof(type)];                                      \
    static void __attribute__((constructor)) RomPatchesRegister_##table(void)                   \
    {                                                                                           \
        memcpy(table##_rom, table, sizeof(table));                                              \
        RomPatches_RegisterTable(#table, (void *)table##_rom, sizeof(table), (file), (hint), (group)); \
    }

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_ROM_PATCHES_H
