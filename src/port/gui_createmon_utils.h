#ifndef GUI_CREATEMON_UTILS_H
#define GUI_CREATEMON_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: You will need to free the pointer returned by this!
const char * GUI_CreateMon_GetSpeciesName(int speciesNo);
int GUI_CreateMon_GenPokemon(int speciesNo, int level);
// Starts a battle from the overworld: against a wild speciesNo at level, or against
// trainerID (0: wild) when it is set, as a double battle if doubles is set
int GUI_CreateMon_StartBattle(int speciesNo, int level, int trainerID, int doubles);

#ifdef __cplusplus
}
#endif


#endif