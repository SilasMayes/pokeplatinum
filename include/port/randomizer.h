#ifndef POKEPLATINUM_PORT_RANDOMIZER_H
#define POKEPLATINUM_PORT_RANDOMIZER_H

// The randomizer: a game added from the "Add a game" screen (libntr's sim_carts) can be
// randomized from a seed. It changes the game's own data files in its folder, so the
// game, the Gear and everything else just read the randomized data:
//   - wild Pokemon: each area's species become others (one for one within the area, so
//     how often each shows up stays the same)
//   - trainers' Pokemon: levels stay; trainers with chosen moves get the new Pokemon's
//     latest level-up moves
//   - starters: three Pokemon that evolve twice (written to randomizer.ini, which the
//     starter choice and the rival's and Dawn's/Lucas's Pokemon read)
// optionally of similar strength (base stat total), sharing a type, and without
// legendaries. What changed is written to randomizer_log.txt in the game's folder.
//
// Everything comes from the game's own files (personal data, evolutions, learnsets,
// names), so Renegade Platinum's changes are kept.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RANDOMIZER_WILD = 0,
    RANDOMIZER_TRAINERS,
    RANDOMIZER_STARTERS,
    RANDOMIZER_SIMILAR_STRENGTH,
    RANDOMIZER_KEEP_TYPE,
    RANDOMIZER_NO_LEGENDARIES,
    RANDOMIZER_OPTION_COUNT,
};

// The starter in position index (0: left, Turtwig's) for the game being played: the
// randomized one, or vanilla
u16 Randomizer_Starter(int index, u16 vanilla);
BOOL Randomizer_HasStarters(void);
// Which starter position species is in (-1 when it isn't a randomized starter)
int Randomizer_StarterIndex(u16 species);

// The seed a game folder was randomized with (FALSE if it wasn't)
BOOL Randomizer_FolderSeed(const char *dir, u32 *seed);

// The log of the game being played ("randomizer_log.txt"), NULL if it isn't randomized
const char *Randomizer_LogPath(void);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_RANDOMIZER_H
