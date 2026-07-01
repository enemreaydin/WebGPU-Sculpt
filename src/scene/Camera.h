#pragma once
#include <glm/glm.hpp>  
#include <imgui.h>
#include "../core/Settings.h"
class Camera
{
public:
	glm::vec3 GetPosition();
	void OrbitMovement(Settings appSettings, ImVec2 delta);
private:
	float yaw = 0.5f;
	float pitch = 0.5f;
	float distance = 8.0f;
	glm::vec3 targetP = { 0.0f, 0.0f, 0.0f };
};


