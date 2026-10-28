// The GBA slot from an emulator's save. See port/gba_slot.h
#include "port/gba_slot.h"

#include <nitro.h>
#include <simulator/sim_carts.h>
#include <simulator/sim_gba_slot.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/versions.h"
#include "global/pm_version.h"

#include "port/sim_config_prj.h"
#include "system.h"

#define SAVE_FILE "gba_slot.sav"
#define MAX_SAVE_FILE_SIZE (512 * 1024)

#define SECTOR_SIZE 0x1000
#define SECTOR_DATA_SIZE 0xF80
#define SECTORS_PER_SLOT 14
#define SECTOR_SIGNATURE 0x08012025
#define STORAGE_SIZE 0x83D0 // the PC boxes: 14 boxes of 30, their names and wallpapers
#define BOX_MON_SIZE 80

typedef struct GameInfo {
    const char *name;
    const char *title; // the cartridge header's
    const char *code;  // game code, less the language letter
    int version;
    int family;
} GameInfo;

static const GameInfo sGames[GBA_SLOT_GAME_COUNT] = {
    [GBA_SLOT_NONE] = { "Empty", NULL, NULL, VERSION_NONE, GBA_SLOT_NONE },
    [GBA_SLOT_RUBY] = { "Ruby", "POKEMON RUBY", "AXV", VERSION_RUBY, GBA_SLOT_RUBY },
    [GBA_SLOT_SAPPHIRE] = { "Sapphire", "POKEMON SAPP", "AXP", VERSION_SAPPHIRE, GBA_SLOT_RUBY },
    [GBA_SLOT_EMERALD] = { "Emerald", "POKEMON EMER", "BPE", VERSION_EMERALD, GBA_SLOT_EMERALD },
    [GBA_SLOT_FIRERED] = { "FireRed", "POKEMON FIRE", "BPR", VERSION_FIRERED, GBA_SLOT_FIRERED },
    [GBA_SLOT_LEAFGREEN] = { "LeafGreen", "POKEMON LEAF", "BPG", VERSION_LEAFGREEN, GBA_SLOT_FIRERED },
};

// The save blocks' sizes in each pair of games (their saves differ only in these)
typedef struct SaveLayout {
    int family;
    u32 saveBlock2Size;
    u32 saveBlock1Size;
    u32 partyCountOffset; // in save block 1
} SaveLayout;

static const SaveLayout sLayouts[] = {
    { GBA_SLOT_EMERALD, 0xF2C, 0x3D88, 0x234 },
    { GBA_SLOT_FIRERED, 0xF24, 0x3D68, 0x34 },
    { GBA_SLOT_RUBY, 0x890, 0x3AC0, 0x234 },
};

static int ValidGame(int game)
{
    return game > GBA_SLOT_NONE && game < GBA_SLOT_GAME_COUNT ? game : GBA_SLOT_NONE;
}

const char *GBASlot_SavePath(void)
{
    return SIM_SettingsPath(SAVE_FILE);
}

BOOL GBASlot_HasSave(void)
{
    FILE *file = fopen(GBASlot_SavePath(), "rb");
    if (file == NULL) {
        return FALSE;
    }
    fclose(file);
    return TRUE;
}

int GBASlot_Game(void)
{
    return ValidGame(SIM_Config_prj_GetConfig()->gbaSlotGame);
}

const char *GBASlot_GameName(int game)
{
    return sGames[ValidGame(game)].name;
}

// The letter that ends the game code: the GBA game must be in the DS game's language
static char LanguageLetter(void)
{
    switch (gGameLanguage) {
    case LANGUAGE_JAPANESE:
        return 'J';
    case LANGUAGE_FRENCH:
        return 'F';
    case LANGUAGE_ITALIAN:
        return 'I';
    case LANGUAGE_GERMAN:
        return 'D';
    case LANGUAGE_SPANISH:
        return 'S';
    default:
        return 'E';
    }
}

// Puts the game in the slot, with the save when there is one (an empty flash chip when
// there isn't, or it can't be used)
static void Insert(int game)
{
    game = ValidGame(game);
    if (game == GBA_SLOT_NONE) {
        SIM_GBASlot_Eject();
        return;
    }
    char code[5];
    snprintf(code, sizeof(code), "%s%c", sGames[game].code, LanguageLetter());
    SIM_GBASlotCart cart = { sGames[game].title, code, "01", GBASlot_HasSave() ? GBASlot_SavePath() : NULL };
    char err[160];
    if (!SIM_GBASlot_Insert(&cart, err, sizeof(err)) && cart.savePath != NULL) {
        printf("GBA slot: %s\n", err);
        cart.savePath = NULL;
        SIM_GBASlot_Insert(&cart, NULL, 0);
    }
}

