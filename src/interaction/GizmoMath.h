#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "interaction/GizmoState.h"

namespace GizmoMath
{
    struct AxisScreenSegment
    {
        GizmoAxis axis = GizmoAxis::None;
        glm::vec3 worldDir = glm::vec3(0.0f);

        glm::vec2 screenStart = glm::vec2(0.0f);
        glm::vec2 screenEnd = glm::vec2(0.0f);

        float worldLength = 1.0f;
        bool valid = false;
    };

    glm::vec3 AxisToWorldDir(GizmoAxis axis);

    // The axis' world direction in the requested space: the raw world axis, or
    // that axis carried into the object's frame by its rotation.
    glm::vec3 AxisDirInSpace(GizmoAxis axis, const glm::quat& objectRotation, GizmoSpace space);

    bool WorldToScreen(
        const glm::vec3& worldPos,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec2& outScreen
    );

    // Inverse of WorldToScreen: the world-space ray through a render pixel.
    bool ScreenPointToRay(
        const glm::vec2& mouse,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec3& outRayOrigin,
        glm::vec3& outRayDir
    );

    // Ray/plane hit. False when the ray is parallel to the plane (the ring seen
    // edge-on) or the plane sits behind the camera.
    bool IntersectRayPlane(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        float& outT
    );

    // Signed angle from `from` to `to` measured about `axis`, in radians.
    float SignedAngleAround(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis);

    // Unit vector from the ring centre to where the mouse ray pierces the ring's
    // plane — the "spoke" the rotate drag tracks.
    bool ComputeRingSpoke(
        const glm::vec2& mouse,
        const glm::vec3& origin,
        const glm::vec3& axisDir,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec3& outSpoke
    );

    float DistancePointToSegment(
        const glm::vec2& point,
        const glm::vec2& a,
        const glm::vec2& b
    );

    float WorldUnitsPerPixelAt(
        const glm::vec3& worldPos,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportHeight
    );

    AxisScreenSegment BuildAxisSegment(
        GizmoAxis axis,
        const glm::vec3& axisDir,
        const glm::vec3& origin,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx
    );

    GizmoAxis HitTestTranslateAxes(
        const glm::vec2& mouse,
        const glm::vec3& origin,
        const glm::quat& objectRotation,
        GizmoSpace space,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx,
        float hitThresholdPx
    );

    // Same idea for the three rotate rings: the ring whose circumference the
    // mouse ray passes closest to, within the tolerance. Back-facing halves are
    // not pickable, matching what the shader draws.
    GizmoAxis HitTestRotateRings(
        const glm::vec2& mouse,
        const glm::vec3& origin,
        const glm::quat& objectRotation,
        GizmoSpace space,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenRadiusPx,
        float hitThresholdPx
    );

    bool ComputeTranslateDragPosition(
        const glm::vec3& axisDir,
        const glm::vec2& dragStartMouse,
        const glm::vec2& currentMouse,
        const glm::vec3& dragStartObjectPosition,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx,
        glm::vec3& outPosition
    );
}
