#ifndef POKEPLATINUM_PORT_WIDESCREEN_H
#define POKEPLATINUM_PORT_WIDESCREEN_H

// Widescreen overworld (experimental: Settings > Display > Widescreen, libntr's setting).
// While the field map is shown in clear weather, libntr draws the 3D scene as wide as the
// top display and the field camera's projection is widened to match, so more of the map
// shows on the sides. The 2D layers (text boxes, menus, the location banner) stay 4:3 in
// the middle. Weather and Flash's darkness are 2D overlays that would only cover the
// middle, so those scenes stay 4:3, as does everything off the field map (battles, menus).

#include <nitro/types.h>

#include "field/field_system_decl.h"

#include "camera.h"

#ifdef __cplusplus
extern "C" {
#endif

// Once per main loop iteration: tells libntr whether this scene may be wide
void Widescreen_Update(void);

// Camera_ComputeProjectionMatrix: the aspect ratio to project the camera with
fx32 Widescreen_AspectRatio(const Camera *camera, fx32 aspectRatio);

// The field map's 3D rendering is about to start: widens or narrows the field camera's
// projection when libntr's wide state changed
void Widescreen_BeforeFieldRender(FieldSystem *fieldSystem);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_WIDESCREEN_H