void GBASlot_Init(void)
{
    Insert(GBASlot_Game());
}

void GBASlot_SetGame(int game)
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    config->gbaSlotGame = ValidGame(game);
    SIM_Config_prj_SaveConfigFile(config);
    Insert(config->gbaSlotGame);
    // The dual-slot encounters follow at once (on a DS the main menu would check the slot)
    SetGBACartridgeVersion(sGames[config->gbaSlotGame].version);
}

// --- Reading a save ---------------------------------------------------------------------------

static u32 Read32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u16 Read16(const u8 *p)
{
    return (u16)(p[0] | p[1] << 8);
}

static u16 Checksum(const u8 *data, u32 size)
{
    u32 sum = 0;
    for (u32 i = 0; i + 4 <= size; i += 4) {
        sum += Read32(data + i);
    }
    return (u16)((sum >> 16) + (sum & 0xFFFF));
}

static u32 SectorSize(const SaveLayout *layout, int id)
{
    if (id == 0) {
        return layout->saveBlock2Size;
    }
    if (id == 4) {
        return layout->saveBlock1Size - SECTOR_DATA_SIZE * 3;
    }
    if (id == 13) {
        return STORAGE_SIZE - SECTOR_DATA_SIZE * 8;
    }
    return SECTOR_DATA_SIZE;
}

typedef struct ParsedSave {
    const SaveLayout *layout;
    int sectorOf[SECTORS_PER_SLOT]; // where each part of the newest save is
} ParsedSave;

// One of the two save slots: TRUE if all 14 parts are there with good checksums
static BOOL CheckSlot(const u8 *flash, int slot, const SaveLayout *layout, u32 *counter, int *sectorOf)
{
    u32 found = 0;
    for (int i = 0; i < SECTORS_PER_SLOT; i++) {
        int sector = slot * SECTORS_PER_SLOT + i;
        const u8 *data = flash + sector * SECTOR_SIZE;
        if (Read32(data + 0xFF8) != SECTOR_SIGNATURE) {
            continue;
        }
        int id = Read16(data + 0xFF4);
        if (id >= SECTORS_PER_SLOT || Read16(data + 0xFF6) != Checksum(data, SectorSize(layout, id))) {
            continue;
        }
        found |= 1u << id;
        *counter = Read32(data + 0xFFC);
        sectorOf[id] = sector;
    }
    return found == (1u << SECTORS_PER_SLOT) - 1;
}

// Which pair of games a save is from, by its save block 2 (as PKHeX tells them apart): at
// 0xAC FireRed/LeafGreen keep their game code (1), Ruby/Sapphire have 0 there and Emerald
// its random encryption key. Their checksums can't tell: the sectors are zero past the data.
static const SaveLayout *LayoutOf(const u8 *flash, int slot)
{
    for (int i = 0; i < SECTORS_PER_SLOT; i++) {
        const u8 *data = flash + (slot * SECTORS_PER_SLOT + i) * SECTOR_SIZE;
        if (Read32(data + 0xFF8) == SECTOR_SIGNATURE && Read16(data + 0xFF4) == 0) {
            u32 code = Read32(data + 0xAC);
            return code == 1 ? &sLayouts[1] : code == 0 ? &sLayouts[2] : &sLayouts[0];
        }
    }
    return NULL;
}

// The newest complete save (as the DS game chooses it) and which games' it is
static BOOL ParseSave(const u8 *flash, ParsedSave *out)
{
    int sectors[2][SECTORS_PER_SLOT];
    u32 counters[2] = { 0, 0 };
    const SaveLayout *layouts[2];
    BOOL valid[2];
    for (int slot = 0; slot < 2; slot++) {
        layouts[slot] = LayoutOf(flash, slot);
        valid[slot] = layouts[slot] != NULL && CheckSlot(flash, slot, layouts[slot], &counters[slot], sectors[slot]);
    }
    if (!valid[0] && !valid[1]) {
        return FALSE;
    }
    // The counters wrap around (as ov97_02235FFC compares them)
    int use = valid[1] && (!valid[0] || counters[1] + 1 > counters[0] + 1) ? 1 : 0;
    out->layout = layouts[use];
    memcpy(out->sectorOf, sectors[use], sizeof(out->sectorOf));
    return TRUE;
}

