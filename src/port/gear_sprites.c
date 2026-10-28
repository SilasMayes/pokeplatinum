// See include/port/gear_sprites.h
#include "port/gear_sprites.h"

#include <nitro.h>
#include <stdlib.h>
#include <string.h>

#include "constants/narc.h"
#include "constants/species.h"
#include "generated/genders.h"

#include "narc.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_sprite.h"

// --- NCGR / NCLR (the DS's character and palette files) -------------------------------------

typedef struct CharacterFile {
    u16 tilesHigh; // 0xFFFF: not given (1D mapping)
    u16 tilesWide;
    u32 pixelFormat; // 3: 4 bits per pixel
    BOOL bitmap;     // pixels in rows, not in 8x8 tiles
    u32 size;
    u8 *pixels;
} CharacterFile;

static u16 Read16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 Read32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// The data of a block ("RAHC", "TTLP"...) of a G2D file, after its 8-byte block header
static u8 *FindBlock(u8 *file, u32 fileSize, const char *magic, u32 *blockSize)
{
    if (fileSize < 16) {
        return NULL;
    }
    u32 offset = Read16(file + 12);
    const u16 blockCount = Read16(file + 14);
    for (int i = 0; i < blockCount && offset + 8 <= fileSize; i++) {
        const u32 size = Read32(file + offset + 4);
        if (size < 8 || offset + size > fileSize) {
            return NULL;
        }
        if (memcmp(file + offset, magic, 4) == 0) {
            *blockSize = size - 8;
            return file + offset + 8;
        }
        offset += size;
    }
    return NULL;
}

static BOOL ReadCharacters(u8 *file, u32 fileSize, CharacterFile *out)
{
    u32 blockSize = 0;
    u8 *block = FindBlock(file, fileSize, "RAHC", &blockSize);
    if (block == NULL || blockSize < 24) {
        return FALSE;
    }
    out->tilesHigh = Read16(block);
    out->tilesWide = Read16(block + 2);
    out->pixelFormat = Read32(block + 4);
    out->bitmap = (Read32(block + 12) & 0xFF) == 1;
    out->size = Read32(block + 16);
    const u32 offset = Read32(block + 20);
    if (offset + out->size > blockSize) {
        return FALSE;
    }
    out->pixels = block + offset;
    return out->pixelFormat == 3; // 16 colors
}

// count colors from the palette file's first, as RGBA8
static BOOL ReadPalette(u8 *file, u32 fileSize, int first, int count, u32 *colors)
{
    u32 blockSize = 0;
    u8 *block = FindBlock(file, fileSize, "TTLP", &blockSize);
    if (block == NULL || blockSize < 16) {
        return FALSE;
    }
    const u32 size = Read32(block + 8);
    const u32 offset = Read32(block + 12);
    if (offset + size > blockSize || (u32)(first + count) * 2 > size) {
        return FALSE;
    }
    const u8 *raw = block + offset + first * 2;
    for (int i = 0; i < count; i++) {
        const u16 bgr = Read16(raw + i * 2);
        const u32 r = ((bgr & 0x1F) * 527 + 23) >> 6;
        const u32 g = (((bgr >> 5) & 0x1F) * 527 + 23) >> 6;
        const u32 b = (((bgr >> 10) & 0x1F) * 527 + 23) >> 6;
        // Bytes R, G, B, A in memory
        colors[i] = r | (g << 8) | (b << 16) | (i == 0 ? 0u : 0xFF000000u);
    }
    return TRUE;
}

// 4-bit pixel (x, y) of an image width pixels wide, in rows or in 8x8 tiles row by row
static u8 Pixel(const u8 *pixels, BOOL bitmap, int width, int x, int y)
{
    u32 index;
    if (bitmap) {
        index = (u32)(y * width + x);
    } else {
        const int tilesWide = width / 8;
        const int tile = (y / 8) * tilesWide + (x / 8);
        index = (u32)(tile * 64 + (y % 8) * 8 + (x % 8));
    }
    const u8 byte = pixels[index / 2];
    return (index & 1) ? (byte >> 4) : (byte & 0x0F);
}

static void ToRGBA(const u8 *pixels, BOOL bitmap, int sourceWidth, int width, int height, const u32 *palette, u32 *rgba)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            rgba[y * width + x] = palette[Pixel(pixels, bitmap, sourceWidth, x, y)];
        }
    }
}

