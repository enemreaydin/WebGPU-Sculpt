#include "Camera.h"
#include <cmath>

glm::vec3 Camera::GetPosition() {
	float x = targetP.x + distance * cos(pitch) * sin(yaw);
	float y = targetP.y + distance * sin(pitch);
	float z = targetP.z + distance * cos(pitch) * cos(yaw);
	return glm::vec3(x, y, z);
}
glm::vec3 Camera::GetTargetPosition() {
	return targetP;
}
void Camera::OrbitMovement(Settings appSettings, Vec2 delta) {
	float sensitivity = appSettings.orbitSensitivity;
	yaw = yaw - delta.x * sensitivity;
	pitch = pitch + delta.y * sensitivity;
	if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
	if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;
}

void Camera::Zoom(Settings appSettings, float delta) {
	float sensitivity = appSettings.zoomSensitivity;
	distance -= delta * sensitivity;
}

void Camera::TargetPan(Settings appSettings, Vec2 delta) {
	float panSensitivity = appSettings.panSensitivity;

	glm::vec3 forward = glm::normalize(targetP - GetPosition());
	glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 up = glm::cross(right, forward);

	targetP += -right * delta.x * panSensitivity + up * delta.y * panSensitivity;
}

bool Camera::OrientationForDirection(const glm::vec3& direction, float& outYaw, float& outPitch) {
	const float lengthSq = glm::dot(direction, direction);
	// A zero-length or non-finite direction would normalize to NaN and poison
	// yaw/pitch for the rest of the session; the caller keeps its current view.
	if (!std::isfinite(lengthSq) || lengthSq < 1e-12f) return false;
	const glm::vec3 d = direction / sqrt(lengthSq);
	outPitch = glm::clamp(asin(glm::clamp(d.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
	outYaw = atan2(d.x, d.z);
	return true;
}

void Camera::SetOrientation(float newYaw, float newPitch) {
	yaw = newYaw;
	pitch = glm::clamp(newPitch, -PITCH_LIMIT, PITCH_LIMIT);
}

void Camera::SnapToDirection(const glm::vec3& direction) {
	float newYaw = 0.0f;
	float newPitch = 0.0f;
	if (OrientationForDirection(direction, newYaw, newPitch)) SetOrientation(newYaw, newPitch);
}

void Camera::FrameBounds(const glm::vec3& center, float radius) {
	if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
		!std::isfinite(radius) || radius <= 0.0f) return;
	targetP = center;
	// The camera uses a fixed 45 degree vertical FOV. A small margin keeps the
	// bounding sphere clear of the viewport edges without changing orientation.
	distance = glm::max(0.1f, radius / sin(glm::radians(22.5f)) * 1.15f);
}

// Helper functions to integrate Gizmo
glm::mat4 Camera::GetViewMatrix() {
	glm::vec3 eye = GetPosition();
	return glm::lookAt(eye, targetP, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) {
	return glm::perspective(
		glm::radians(45.0f),   // vertical field of view
		aspect,                // aspect ratio (passed in)
		0.1f,                  // near plane
		100.0f                 // far plane
	);
}
