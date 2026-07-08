#pragma once

#include <glm/glm.hpp>
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

    bool WorldToScreen(
        const glm::vec3& worldPos,
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        glm::vec2& outScreen
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
        const glm::mat4& view,
        const glm::mat4& proj,
        float viewportWidth,
        float viewportHeight,
        float desiredScreenLengthPx,
        float hitThresholdPx
    );

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
    );
}