#include "Camera.h"

glm::vec3 Camera::GetPosition() {
	float x = targetP.x + distance * cos(pitch) * sin(yaw);
	float y = targetP.y + distance * sin(pitch);
	float z = targetP.z + distance * cos(pitch) * cos(yaw);
	return glm::vec3(x, y, z);
}

void Camera::OrbitMovement(Settings appSettings, ImVec2 delta) {
	const float limit = 1.55f;
	float sensitivity = appSettings.orbitSensitivity;
	yaw = yaw - delta.x * sensitivity;
	pitch = pitch + delta.y * sensitivity;
	if (pitch > limit) pitch = limit;
	if (pitch < -limit) pitch = -limit;
}