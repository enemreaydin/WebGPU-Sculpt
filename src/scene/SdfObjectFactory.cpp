#include "SdfObjectFactory.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp> 

SdfObjectData SdfObjectFactory::CreateSphere(float radius)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Sphere);
    obj.op = static_cast<int32_t>(SdfOperationType::Union);

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.size = { radius, 0.0f, 0.0f, 0.0f };
    obj.color = { 0.0f, 0.45f, 0.85f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateBox(float x, float y, float z)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Box);
    obj.op = static_cast<int32_t>(SdfOperationType::Union);

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.size = { x, y, z, 0.0f };
    obj.color = { 0.8f, 0.2f, 0.1f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateCylinder(float radius, float height)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Cylinder);
    obj.op = static_cast<int32_t>(SdfOperationType::Union);

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.size = { radius, height, 0.0f, 0.0f };
    obj.color = { 0.2f, 0.8f, 0.3f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateTorus(float majorRadius, float minorRadius)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Torus);
    obj.op = static_cast<int32_t>(SdfOperationType::Union);

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };

    // size.x = major radius, size.y = minor radius
    obj.size = { majorRadius, minorRadius, 0.0f, 0.0f };

    obj.color = { 0.9f, 0.6f, 0.1f, 0.0f };

    return obj;
}


glm::mat4 SdfObjectFactory::BuildModelMatrix(const SdfObjectData& obj) {
    glm::vec3 pos(obj.position[0], obj.position[1], obj.position[2]);
    glm::vec3 scale(obj.size[0], obj.size[1], obj.size[2]);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
    model = glm::scale(model, scale);
    return model;
}

void SdfObjectFactory::ApplyModelMatrix(const glm::mat4& matrix, SdfObjectData& obj) {
    glm::vec3 translation, scale, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    glm::decompose(matrix, scale, rotation, translation, skew, perspective);

    obj.position[0] = translation.x;
    obj.position[1] = translation.y;
    obj.position[2] = translation.z;

    obj.size[0] = scale.x;
    obj.size[1] = scale.y;
    obj.size[2] = scale.z;
}