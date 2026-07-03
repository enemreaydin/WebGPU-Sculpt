#include "camera/CameraController.h"
#include <ImGuizmo.h>

void CameraController::Update(Camera& camera, Settings appSettings, bool sceneViewHovered, bool noModifiersPressed, ImVec2 sceneCursorDelta, float sceneScrollDelta) {
    // Scene operations
    bool gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (sceneViewHovered && !gizmoActive)
    {
        // Orbit movement
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && noModifiersPressed){
            camera.OrbitMovement(appSettings, sceneCursorDelta);

        }
        //Zoom
        if (sceneScrollDelta != 0.0f && noModifiersPressed)
        {
            camera.Zoom(appSettings, sceneScrollDelta);
        }
        // Target change(pan)
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt)
        {
            camera.TargetPan(appSettings, sceneCursorDelta);
        }
    }
}