// A name in the GBA games' Western character set
static void DecodeName(const u8 *chars, int count, char *out, int size)
{
    int len = 0;
    for (int i = 0; i < count && chars[i] != 0xFF && len + 4 < size; i++) {
        u8 c = chars[i];
        if (c >= 0xBB && c <= 0xD4) {
            out[len++] = (char)('A' + c - 0xBB);
        } else if (c >= 0xD5 && c <= 0xEE) {
            out[len++] = (char)('a' + c - 0xD5);
        } else if (c >= 0xA1 && c <= 0xAA) {
            out[len++] = (char)('0' + c - 0xA1);
        } else if (c == 0x00) {
            out[len++] = ' ';
        } else if (c == 0xAB) {
            out[len++] = '!';
        } else if (c == 0xAC) {
            out[len++] = '?';
        } else if (c == 0xAD) {
            out[len++] = '.';
        } else if (c == 0xAE) {
            out[len++] = '-';
        } else if (c == 0xB4) {
            out[len++] = '\'';
        } else if (c == 0xB8) {
            out[len++] = ',';
        } else {
            out[len++] = '?';
        }
    }
    out[len] = '\0';
}

static void Summarize(const u8 *flash, GBASlotSummary *out)
{
    memset(out, 0, sizeof(*out));
    ParsedSave save;
    if (flash == NULL || !ParseSave(flash, &save)) {
        return;
    }
    out->valid = TRUE;
    out->family = save.layout->family;

    const u8 *block2 = flash + save.sectorOf[0] * SECTOR_SIZE;
    DecodeName(block2, 7, out->name, sizeof(out->name));
    out->gender = block2[0x08];
    out->trainerID = Read16(block2 + 0x0A);
    out->hours = Read16(block2 + 0x0E);
    out->minutes = block2[0x10];

    const u8 *block1 = flash + save.sectorOf[1] * SECTOR_SIZE;
    out->partyCount = (u8)Read32(block1 + save.layout->partyCountOffset);
    if (out->partyCount > 6) {
        out->partyCount = 0;
    }

    // The boxes' Pokemon (flags byte: bit 1 "has a species")
    for (u32 pos = 4; pos + BOX_MON_SIZE <= 4 + 14 * 30 * BOX_MON_SIZE; pos += BOX_MON_SIZE) {
        u32 flagsAt = pos + 19;
        int part = 5 + (int)(flagsAt / SECTOR_DATA_SIZE);
        const u8 *sector = flash + save.sectorOf[part] * SECTOR_SIZE;
        if (sector[flagsAt % SECTOR_DATA_SIZE] & 2) {
            out->boxedCount++;
        }
    }
}

void GBASlot_GetSummary(GBASlotSummary *out)
{
    Summarize(SIM_GBASlot_Flash(), out);
}

BOOL GBASlot_ImportSave(const char *path, char *message, int messageSize)
{
    message[0] = '\0';
    FILE *in = fopen(path, "rb");
    if (in == NULL) {
        snprintf(message, (size_t)messageSize, "The file can't be opened.");
        return FALSE;
    }
    u8 *data = malloc(MAX_SAVE_FILE_SIZE);
    size_t size = data != NULL ? fread(data, 1, MAX_SAVE_FILE_SIZE, in) : 0;
    fclose(in);
    if (size < SIM_GBA_FLASH_SIZE) {
        snprintf(message, (size_t)messageSize, "%s",
            size == 0x10000 ? "That save is 64 KB. Pokemon's GBA saves are 128 KB: set the emulator's save type to "
                              "Flash 128K (1 Mbit) and load the game once."
                            : "That isn't a GBA Pokemon save (it should be 128 KB).");
        free(data);
        return FALSE;
    }

    GBASlotSummary summary;
    Summarize(data, &summary);
    if (!summary.valid) {
        snprintf(message, (size_t)messageSize, "No saved game in that file that the DS can read: it may be empty, "
                                               "damaged, or not Ruby, Sapphire, Emerald, FireRed or LeafGreen.");
        free(data);
        return FALSE;
    }

    FILE *out = fopen(GBASlot_SavePath(), "wb");
    BOOL ok = out != NULL && fwrite(data, 1, size, out) == size;
    if (out != NULL && fclose(out) != 0) {
        ok = FALSE;
    }
    free(data);
    if (!ok) {
        snprintf(message, (size_t)messageSize, "The save couldn't be copied in.");
        remove(GBASlot_SavePath());
        Insert(GBASlot_Game());
        return FALSE;
    }

    // The slot's game follows the save's pair of games
    int game = GBASlot_Game();
    if (sGames[game].family != summary.family) {
        game = summary.family;
        snprintf(message, (size_t)messageSize, "%s",
            game == GBA_SLOT_RUBY       ? "A Ruby or Sapphire save: the slot is set to Ruby. Choose Sapphire if it's that one."
                : game == GBA_SLOT_FIRERED ? "A FireRed or LeafGreen save: the slot is set to FireRed. Choose LeafGreen if it's that one."
                                           : "An Emerald save: the slot is set to Emerald.");
    }
    GBASlot_SetGame(game);
    return TRUE;
}

void GBASlot_RemoveSave(void)
{
    remove(GBASlot_SavePath());
    Insert(GBASlot_Game());
}
