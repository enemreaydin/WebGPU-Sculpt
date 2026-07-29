#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>

// A time-based ease of the orbit camera's orientation (yaw / pitch), used by the
// navigation-gizmo axis views: instead of teleporting to the axis, the camera
// glides into it over a fraction of a second so the eye can follow which way the
// scene turned.
//
// Like GizmoController it holds no engine references — it is pure math plus a
// clock. The caller passes the start and end orientation in and feeds the
// interpolated result back into the Camera, which keeps this reusable for any
// future animated view change (framing the selection, saved views, ...).
//
// It owns its own clock rather than taking a delta time because the engine has
// no frame-time plumbing; steady_clock is monotonic and available under
// Emscripten, so a paused/backgrounded tab simply resumes at the correct point
// on the curve instead of replaying skipped frames.
class CameraTransition
{
public:
    // Feels responsive without being a jump; roughly the duration Blender and
    // similar editors use for their view-axis transitions.
    static constexpr float DEFAULT_DURATION_S = 0.08f;

    // Below this the move is visually instantaneous. Animating it would only
    // keep the render-on-demand loop awake for a third of a second redrawing
    // the same image (see CLAUDE.md 6a).
    static constexpr float MIN_DELTA_RAD = 1e-3f;

    // Begin easing from the camera's current orientation towards the target one.
    // Returns false when the move is too small to be worth animating — the
    // caller should then apply the target orientation directly.
    //
    // Restarting mid-flight is safe: the caller passes the camera's *current*
    // (already interpolated) orientation, so a second click simply re-eases from
    // wherever the first run had reached.
    bool Start(float fromYaw, float fromPitch, float toYaw, float toPitch,
               float durationSeconds = DEFAULT_DURATION_S)
    {
        // Yaw is an angle, so the raw difference may describe the long way round
        // the circle (+170 deg -> -170 deg is a 20 deg turn, not 340). Rewriting
        // the target into the revolution nearest the start makes the camera
        // always take the short path.
        const float endYaw = fromYaw + ShortestAngleDelta(toYaw - fromYaw);

        if (std::abs(endYaw - fromYaw) < MIN_DELTA_RAD &&
            std::abs(toPitch - fromPitch) < MIN_DELTA_RAD)
        {
            m_active = false;
            return false;
        }

        m_startYaw = fromYaw;
        m_startPitch = fromPitch;
        m_endYaw = endYaw;
        m_endPitch = toPitch;
        m_duration = std::max(durationSeconds, 1e-3f);   // never divide by zero
        m_startTime = std::chrono::steady_clock::now();
        m_active = true;
        return true;
    }

    bool IsActive() const { return m_active; }
    void Cancel() { m_active = false; }

    // Sample the curve at the current wall-clock time and write the eased
    // orientation. Returns false when nothing is running, so the caller leaves
    // the camera untouched. The final sample writes the exact target and clears
    // the active flag, so the animation can never leave the camera slightly off
    // the requested axis.
    bool Advance(float& outYaw, float& outPitch)
    {
        if (!m_active) return false;

        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m_startTime).count();

        float t = elapsed / m_duration;
        if (!(t > 0.0f)) t = 0.0f;              // also catches a NaN clock reading
        if (t >= 1.0f) { t = 1.0f; m_active = false; }

        const float e = EaseInOut(t);
        outYaw = m_startYaw + (m_endYaw - m_startYaw) * e;
        outPitch = m_startPitch + (m_endPitch - m_startPitch) * e;
        return true;
    }

private:
    // Wrap an angle difference into [-pi, pi]: the short way round the circle.
    static float ShortestAngleDelta(float delta)
    {
        constexpr float pi = 3.14159265358979323846f;
        constexpr float twoPi = 2.0f * pi;
        delta = std::fmod(delta + pi, twoPi);
        if (delta < 0.0f) delta += twoPi;
        return delta - pi;
    }

    // Smoothstep: zero velocity at both ends, so the move starts and stops
    // without a visible snap. A linear ramp reads as mechanical, and a spring /
    // overshoot curve would push the view past the axis it was asked for.
    static float EaseInOut(float t) { return t * t * (3.0f - 2.0f * t); }

    bool m_active = false;
    float m_startYaw = 0.0f;
    float m_startPitch = 0.0f;
    float m_endYaw = 0.0f;
    float m_endPitch = 0.0f;
    float m_duration = DEFAULT_DURATION_S;
    std::chrono::steady_clock::time_point m_startTime{};
};
