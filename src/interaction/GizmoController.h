#pragma once

#include <glm/glm.hpp>
#include <algorithm>

#include "interaction/GizmoState.h"
#include "interaction/GizmoMath.h"

// Owns the translate-gizmo interaction state machine (hover / begin-drag /
// drag / end-drag), pulled out of Application so the gizmo is self-contained.
//
// It holds no engine references: every call is handed the data it needs (mouse,
// the selected object's world origin, the camera view/proj, and the viewport
// size). The pure screen-space projection/hit-test math stays in GizmoMath;
// this class is only the stateful orchestration around it.
class GizmoController
{
public:
    // Screen length of the axis handles and the pick tolerance, in render px.
    static constexpr float SCREEN_LENGTH_PX = 96.0f;
    static constexpr float HIT_THRESHOLD_PX = 10.0f;

    bool      IsDragging()  const { return m_state.isDragging; }
    GizmoAxis HoveredAxis() const { return m_state.hoveredAxis; }
    GizmoAxis ActiveAxis()  const { return m_state.activeAxis; }

    // Recompute the hovered axis under the mouse. While a drag is in progress
    // the active axis stays latched (returns it). Result is the axis as int
    // (0 none, 1 X, 2 Y, 3 Z).
    int Hover(const glm::vec2& mouse, const glm::vec3& origin,
              const glm::mat4& view, const glm::mat4& proj, float w, float h)
    {
        if (m_state.isDragging)
            return static_cast<int>(m_state.activeAxis);

        m_state.hoveredAxis = GizmoMath::HitTestTranslateAxes(
            mouse, origin, view, proj, w, h, SCREEN_LENGTH_PX, HIT_THRESHOLD_PX);
        return static_cast<int>(m_state.hoveredAxis);
    }

    // Latch a drag onto the currently-hovered axis (if the mouse is over one).
    // Anchors on the object's origin so the drag has no accumulated drift.
    void BeginDrag(const glm::vec2& mouse, const glm::vec3& origin,
                   const glm::mat4& view, const glm::mat4& proj, float w, float h)
    {
        Hover(mouse, origin, view, proj, w, h);
        if (m_state.hoveredAxis == GizmoAxis::None)
        {
            m_state.activeAxis = GizmoAxis::None;
            m_state.isDragging = false;
            return;
        }
        m_state.activeAxis = m_state.hoveredAxis;
        m_state.isDragging = true;
        m_state.dragStartMouse = mouse;
        m_state.dragStartObjectPosition = origin;
    }

    // Position of the dragged object along the active axis for the current
    // mouse. Returns false when not dragging or the projection is degenerate.
    bool Drag(const glm::vec2& mouse, const glm::mat4& view, const glm::mat4& proj,
              float w, float h, glm::vec3& outPosition) const
    {
        if (!m_state.isDragging || m_state.activeAxis == GizmoAxis::None)
            return false;

        return GizmoMath::ComputeTranslateDragPosition(
            m_state.activeAxis, m_state.dragStartMouse, mouse,
            m_state.dragStartObjectPosition, view, proj, w, h,
            SCREEN_LENGTH_PX, outPosition);
    }

    void EndDrag()
    {
        m_state.isDragging = false;
        m_state.activeAxis = GizmoAxis::None;
        m_state.hoveredAxis = GizmoAxis::None;
    }

    // World-space endpoint of one axis handle at the constant on-screen length.
    // Fed to the shader (via GlobalsData) so the gizmo renders in the same frame
    // as the SDF. h is the viewport height in render px.
    glm::vec3 AxisEndWorld(GizmoAxis axis, const glm::vec3& origin,
                           const glm::mat4& view, const glm::mat4& proj, float h) const
    {
        float unitsPerPixel = GizmoMath::WorldUnitsPerPixelAt(origin, view, proj, h);
        float worldLength = std::max(unitsPerPixel * SCREEN_LENGTH_PX, 0.001f);
        return origin + GizmoMath::AxisToWorldDir(axis) * worldLength;
    }

private:
    GizmoState m_state;
};
