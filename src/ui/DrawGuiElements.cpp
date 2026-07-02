#include "core/Application.h"
#include <imgui.h>
#include "Theme.h"
#include <string>
#include <ImGuizmo.h>
#include "scene/SdfObjectFactory.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
void Application::DrawGui() {
    DrawMainMenuBar();
    SdfObjectData newObject{};

    if (addObjectPanel.Draw(newObject))
    {
        AddObjectToScene(newObject);
    }
    DrawSettings();
    DrawWorkspace();

}

void Application::DrawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                NewScene();
            }

            if (ImGui::MenuItem("Open")) {
                OpenSceneFromSDFO();
            }

            if (ImGui::MenuItem("Save")) {
                SaveSceneToSDFO();
            }

            if (ImGui::MenuItem("Export as")) {
                // Export is not implemented yet.
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Object")) {
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItem("Sphere")) {
                    addObjectPanel.Open(SdfObjectType::Sphere);
                }

                if (ImGui::MenuItem("Box")) {
                    addObjectPanel.Open(SdfObjectType::Box);
                }

                if (ImGui::MenuItem("Cylinder")) {
                    addObjectPanel.Open(SdfObjectType::Cylinder);
                }

                if (ImGui::MenuItem("Torus")) {
                    addObjectPanel.Open(SdfObjectType::Torus);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Select")) {}
            if (ImGui::MenuItem("Copy")) {}
            if (ImGui::MenuItem("Paste")) {}
            if (ImGui::MenuItem("Delete")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Camera")) {}
            if (ImGui::MenuItem("Show/Hide Grid", nullptr, showGrid)) {
                showGrid = !showGrid;
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Settings")) {
            appSettings.show = true;
        }

        ImGui::EndMainMenuBar();
    }
}

void Application::DrawSettings() {
    if (!appSettings.show) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGuiWindowFlags settingsWindowFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x - 50.0f, viewport->WorkSize.y - 30.0f),
        ImGuiCond_Always
    );

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    bool windowOpen = ImGui::Begin("##Settings", &appSettings.show, settingsWindowFlags);
    ImGui::PopStyleVar(2);

    if (windowOpen) {
        //  Title Bar 
        const float titleHeight = 50.0f;

        if (ImGui::BeginChild("SettingsText", ImVec2(0, titleHeight), false)) {
            ImGui::PushFont(appTheme.fontLarge);
            float textHeight = ImGui::GetTextLineHeight();
            ImGui::SetCursorPos(ImVec2(10.0f, (titleHeight - textHeight) / 2.0f));
            ImGui::Text("Settings");
            ImGui::PopFont();

            const float buttonSize = 20.0f;
            ImGui::SetCursorPos(ImVec2(
                ImGui::GetWindowWidth() - buttonSize - 10.0f,
                (titleHeight - buttonSize) / 2.0f
            ));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, appTheme.pWarningRed);
            if (ImGui::Button("X", ImVec2(buttonSize, buttonSize)))
                appSettings.show = false;
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::PushStyleColor(ImGuiCol_Separator, appTheme.border);
        ImGui::Separator();
        ImGui::PopStyleColor();

        //  Left Side Bar 
        if (ImGui::BeginChild("SettingsLeftBar", ImVec2(200, 0), false)) {
            const char* tabs[] = { "General", "Render", "Shortcuts", "UI" };
            const float itemHeight = 35.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            for (int i = 0; i < 4; i++) {
                ImGui::PushID(i);
                ImVec2 pos = ImGui::GetCursorPos();

                if (ImGui::Selectable("", appSettings.selectedTab == i, 0, ImVec2(0, itemHeight)))
                    appSettings.selectedTab = i;

                float textHeight = ImGui::GetTextLineHeight();
                ImGui::SetCursorPos(ImVec2(pos.x + 10.0f, pos.y + (itemHeight - textHeight) / 2.0f));
                ImGui::Text("%s", tabs[i]);
                ImGui::SetCursorPos(ImVec2(pos.x, pos.y + itemHeight));
                ImGui::PopID();
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Sa� ��erik ---
        if (ImGui::BeginChild("SettingsRightContent", ImVec2(0, 0), false)) {
            switch (appSettings.selectedTab) {
            case 0: ImGui::Text("General Settings");    break;
            case 1: ImGui::Text("Render Settings");     break;
            case 2: ImGui::Text("Keyboard Shortcuts");  break;
            case 3: ImGui::Text("UI Settings");         break;
            default: break;
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

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
                renderer.CreateRenderTarget(device, w, h);
                ImTextureID texId = (ImTextureID)(WGPUTextureView)renderer.GetRenderTargetView();
                ImGui::Image(texId, avail);
                // Gizmo connection
                if (selectedObjectIndex >= 0 && selectedObjectIndex < sceneManager.GetObjects().size())
                {
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();

                    // SceneView panelinin ekran konumu/boyutu — gizmo'nun nereye çizileceğini bilmesi için
                    ImVec2 panelPos = ImGui::GetWindowPos();
                    ImVec2 panelSize = ImGui::GetWindowSize();
                    ImGuizmo::SetRect(panelPos.x, panelPos.y, panelSize.x, panelSize.y);

                    SdfObjectData& obj = sceneManager.GetObjects()[selectedObjectIndex];  // mutable erişim lazım!
                    glm::mat4 model = SdfObjectFactory::BuildModelMatrix(obj, ImGuizmo::TRANSLATE);

                    bool changed = ImGuizmo::Manipulate(
                        glm::value_ptr(view),
                        glm::value_ptr(proj),
                        ImGuizmo::TRANSLATE,      // sadece translate ile başla
                        ImGuizmo::WORLD,
                        glm::value_ptr(model)
                    );

                    if (changed)
                    {
                        SdfObjectFactory::ApplyModelMatrix(model, obj, ImGuizmo::TRANSLATE);
                        RefreshSceneBuffer();
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

                if (ImGui::Selectable(label.c_str(), selectedObjectIndex == i))
                {
                    selectedObjectIndex = i;
                }
            }

            ImGui::Separator();

            if (selectedObjectIndex >= 0 && selectedObjectIndex < static_cast<int>(objects.size()))
            {
                SdfObjectData& obj = objects[selectedObjectIndex];

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
                    RefreshSceneBuffer();
                }

                if (ImGui::Button("Delete Object"))
                {
                    objects.erase(objects.begin() + selectedObjectIndex);

                    if (objects.empty())
                        selectedObjectIndex = -1;
                    else if (selectedObjectIndex >= static_cast<int>(objects.size()))
                        selectedObjectIndex = static_cast<int>(objects.size()) - 1;

                    RefreshSceneBuffer();
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