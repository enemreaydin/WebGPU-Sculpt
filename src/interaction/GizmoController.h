#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

#include "interaction/GizmoState.h"
#include "interaction/GizmoMath.h"

// Owns the gizmo interaction state machine (hover / begin-drag / drag /
// end-drag) for both the translate and rotate modes, in either the world or the
// object's local frame. Pulled out of Application so the gizmo is self-contained.
//
// It holds no engine references: every call is handed the data it needs (mouse,
// the selected object's world origin and rotation, the camera view/proj, and the
// viewport size). The pure projection/hit-test math stays in GizmoMath; this
// class is only the stateful orchestration around it.
class GizmoController
{
public:
    // Screen length of the axis handles / radius of the rotate rings, and the
    // pick tolerance — both in render px, so the gizmo is a constant size
    // on screen regardless of how far away the object is.
    static constexpr float SCREEN_LENGTH_PX = 96.0f;
    static constexpr float HIT_THRESHOLD_PX = 10.0f;

    bool       IsDragging()  const { return m_state.isDragging; }
    GizmoAxis  HoveredAxis() const { return m_state.hoveredAxis; }
    GizmoAxis  ActiveAxis()  const { return m_state.activeAxis; }
    GizmoMode  Mode()        const { return m_state.mode; }
    GizmoSpace Space()       const { return m_state.space; }

    // Changing mode or space mid-drag would invalidate the latched axis, so both
    // are ignored while a drag is in flight.
    void SetMode(GizmoMode mode)    { if (!m_state.isDragging) m_state.mode = mode; }
    void SetSpace(GizmoSpace space) { if (!m_state.isDragging) m_state.space = space; }

    // Ratio of render-buffer px to display (CSS) px, i.e. the web Render
    // Resolution scale. The base pixel sizes above are authored in display px;
    // multiplying by this keeps the gizmo a constant on-screen size even when
    // the backing store is rendered smaller and upscaled (§10). 1.0 = 100%.
    void  SetPixelScale(float s) { m_pixelScale = (s > 0.0f) ? s : 1.0f; }
    float PixelScale() const     { return m_pixelScale; }

    // Which handles the SCALE gizmo offers. A primitive's size does not depend on
    // every direction (a cone's flare is an angle, a torus' tube is a fixed
    // thickness — see PrimitiveGeometry::ScaleAxisMap), and a handle that cannot
    // move anything is worse than no handle: the user drags it and concludes the
    // editor is broken. The caller refreshes this from the current target before
    // hover / drag / draw; it is ignored in translate and rotate mode, which
    // always offer all three. Latched at BeginDrag so a mid-drag refresh can
    // never pull the axis out from under an active drag.
    void SetScaleAxisMask(bool x, bool y, bool z)
    {
        if (m_state.isDragging) return;
        m_scaleAxisEnabled[0] = x;
        m_scaleAxisEnabled[1] = y;
        m_scaleAxisEnabled[2] = z;
    }
    bool ScaleAxisEnabled(int axisIndex) const
    {
        return axisIndex >= 0 && axisIndex < 3 ? m_scaleAxisEnabled[axisIndex] : false;
    }

    // The frame the handles are aligned to for the ACTIVE mode. Translate and
    // rotate honour the user's World/Local toggle. Scale cannot: the value it
    // edits (EditorObject::scale) is defined per local axis, and scaling a
    // rotated object along a world axis is a shear — which an SdfObjectData
    // (position + quaternion + size) has no way to represent. Rather than apply
    // a world drag to local components and silently produce the wrong shape, the
    // scale gizmo always shows and edits the object's own axes.
    GizmoSpace EffectiveSpace() const
    {
        return (m_state.mode == GizmoMode::Scale) ? GizmoSpace::Local : m_state.space;
    }

    // Recompute the hovered axis under the mouse. While a drag is in progress
    // the active axis stays latched (returns it). Result is the axis as int
    // (0 none, 1 X, 2 Y, 3 Z).
    int Hover(const glm::vec2& mouse, const glm::vec3& origin, const glm::quat& rotation,
              const glm::mat4& view, const glm::mat4& proj, float w, float h)
    {
        if (m_state.isDragging)
            return static_cast<int>(m_state.activeAxis);

        // Translate and scale are the same three straight handles on screen, so
        // they share the segment hit test; only scale masks off axes it cannot
        // offer. Rotate is the ring test instead.
        m_state.hoveredAxis = (m_state.mode == GizmoMode::Rotate)
            ? GizmoMath::HitTestRotateRings(mouse, origin, rotation, m_state.space,
                                            view, proj, w, h, ScreenLengthPx(), HitThresholdPx())
            : GizmoMath::HitTestTranslateAxes(mouse, origin, rotation, EffectiveSpace(),
                                              view, proj, w, h, ScreenLengthPx(), HitThresholdPx(),
                                              (m_state.mode == GizmoMode::Scale) ? m_scaleAxisEnabled : nullptr);

        return static_cast<int>(m_state.hoveredAxis);
    }

