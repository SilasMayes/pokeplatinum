// See include/port/speed_up.h
#include "port/speed_up.h"

#include <nitro.h>

#include "port/sim_config_prj.h"

BOOL SpeedUp_InstantText(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    return config != NULL && config->instantText;
}

BOOL SpeedUp_InstantBars(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    return config != NULL && config->instantBars;
}
