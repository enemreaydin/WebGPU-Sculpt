#pragma once
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
private:
	float yaw = 0.5f;
	float pitch = 0.5f;
	float distance = 8.0f;
	glm::vec3 targetP = { 0.0f, 0.0f, 0.0f };
};