// --- icons -----------------------------------------------------------------------------------

BOOL GearSprites_Icon(u16 species, u8 form, BOOL isEgg, u32 *rgba)
{
    if (species == SPECIES_NONE || species > MAX_SPECIES) {
        return FALSE;
    }

    u32 charSize = 0;
    u8 *charFile = NARC_PortReadMember(NARC_INDEX_POKETOOL__ICONGRA__PL_POKE_ICON,
        (int)PokeIconSpriteIndex(species, isEgg, form), &charSize);
    u32 palSize = 0;
    u8 *palFile = NARC_PortReadMember(NARC_INDEX_POKETOOL__ICONGRA__PL_POKE_ICON, (int)PokeIconPalettesFileIndex(),
        &palSize);

    BOOL ok = FALSE;
    CharacterFile chars;
    u32 palette[16];
    if (charFile != NULL && palFile != NULL && ReadCharacters(charFile, charSize, &chars)
        && chars.size >= GEAR_ICON_WIDTH * GEAR_ICON_HEIGHT / 2
        && ReadPalette(palFile, palSize, PokeIconPaletteIndex(species, form, isEgg) * 16, 16, palette)) {
        ToRGBA(chars.pixels, chars.bitmap, GEAR_ICON_WIDTH, GEAR_ICON_WIDTH, GEAR_ICON_HEIGHT, palette, rgba);
        ok = TRUE;
    }
    free(charFile);
    free(palFile);
    return ok;
}

// --- front sprites ---------------------------------------------------------------------------

#define SPRITE_SHEET_WIDTH 160 // two 80x80 frames side by side
#define SPRITE_SHEET_BYTES (SPRITE_SHEET_WIDTH * GEAR_FRONT_SIZE / 2)

static u8 *ReadSpriteSheet(const PokemonSpriteTemplate *sprite, int character, CharacterFile *chars, u32 *size)
{
    u8 *file = NARC_PortReadMember(sprite->narcID, character, size);
    if (file == NULL) {
        return NULL;
    }
    if (!ReadCharacters(file, *size, chars) || chars->size < SPRITE_SHEET_BYTES) {
        free(file);
        return NULL;
    }
    return file;
}

BOOL GearSprites_Front(u16 species, u8 gender, u8 form, BOOL shiny, BOOL isEgg, u32 personality, u32 *rgba)
{
    if (species == SPECIES_NONE || species > MAX_SPECIES) {
        return FALSE;
    }

    PokemonSpriteTemplate sprite;
    if (isEgg) {
        BuildPokemonSpriteTemplate(&sprite, SPECIES_EGG, GENDER_MALE, FACE_FRONT, FALSE, species == SPECIES_MANAPHY ? 1 : 0, 0);
    } else {
        BuildPokemonSpriteTemplate(&sprite, species, gender, FACE_FRONT, shiny ? 1 : 0, form, personality);
    }

    CharacterFile chars;
    u32 charSize = 0;
    u8 *charFile = ReadSpriteSheet(&sprite, sprite.character, &chars, &charSize);
    if (charFile == NULL && !isEgg && sprite.narcID == NARC_INDEX_POKETOOL__POKEGRA__PL_POKEGRA && gender == GENDER_FEMALE) {
        // No female sprite of its own: the other one
        charFile = ReadSpriteSheet(&sprite, sprite.character + 1, &chars, &charSize);
    }
    u32 palSize = 0;
    u8 *palFile = NARC_PortReadMember(sprite.narcID, sprite.palette, &palSize);

    BOOL ok = FALSE;
    u32 palette[16];
    if (charFile != NULL && palFile != NULL && ReadPalette(palFile, palSize, 0, 16, palette)) {
        PokemonSprite_Decrypt(chars.pixels, sprite.narcID);
        if (sprite.spindaSpots == SPECIES_SPINDA) {
            PokemonSprite_DrawSpindaSpots(chars.pixels, sprite.personality, FALSE);
        }
        ToRGBA(chars.pixels, TRUE, SPRITE_SHEET_WIDTH, GEAR_FRONT_SIZE, GEAR_FRONT_SIZE, palette, rgba);
        ok = TRUE;
    }
    free(charFile);
    free(palFile);
    return ok;
}
