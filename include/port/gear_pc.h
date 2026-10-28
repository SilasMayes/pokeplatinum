#ifndef POKEPLATINUM_PORT_GEAR_PC_H
#define POKEPLATINUM_PORT_GEAR_PC_H

// The Gear's PC tab: the boxes as they are in the save (to look through and search), and
// sorting them and the bag. Only while walking around (as the save editor): the game's PC
// and bag work on the same data.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEAR_PC_BOXES 18
#define GEAR_PC_BOX_SLOTS 30

typedef struct GearBoxMon {
    u16 species; // SPECIES_NONE: empty
    u8 form;
    u8 level;
    u8 gender;
    BOOL isEgg;
    BOOL shiny;
    u32 personality;
} GearBoxMon;

enum GearPCSort {
    GEAR_PC_SORT_DEX = 0,
    GEAR_PC_SORT_NAME,
    GEAR_PC_SORT_LEVEL, // highest first
    GEAR_PC_SORT_TYPE,
};

enum GearBagSort {
    GEAR_BAG_SORT_NAME = 0,
    GEAR_BAG_SORT_NUMBER,
};

// NULL when the boxes and bag can be read and changed now, else why not
const char *Gear_PCUnavailable(void);
BOOL Gear_PCReadBox(int box, GearBoxMon out[GEAR_PC_BOX_SLOTS]);
void Gear_PCBoxName(int box, char *out, int size);
int Gear_PCCurrentBox(void);
// Sorts a box (-1: all of them, filling them in order). Eggs go last. FALSE if it can't now.
BOOL Gear_PCSort(int box, int key);
// Sorts every pocket of the bag
BOOL Gear_BagSort(int key);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_GEAR_PC_H
