#pragma once
#include <glm/glm.hpp>  
#include "../core/SdfDefinitions.h"

class Camera
{
public:
	glm::vec3 GetPosition();
	Camera();
private:
	float yaw;
	float pitch;
	float distance;
	glm::vec3 targetP = { 0.0f, 0.0f, 0.0f };
};


