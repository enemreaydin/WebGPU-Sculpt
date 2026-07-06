#pragma once
#include "camera/Camera.h"
#include "core/ViewportInput.h"

// Drives the Camera (orbit / zoom / pan) from a per-frame ViewportInput. Input
// is supplied by the caller (web bindings or a native event pump) rather than
// polled from any UI toolkit.
class CameraController
{
public:
    void Update(Camera& camera, const Settings& appSettings, const ViewportInput& input);
};
