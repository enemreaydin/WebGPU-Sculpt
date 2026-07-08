#include "Camera.h"

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
	const float limit = 1.55f;
	float sensitivity = appSettings.orbitSensitivity;
	yaw = yaw - delta.x * sensitivity;
	pitch = pitch + delta.y * sensitivity;
	if (pitch > limit) pitch = limit;
	if (pitch < -limit) pitch = -limit;
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
