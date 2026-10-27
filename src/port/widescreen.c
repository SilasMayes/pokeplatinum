// Widescreen overworld (experimental); see include/port/widescreen.h
#include "port/widescreen.h"

#include <nitro.h>
#include <simulator/sim.h>

#include "constants/overworld_weather.h"

#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/ov5_021D5EB8.h"
#include "port/debug_field.h"

#include "camera.h"

// The field camera's projection as last computed, and its widening
static const Camera *sFieldCamera = NULL;
static float sFieldRatio = 1.0f;

// The field map is running with no sub-screen open (its camera exists then)
static FieldSystem *ShownFieldSystem(void)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || fieldSystem->processManager == NULL || fieldSystem->processManager->child != NULL
        || !fieldSystem->runningFieldMap || fieldSystem->camera == NULL) {
        return NULL;
    }
    return fieldSystem;
}

// No weather overlay (rain, snow, sand, fog, Flash's darkness...): those are 2D and would
// only cover the 4:3 middle
static BOOL ClearWeather(FieldSystem *fieldSystem)
{
    if (fieldSystem->unk_04 == NULL || fieldSystem->unk_04->unk_0C == NULL) {
        return TRUE;
    }
    switch (ov5_021D5FE0(fieldSystem->unk_04->unk_0C)) {
    case OVERWORLD_WEATHER_CLEAR:
    case OVERWORLD_WEATHER_CLEAR_8:
    case OVERWORLD_WEATHER_CLEAR_13:
        return TRUE;
    default:
        return FALSE;
    }
}

void Widescreen_Update(void)
{
    FieldSystem *fieldSystem = ShownFieldSystem();
    SIM_SetWidescreenScene(fieldSystem != NULL && ClearWeather(fieldSystem));
}

static float WantedRatio(const Camera *camera)
{
    FieldSystem *fieldSystem = ShownFieldSystem();
    if (fieldSystem == NULL || camera != fieldSystem->camera || !SIM_IsWidescreenActive()) {
        return 1.0f;
    }
    return SIM_WidescreenRatio();
}

fx32 Widescreen_AspectRatio(const Camera *camera, fx32 aspectRatio)
{
    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem == NULL || camera != fieldSystem->camera) {
        return aspectRatio;
    }
    const float ratio = WantedRatio(camera);
    sFieldCamera = camera;
    sFieldRatio = ratio;
    return ratio == 1.0f ? aspectRatio : (fx32)((float)aspectRatio * ratio);
}

void Widescreen_BeforeFieldRender(FieldSystem *fieldSystem)
{
    Camera *camera = fieldSystem->camera;
    // Only while the field camera is the one in use: the projection is global
    if (camera == NULL || Camera_PortGetActive() != camera) {
        return;
    }
    if (sFieldCamera != camera || sFieldRatio != WantedRatio(camera)) {
        Camera_ComputeProjectionMatrix(camera->projection, camera);
    }
}
