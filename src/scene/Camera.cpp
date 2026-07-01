#include "Camera.h"

Camera::Camera() {
	yaw = 0.5f;
	pitch = 0.5f;
	distance = 8.0f;
	targetP = { 0.0f, 0.0f, 0.0f };
}

glm::vec3 Camera::GetPosition() {
	float x = targetP.x + distance * cos(pitch) * sin(yaw);
	float y = targetP.y + distance * sin(pitch);
	float z = targetP.z + distance * cos(pitch) * cos(yaw);
	return glm::vec3(x, y, z);
}
