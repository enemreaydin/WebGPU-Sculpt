#include "Camera.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

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


// Helper functions to integrate Gizmo
glm::mat4 Camera::GetViewMatrix() {
	glm::vec3 eye = GetPosition();
	return glm::lookAt(eye, targetP, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) {
	return glm::perspective(
		glm::radians(45.0f),   // fov: dikey görüþ açýsý (radyan)
		aspect,                // en/boy oraný (dýþarýdan gelir)
		0.1f,                  // near: en yakýn kýrpma düzlemi
		100.0f                 // far: en uzak kýrpma düzlemi
	);
}