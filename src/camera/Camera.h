#pragma once

// Must be defined before any glm header so glm::perspective produces a
// [0,1] clip-space depth (WebGPU/D3D convention) rather than OpenGL's [-1,1].
// Defining it after the include (as Camera.cpp used to) is a silent no-op.
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "core/Vec2.h"
#include "editor/Settings.h"
class Camera
{
public:
	glm::vec3 GetPosition();
	glm::vec3 GetTargetPosition();
	glm::mat4 GetViewMatrix();
	glm::mat4 GetProjectionMatrix(float aspect);
	void OrbitMovement(Settings appSettings, Vec2 delta);
	void Zoom(Settings appSettings, float delta);
	void TargetPan(Settings appSettings, Vec2 delta);
	void SnapToDirection(const glm::vec3& direction);

	// The orbit angles that look along `direction`, without touching the camera.
	// Lets an animated view change (CameraTransition) compute where it is going
	// before it starts moving. Returns false for a degenerate/non-finite
	// direction, which would normalize to NaN.
	static bool OrientationForDirection(const glm::vec3& direction, float& outYaw, float& outPitch);

	// Apply an orientation directly (pitch clamped to the same limit as orbiting).
	void SetOrientation(float newYaw, float newPitch);

	void FrameBounds(const glm::vec3& center, float radius);
	float GetYaw() const { return yaw; }
	float GetPitch() const { return pitch; }

	// Pitch is clamped just short of straight up/down: at exactly +-90 degrees the
	// view forward vector is parallel to the world up, so cross(forward, up) is
	// zero and both glm::lookAt and the shader's camera basis produce NaNs.
	static constexpr float PITCH_LIMIT = 1.55f;
private:
	float yaw = 0.5f;
	float pitch = 0.5f;
	float distance = 8.0f;
	glm::vec3 targetP = { 0.0f, 0.0f, 0.0f };
};


