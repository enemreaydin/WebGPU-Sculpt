#include "editor/Application.h"
#include <imgui.h>

void Application::DrawMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                sceneSerializer.NewScene();
            }

            if (ImGui::MenuItem("Open")) {
                sceneSerializer.OpenSceneFromSDFO();
            }

            if (ImGui::MenuItem("Save")) {
                sceneSerializer.SaveSceneToSDFO();
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
