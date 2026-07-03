#pragma once
#include "camera/Camera.h"

// Reads mouse input each frame and drives the Camera (orbit / zoom / pan).
class CameraController
{
public:
    void Update(Camera& camera, Settings appSettings, bool sceneViewHovered, bool noModifiersPressed, ImVec2 sceneCursorDelta, float sceneScrollDelta);
};
