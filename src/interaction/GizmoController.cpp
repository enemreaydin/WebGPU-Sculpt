#include "interaction/GizmoController.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

glm::mat4 GizmoController::BuildModelMatrix(const SdfObjectData& obj, ImGuizmo::OPERATION mode)
{
    glm::vec3 pos(obj.position[0], obj.position[1], obj.position[2]);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);

    if (mode == ImGuizmo::SCALE)
    {
        glm::vec3 scale(obj.size[0], obj.size[1], obj.size[2]);
        model = glm::scale(model, scale);
    }

    return model;
}

void GizmoController::ApplyModelMatrix(const glm::mat4& matrix, SdfObjectData& obj, ImGuizmo::OPERATION mode)
{
    if (mode == ImGuizmo::TRANSLATE)
    {
        glm::vec3 translation = glm::vec3(matrix[3]);   // 4. column = translation
        obj.position[0] = translation.x;
        obj.position[1] = translation.y;
        obj.position[2] = translation.z;
    }
    else if (mode == ImGuizmo::SCALE)
    {
        glm::vec3 scale(
            glm::length(glm::vec3(matrix[0])),
            glm::length(glm::vec3(matrix[1])),
            glm::length(glm::vec3(matrix[2]))
        );
        obj.size[0] = scale.x;
        obj.size[1] = scale.y;
        obj.size[2] = scale.z;
    }
    // ROTATE not yet
}
