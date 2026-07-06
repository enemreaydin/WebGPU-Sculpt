#include "camera/CameraController.h"

void CameraController::Update(Camera& camera, const Settings& appSettings, const ViewportInput& input)
{
    // Orbit: left-drag with no modifiers.
    if (input.orbiting)
    {
        camera.OrbitMovement(appSettings, input.mouseDelta);
    }

    // Pan: left-drag with the pan modifier held.
    if (input.panning)
    {
        camera.TargetPan(appSettings, input.mouseDelta);
    }

    // Zoom: mouse wheel.
    if (input.scrollDelta != 0.0f)
    {
        camera.Zoom(appSettings, input.scrollDelta);
    }
}
