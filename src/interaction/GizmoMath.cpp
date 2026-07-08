#include "interaction/GizmoMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float EPSILON = 0.00001f;
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
        result.worldDir = AxisToWorldDir(axis);

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
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx,
        float hitThresholdPx
    )
    {
        GizmoAxis bestAxis = GizmoAxis::None;
        float bestDistance = std::numeric_limits<float>::max();

        const GizmoAxis axes[3] = {
            GizmoAxis::X,
            GizmoAxis::Y,
            GizmoAxis::Z
        };

        for (GizmoAxis axis : axes)
        {
            AxisScreenSegment seg = BuildAxisSegment(
                axis,
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

    bool ComputeTranslateDragPosition(
        GizmoAxis axis,
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
        if (axis == GizmoAxis::None)
            return false;

        AxisScreenSegment seg = BuildAxisSegment(
            axis,
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
}