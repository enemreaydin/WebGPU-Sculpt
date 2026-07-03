#include "editor/Application.h"
#include <imgui.h>
#include <string>
#include <ImGuizmo.h>
#include "interaction/GizmoController.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

void Application::DrawWorkspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Workspace", nullptr, flags)) {
        // SDF Render Sahnesi
        if (ImGui::BeginChild("SceneView", ImVec2(-300, 0), true)) {

            // Scene movement checks
            sceneViewHovered = ImGui::IsWindowHovered();
            sceneCursorDelta = ImGui::GetIO().MouseDelta;
            sceneScrollDelta = ImGui::GetIO().MouseWheel;


            ImVec2 avail = ImGui::GetContentRegionAvail();
            sceneViewSize = avail;
            uint32_t w = static_cast<uint32_t>(avail.x);
            uint32_t h = static_cast<uint32_t>(avail.y);

            if (w > 0 && h > 0) {
                renderer.CreateRenderTarget(graphics.device, w, h);
                ImTextureID texId = (ImTextureID)(WGPUTextureView)renderer.GetRenderTargetView();
                ImGui::Image(texId, avail);
                // Gizmo connection
                if (selectionState.selectedObjectIndex >= 0 && selectionState.selectedObjectIndex < sceneManager.GetObjects().size())
                {
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();

                    // Tell the gizmo where the SceneView panel sits on screen.
                    ImVec2 panelPos = ImGui::GetWindowPos();
                    ImVec2 panelSize = ImGui::GetWindowSize();
                    ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

                    SdfObjectData& obj = sceneManager.GetObjects()[selectionState.selectedObjectIndex];  // needs mutable access
                    glm::mat4 model = GizmoController::BuildModelMatrix(obj, ImGuizmo::TRANSLATE);

                    bool changed = ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(proj),
                        ImGuizmo::TRANSLATE,      // translate only for now
                        ImGuizmo::WORLD,
                        glm::value_ptr(model)
                    );

                    if (changed)
                    {
                        GizmoController::ApplyModelMatrix(model, obj, ImGuizmo::TRANSLATE);
                        sceneSerializer.RefreshSceneBuffer();
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("SceneProperties", ImVec2(300, 0), true))
        {
            ImGui::Text("Scene Objects");
            ImGui::Separator();

            auto& objects = sceneManager.GetObjects();

            for (int i = 0; i < static_cast<int>(objects.size()); i++)
            {
                std::string label = "Object " + std::to_string(i);

                if (objects[i].type == static_cast<int32_t>(SdfObjectType::Sphere))
                    label = "Sphere " + std::to_string(i);
                else if (objects[i].type == static_cast<int32_t>(SdfObjectType::Box))
                    label = "Box " + std::to_string(i);
                else if (objects[i].type == static_cast<int32_t>(SdfObjectType::Cylinder))
                    label = "Cylinder " + std::to_string(i);
                else if (objects[i].type == static_cast<int32_t>(SdfObjectType::Torus))
                    label = "Torus " + std::to_string(i);

                if (ImGui::Selectable(label.c_str(), selectionState.selectedObjectIndex == i))
                {
                    selectionState.selectedObjectIndex = i;
                }
            }

            ImGui::Separator();

            if (selectionState.selectedObjectIndex >= 0 && selectionState.selectedObjectIndex < static_cast<int>(objects.size()))
            {
                SdfObjectData& obj = objects[selectionState.selectedObjectIndex];

                ImGui::Text("Selected Object");
                ImGui::Separator();

                bool changed = false;

                changed |= ImGui::InputFloat3("Position", obj.position.data());
                changed |= ImGui::InputFloat3("Size", obj.size.data());
                changed |= ImGui::ColorEdit3("Color", obj.color.data());

                ImGui::Separator();

                const char* operations[] = {
                    "None",
                    "Union",
                    "Subtract",
                    "Intersection"
                };

                int currentOp = obj.op;

                if (ImGui::Combo("Operation", &currentOp, operations, IM_ARRAYSIZE(operations)))
                {
                    obj.op = currentOp;
                    changed = true;
                }

                if (changed)
                {
                    sceneSerializer.RefreshSceneBuffer();
                }

                if (ImGui::Button("Delete Object"))
                {
                    objects.erase(objects.begin() + selectionState.selectedObjectIndex);

                    if (objects.empty())
                        selectionState.selectedObjectIndex = -1;
                    else if (selectionState.selectedObjectIndex >= static_cast<int>(objects.size()))
                        selectionState.selectedObjectIndex = static_cast<int>(objects.size()) - 1;

                    sceneSerializer.RefreshSceneBuffer();
                }
            }
            else
            {
                ImGui::Text("No object selected.");
            }
        }
        ImGui::EndChild();
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
}