    // Latch a drag onto the currently-hovered axis (if the mouse is over one).
    // Anchors on the object's origin, its rotation, and the grabbed axis
    // direction, so the drag has no accumulated drift.
    void BeginDrag(const glm::vec2& mouse, const glm::vec3& origin, const glm::quat& rotation,
                   const glm::mat4& view, const glm::mat4& proj, float w, float h)
    {
        Hover(mouse, origin, rotation, view, proj, w, h);
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
        m_state.dragStartRotation = rotation;
        m_state.dragAxisDir = GizmoMath::AxisDirInSpace(m_state.activeAxis, rotation, EffectiveSpace());

        if (m_state.mode == GizmoMode::Rotate)
        {
            glm::vec3 spoke;
            if (!GizmoMath::ComputeRingSpoke(mouse, origin, m_state.dragAxisDir,
                                             view, proj, w, h, spoke))
            {
                // Grabbed a ring we cannot resolve a spoke on (edge-on, or dead
                // centre). Refuse the drag rather than start it from garbage.
                m_state.activeAxis = GizmoAxis::None;
                m_state.isDragging = false;
                return;
            }
            m_state.dragStartSpoke = spoke;
        }
    }

    // Position of the dragged object along the active axis for the current
    // mouse. Returns false when not translating or the projection is degenerate.
    bool DragTranslate(const glm::vec2& mouse, const glm::mat4& view, const glm::mat4& proj,
                       float w, float h, glm::vec3& outPosition) const
    {
        if (!m_state.isDragging || m_state.mode != GizmoMode::Translate ||
            m_state.activeAxis == GizmoAxis::None)
            return false;

        return GizmoMath::ComputeTranslateDragPosition(
            m_state.dragAxisDir, m_state.dragStartMouse, mouse,
            m_state.dragStartObjectPosition, view, proj, w, h,
            ScreenLengthPx(), outPosition);
    }

    // Absolute rotation of the dragged object for the current mouse: the angle
    // swept between the grabbed spoke and the current one, applied on top of the
    // rotation the object had when the drag began.
    bool DragRotate(const glm::vec2& mouse, const glm::mat4& view, const glm::mat4& proj,
                    float w, float h, glm::quat& outRotation) const
    {
        if (!m_state.isDragging || m_state.mode != GizmoMode::Rotate ||
            m_state.activeAxis == GizmoAxis::None)
            return false;

        glm::vec3 spoke;
        if (!GizmoMath::ComputeRingSpoke(mouse, m_state.dragStartObjectPosition,
                                         m_state.dragAxisDir, view, proj, w, h, spoke))
            return false;   // ray went parallel to the ring plane; hold the last pose

        float angle = GizmoMath::SignedAngleAround(m_state.dragStartSpoke, spoke, m_state.dragAxisDir);
        glm::quat delta = glm::angleAxis(angle, m_state.dragAxisDir);

        // Left-multiply: dragStartRotation acts first, then the delta about the
        // (world-space) handle direction. Right-multiplying would instead turn
        // the object about its own axes, which is not what these handles show.
        outRotation = glm::normalize(delta * m_state.dragStartRotation);
        return true;
    }

    // Multiplier to apply to the scale the object had when the drag began.
    // Absolute-from-start like the other two drags, so there is no accumulated
    // drift and releasing/undoing lands exactly back on the start value.
    bool DragScale(const glm::vec2& mouse, const glm::mat4& view, const glm::mat4& proj,
                   float w, float h, float& outFactor) const
    {
        if (!m_state.isDragging || m_state.mode != GizmoMode::Scale ||
            m_state.activeAxis == GizmoAxis::None)
            return false;

        return GizmoMath::ComputeScaleDragFactor(
            m_state.dragAxisDir, m_state.dragStartMouse, mouse,
            m_state.dragStartObjectPosition, view, proj, w, h,
            ScreenLengthPx(), outFactor);
    }

    void EndDrag()
    {
        m_state.isDragging = false;
        m_state.activeAxis = GizmoAxis::None;
        m_state.hoveredAxis = GizmoAxis::None;
    }

    // World-space endpoint of one axis handle at the constant on-screen length.
    // Fed to the shader (via GlobalsData) so the gizmo renders in the same frame
    // as the SDF. In rotate mode the shader reads the same three endpoints as
    // each ring's normal and radius. h is the viewport height in render px.
    glm::vec3 AxisEndWorld(GizmoAxis axis, const glm::vec3& origin, const glm::quat& rotation,
                           const glm::mat4& view, const glm::mat4& proj, float h) const
    {
        float unitsPerPixel = GizmoMath::WorldUnitsPerPixelAt(origin, view, proj, h);
        float worldLength = std::max(unitsPerPixel * ScreenLengthPx(), 0.001f);
        return origin + GizmoMath::AxisDirInSpace(axis, rotation, EffectiveSpace()) * worldLength;
    }

private:
    // Base pixel sizes scaled into render-buffer px for the current resolution.
    float ScreenLengthPx() const { return SCREEN_LENGTH_PX * m_pixelScale; }
    float HitThresholdPx() const { return HIT_THRESHOLD_PX * m_pixelScale; }

    GizmoState m_state;
    float      m_pixelScale = 1.0f;
    bool       m_scaleAxisEnabled[3] = { true, true, true };
};
