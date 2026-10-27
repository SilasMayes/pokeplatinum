#ifndef SIM_CONFIG_PRJ_H
#define SIM_CONFIG_PRJ_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL enable60fps; // When true, unlock the framerate to 60fps
    BOOL enable60fpsSpeedFix; // When enabled, fixes things in the game that run at double speed due to being at 60fps
    BOOL enable60fpsEverywhere; // 60 Hz logic everywhere, not only on the overworld (see port/frame_rate.h)
    BOOL drawEveryFrame; // draw the in-between DS frames of 30 Hz scenes too (see port/frame_rate.h)
    BOOL instantText; // see port/speed_up.h
    BOOL instantBars;
    BOOL hmFree; // field moves without HMs: see port/hm_free.h
    BOOL hardLevelCap; // no battle EXP past the level cap (Gear_LevelCap)
    int clockOffsetMinutes; // the game's clock runs this far ahead of the device's (SIM_SetClockOffset)
    BOOL nuzlocke;          // Nuzlocke helper (port/nuzlocke.h)
    BOOL nuzlockeDupes;     // dupes clause
    BOOL nuzlockeShiny;     // shiny clause
    BOOL nuzlockeToPC;      // fainted Pokemon go to the PC after the battle
	BOOL enableAsserts; //Should asserts cause a communication error?
    BOOL breakDebuggerOnGfAssert; // When true, break the debugger when a GF_ASSERT fails

	// Cheats section
	BOOL walkThroughWalls; // When enabled, allows the player to walk through walls
    BOOL disableRandomEncounters; // When enabled, all random encounters are turned off
    BOOL runFromTrainerBattles; // When enabled, allows running away from trainer battles

    // Saves section
    int saveSlot; // Save slot in use, 1-based (slot 1 is the original save.bin)
    BOOL autosave; // Save automatically while exploring, when it is safe to
    int autosaveMinutes; // How often autosave runs
    BOOL autosaveOnExit; // Also save when the app is closed or sent to the background

    // Gear section (companion UI on the lower display)
    BOOL gearEnabled;
    int gearDiscovery; // GEAR_DISCOVERY_*: how much the Gear reveals
    BOOL gearBattleControls; // in battles the Gear takes the whole lower screen and has the battle menu
} SIM_Config_prj_type;

enum {
    GEAR_DISCOVERY_VANILLA = 0,   // only what the game itself tells you
    GEAR_DISCOVERY_GUIDE = 1,     // encounter guide; unseen species stay hidden
    GEAR_DISCOVERY_EVERYTHING = 2 // everything, including IVs/EVs and unseen species
};

void SIM_Config_prj_LoadDefaults(SIM_Config_prj_type * aConfig);
BOOL SIM_Config_prj_LoadConfigFile(SIM_Config_prj_type * aConfig);
void SIM_Config_prj_SaveConfigFile(SIM_Config_prj_type * aConfig);
void SIM_Config_prj_init();
SIM_Config_prj_type * SIM_Config_prj_GetConfig();

#ifdef __cplusplus
}
#endif

#endif