#include "interaction/GizmoMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float EPSILON = 0.00001f;

    // Below this |dot(rayDir, planeNormal)| the ray is grazing the plane and the
    // intersection point runs off to infinity — that is a ring seen edge-on.
    constexpr float PLANE_PARALLEL_EPSILON = 0.0005f;

    // A ring point counts as "back half" once it is this far behind the ring
    // centre along the view direction, as a fraction of the ring radius. A plain
    // `> 0` test would speckle a ring viewed face-on, where the whole
    // circumference sits at depth ~0 and float noise decides each pixel.
    constexpr float RING_BACK_FACE_TOLERANCE = 0.02f;

    // World-space camera forward. The view matrix maps world into camera space,
    // where the camera looks down -Z; its rotation block is orthonormal, so the
    // inverse is the transpose and the world forward is minus the third row.
    glm::vec3 CameraForward(const glm::mat4& view)
    {
        return -glm::vec3(view[0][2], view[1][2], view[2][2]);
    }
}

namespace GizmoMath
{
    glm::vec3 AxisToWorldDir(GizmoAxis axis)
    {
        switch (axis)
        {
        case GizmoAxis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
        case GizmoAxis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
        case GizmoAxis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
        default:           return glm::vec3(0.0f);
        }
    }

    glm::vec3 AxisDirInSpace(GizmoAxis axis, const glm::quat& objectRotation, GizmoSpace space)
    {
        glm::vec3 dir = AxisToWorldDir(axis);

        if (space == GizmoSpace::World || axis == GizmoAxis::None)
            return dir;

        // The object's rotation is local-to-world, so it carries a local axis
        // straight into world space. Renormalize: the stored quaternion may have
        // drifted a hair off unit length.
        glm::vec3 rotated = objectRotation * dir;
        float len = glm::length(rotated);
        if (len < EPSILON)
            return dir;
        return rotated / len;
    }

    bool WorldToScreen(
        const glm::vec3& worldPos,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec2& outScreen
    )
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return false;

        glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);

        if (std::abs(clip.w) < EPSILON)
            return false;

        glm::vec3 ndc = glm::vec3(clip) / clip.w;

        if (ndc.z < -0.1f || ndc.z > 1.1f)
            return false;

        outScreen.x = (ndc.x * 0.5f + 0.5f) * viewportWidth;
        outScreen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;

        return true;
    }

    bool ScreenPointToRay(
        const glm::vec2& mouse,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec3& outRayOrigin,
        glm::vec3& outRayDir
    )
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return false;

        // Undo WorldToScreen: pixels -> NDC. The Y flip is reinstated here, and
        // NDC depth runs [0,1] (WebGPU convention, GLM_FORCE_DEPTH_ZERO_TO_ONE),
        // so z=0 is the near plane and z=1 the far plane.
        float ndcX = (2.0f * mouse.x / viewportWidth) - 1.0f;
        float ndcY = 1.0f - (2.0f * mouse.y / viewportHeight);

        glm::mat4 invViewProj = glm::inverse(proj * view);

        glm::vec4 nearH = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farH  = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

        if (std::abs(nearH.w) < EPSILON || std::abs(farH.w) < EPSILON)
            return false;

        glm::vec3 nearPoint = glm::vec3(nearH) / nearH.w;
        glm::vec3 farPoint  = glm::vec3(farH) / farH.w;

        glm::vec3 dir = farPoint - nearPoint;
        if (glm::dot(dir, dir) < EPSILON)
            return false;

        outRayOrigin = nearPoint;
        outRayDir = glm::normalize(dir);
        return true;
    }

    bool IntersectRayPlane(
        const glm::vec3& rayOrigin,
        const glm::vec3& rayDir,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal,
        float& outT
    )
    {
        float denom = glm::dot(rayDir, planeNormal);
        if (std::abs(denom) < PLANE_PARALLEL_EPSILON)
            return false;

        float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
        if (t <= 0.0f)
            return false;

        outT = t;
        return true;
    }

    float SignedAngleAround(const glm::vec3& from, const glm::vec3& to, const glm::vec3& axis)
    {
        // atan2 of (the component of from x to along the axis) against (from . to)
        // recovers the full [-pi, pi] sweep, including the sign. acos(dot) alone
        // could not tell clockwise from counter-clockwise.
        float sine = glm::dot(glm::cross(from, to), axis);
        float cosine = glm::dot(from, to);
        return std::atan2(sine, cosine);
    }

    bool ComputeRingSpoke(
        const glm::vec2& mouse,
        const glm::vec3& origin,
        const glm::vec3& axisDir,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec3& outSpoke
    )
    {
        glm::vec3 rayOrigin, rayDir;
        if (!ScreenPointToRay(mouse, view, proj, viewportWidth, viewportHeight, rayOrigin, rayDir))
            return false;

        float t;
        if (!IntersectRayPlane(rayOrigin, rayDir, origin, axisDir, t))
            return false;

        glm::vec3 hit = rayOrigin + rayDir * t;
        glm::vec3 spoke = hit - origin;

        float len = glm::length(spoke);
        if (len < EPSILON)   // mouse exactly on the ring centre: no angle defined
            return false;

        outSpoke = spoke / len;
        return true;
    }

    float DistancePointToSegment(
        const glm::vec2& point,
        const glm::vec2& a,
        const glm::vec2& b
    )
    {
        glm::vec2 ab = b - a;
        float abLenSq = glm::dot(ab, ab);

        if (abLenSq < EPSILON)
            return glm::length(point - a);

        float t = glm::dot(point - a, ab) / abLenSq;
        t = std::clamp(t, 0.0f, 1.0f);

        glm::vec2 closest = a + ab * t;
        return glm::length(point - closest);
    }

    float WorldUnitsPerPixelAt(
        const glm::vec3& worldPos,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportHeight
    )
    {
        if (viewportHeight <= 0.0f)
            return 0.01f;

        glm::vec4 viewPos = view * glm::vec4(worldPos, 1.0f);

        float depth = std::abs(viewPos.z);
        depth = std::max(depth, 0.001f);

        float fy = proj[1][1];

        if (std::abs(fy) < EPSILON)
            return 0.01f;

        float visibleWorldHeight = 2.0f * depth / fy;
        return visibleWorldHeight / viewportHeight;
    }

    AxisScreenSegment BuildAxisSegment(
        GizmoAxis axis,
        const glm::vec3& axisDir,
        const glm::vec3& origin,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx
    )
    {
        AxisScreenSegment result{};
        result.axis = axis;
        result.worldDir = axisDir;

        if (axis == GizmoAxis::None)
            return result;

        float unitsPerPixel = WorldUnitsPerPixelAt(origin, view, proj, viewportHeight);
        float worldLength = std::max(unitsPerPixel * desiredScreenLengthPx, 0.001f);

        result.worldLength = worldLength;

        glm::vec3 end = origin + result.worldDir * worldLength;

        if (!WorldToScreen(origin, view, proj, viewportWidth, viewportHeight, result.screenStart))
            return result;

        if (!WorldToScreen(end, view, proj, viewportWidth, viewportHeight, result.screenEnd))
            return result;

        if (glm::length(result.screenEnd - result.screenStart) < 2.0f)
            return result;

        result.valid = true;
        return result;
    }

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
        const bool* axisEnabled
    )
    {
        GizmoAxis bestAxis = GizmoAxis::None;
        float bestDistance = std::numeric_limits<float>::max();

        const GizmoAxis axes[3] = {
            GizmoAxis::X,
            GizmoAxis::Y,
            GizmoAxis::Z
        };

        for (int i = 0; i < 3; ++i)
        {
            const GizmoAxis axis = axes[i];

            if (axisEnabled && !axisEnabled[i])
                continue;   // handle not offered in this mode: not drawn, not grabbable

            AxisScreenSegment seg = BuildAxisSegment(
                axis,
                AxisDirInSpace(axis, objectRotation, space),
                origin,
                view,
                proj,
                viewportWidth,
                viewportHeight,
                desiredScreenLengthPx
            );

            if (!seg.valid)
                continue;

            float distance = DistancePointToSegment(mouse, seg.screenStart, seg.screenEnd);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestAxis = axis;
            }
        }

        if (bestDistance <= hitThresholdPx)
            return bestAxis;

        return GizmoAxis::None;
    }

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
    )
    {
        glm::vec3 rayOrigin, rayDir;
        if (!ScreenPointToRay(mouse, view, proj, viewportWidth, viewportHeight, rayOrigin, rayDir))
            return GizmoAxis::None;

        float unitsPerPixel = WorldUnitsPerPixelAt(origin, view, proj, viewportHeight);
        float radius = std::max(unitsPerPixel * desiredScreenRadiusPx, 0.001f);
        float worldThreshold = unitsPerPixel * hitThresholdPx;

        glm::vec3 forward = CameraForward(view);

        GizmoAxis bestAxis = GizmoAxis::None;
        float bestRadial = std::numeric_limits<float>::max();

        const GizmoAxis axes[3] = {
            GizmoAxis::X,
            GizmoAxis::Y,
            GizmoAxis::Z
        };

        for (GizmoAxis axis : axes)
        {
            glm::vec3 normal = AxisDirInSpace(axis, objectRotation, space);

            float t;
            if (!IntersectRayPlane(rayOrigin, rayDir, origin, normal, t))
                continue;   // edge-on ring, or behind the camera

            glm::vec3 hit = rayOrigin + rayDir * t;

            // Only the visible (near) half is grabbable — the shader hides the
            // other half, and picking what you cannot see is disorienting.
            if (glm::dot(hit - origin, forward) > RING_BACK_FACE_TOLERANCE * radius)
                continue;

            float radial = std::abs(glm::length(hit - origin) - radius);

            if (radial < bestRadial)
            {
                bestRadial = radial;
                bestAxis = axis;
            }
        }

        if (bestRadial <= worldThreshold)
            return bestAxis;

        return GizmoAxis::None;
    }

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
    )
    {
        if (glm::dot(axisDir, axisDir) < EPSILON)
            return false;

        AxisScreenSegment seg = BuildAxisSegment(
            GizmoAxis::X,   // only the direction matters here, not the label
            axisDir,
            dragStartObjectPosition,
            view,
            proj,
            viewportWidth,
            viewportHeight,
            desiredScreenLengthPx
        );

        if (!seg.valid)
            return false;

        glm::vec2 screenAxis = seg.screenEnd - seg.screenStart;
        float screenAxisLength = glm::length(screenAxis);

        if (screenAxisLength < 2.0f)
            return false;

        glm::vec2 screenAxisDir = screenAxis / screenAxisLength;
        glm::vec2 mouseDelta = currentMouse - dragStartMouse;

        float projectedPixels = glm::dot(mouseDelta, screenAxisDir);
        float pixelsPerWorldUnit = screenAxisLength / seg.worldLength;

        if (pixelsPerWorldUnit < EPSILON)
            return false;

        float worldDelta = projectedPixels / pixelsPerWorldUnit;

        outPosition = dragStartObjectPosition + seg.worldDir * worldDelta;
        return true;
    }

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
    )
    {
        if (glm::dot(axisDir, axisDir) < EPSILON)
            return false;

        // Same segment the handle is drawn as, rebuilt from the drag-start pose.
        // Anchoring on the start (not the live object) keeps the mapping fixed
        // for the whole drag: as the object grows the handle would otherwise move
        // under the cursor and feed its own growth back in.
        AxisScreenSegment seg = BuildAxisSegment(
            GizmoAxis::X,   // only the direction matters here, not the label
            axisDir,
            dragStartObjectPosition,
            view,
            proj,
            viewportWidth,
            viewportHeight,
            desiredScreenLengthPx
        );

        if (!seg.valid)
            return false;

        glm::vec2 screenAxis = seg.screenEnd - seg.screenStart;
        float screenAxisLength = glm::length(screenAxis);

        if (screenAxisLength < 2.0f)
            return false;   // handle points nearly at the camera: no usable direction

        glm::vec2 screenAxisDir = screenAxis / screenAxisLength;
        float projectedPixels = glm::dot(currentMouse - dragStartMouse, screenAxisDir);

        outFactor = std::max(1.0f + projectedPixels / screenAxisLength, MIN_SCALE_FACTOR);
        return true;
    }
}
