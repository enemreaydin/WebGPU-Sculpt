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

    // The straight-handle hit test, shared by translate and scale (both draw the
    // same three screen-space segments; only the end cap differs). `axisEnabled`
    // masks out handles that are not offered — the scale gizmo hides the axes a
    // primitive cannot scale (PrimitiveGeometry::ScaleAxisMap), and a hidden
    // handle must not be grabbable. Pass nullptr for "all three".
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
        float hitThresholdPx,
        const bool* axisEnabled = nullptr
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

    // Multiplier for a scale drag: the mouse travel since the grab, projected
    // onto the handle's screen direction and measured in HANDLE LENGTHS. Pulling
    // one full handle length outwards doubles the object (factor 2); pushing the
    // same distance inwards collapses it to nothing (factor 0), so the result is
    // floored at MIN_SCALE_FACTOR — a zero or negative size is not a small
    // object, it is a degenerate distance field (inside-out normals, NaN shading).
    //
    // Measuring in handle lengths rather than raw pixels is what makes the drag
    // feel identical near and far from the camera: the handle is a constant
    // on-screen length, so the same gesture always means the same factor.
    constexpr float MIN_SCALE_FACTOR = 0.01f;

    bool ComputeScaleDragFactor(
        const glm::vec3& axisDir,
        const glm::vec2& dragStartMouse,
        const glm::vec2& currentMouse,
        const glm::vec3& dragStartObjectPosition,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx,
        float& outFactor
    );
}
