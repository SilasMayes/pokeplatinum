// 60 FPS scope; see include/port/frame_rate.h
#include "port/frame_rate.h"

#include <nitro.h>
#include <simulator/sim.h>
#include <stdio.h>

#include "field/field_system.h"
#include "port/debug_field.h"
#include "port/sim_config_prj.h"
#include "port/widescreen.h"

#include "field_task.h"

static BOOL s60Hz = FALSE;
static BOOL sSpeedFix = FALSE;
static BOOL sOddTick = FALSE; // alternates every 60 Hz iteration

// The player is free on the overworld: the field map runs with no sub-screen open and
// no event, menu, warp or battle (all of which are field tasks)
static BOOL PlayerIsFreeOnOverworld(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    return fieldSystem != NULL
        && fieldSystem->processManager != NULL
        && !fieldSystem->processManager->pause
        && fieldSystem->processManager->child == NULL
        && fieldSystem->runningFieldMap
        && !FieldSystem_IsRunningTask(fieldSystem);
}

void FrameRate_Update(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    Widescreen_Update();

    if (config == NULL || !config->enable60fps) {
        s60Hz = FALSE;
    } else if (config->enable60fpsEverywhere) {
        s60Hz = TRUE;
    } else {
        s60Hz = PlayerIsFreeOnOverworld();
    }
    sSpeedFix = s60Hz && config->enable60fpsSpeedFix;
    sOddTick = sSpeedFix ? !sOddTick : FALSE;

    static int sLogged = -1;
    if (sLogged != (int)s60Hz) {
        sLogged = (int)s60Hz;
        printf("Frame rate: game logic at %s\n", s60Hz ? "60 Hz" : "30 Hz");
    }
}

BOOL FrameRate_Is60Hz(void)
{
    return s60Hz;
}

void FrameRate_BeforeFirstVBlank(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config == NULL || !config->drawEveryFrame) {
        SIM_SkipNextFrame();
    }
}

BOOL FrameRate_SpeedFixActive(void)
{
    return sSpeedFix;
}

BOOL FrameRate_SkipHalfRateTick(void)
{
    return sSpeedFix && sOddTick;
}
