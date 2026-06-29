#include "core/Application.h"
#include <imgui.h>


void Application::DrawGui() {
    DrawMainMenuBar();
    DrawSettings();

}

void Application::DrawMainMenuBar() {
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open")) {

            }
            if (ImGui::MenuItem("New")) {

            }
            if (ImGui::MenuItem("Save")) {

            }
            if (ImGui::MenuItem("Export as")) {

            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Object"))
        {
            if (ImGui::BeginMenu("New")) {
                if (ImGui::MenuItem("Sphere")) {
                    // küre ekle
                }
                if (ImGui::MenuItem("Box")) {
                    // kutu ekle
                }
                if (ImGui::MenuItem("Cylinder")) {
                    // silindir ekle
                }
                if (ImGui::MenuItem("Torus")) {
                    // torus ekle
                }
                ImGui::EndMenu();  
            }
            if (ImGui::MenuItem("Select")) {

            }
            if (ImGui::MenuItem("Copy")) {

            }
            if (ImGui::MenuItem("Paste")) {

            }
            if (ImGui::MenuItem("Delete")) {

            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Reset Camera")) {

            }
            if (ImGui::MenuItem("Show/Hide Grid")) {

            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Settings"))
        {
            appSettings.show = true;
        }
        ImGui::EndMainMenuBar();
    }
    
}

void Application::DrawSettings() {
    
    if (!appSettings.show) return;

    ImGuiWindowFlags settingsWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - 50, viewport->WorkSize.y - 30), ImGuiCond_Appearing);
    if (ImGui::Begin("Settings", &appSettings.show, settingsWindowFlags))
    {
        // Menu Bar
        if (ImGui::BeginChild("SettingsLeftBar", ImVec2(150, 0), false))
        {
            const char* tabs[] = { "General", "Render", "UI" };
            for (int i = 0; i < 3; i++)
            {
                if (ImGui::Selectable(tabs[i], appSettings.selectedTab == i))
                {
                    appSettings.selectedTab = i;
                }
            }
            ImGui::EndChild();
        }
        ImGui::SameLine();
        // Content
        if (ImGui::BeginChild("SettingsRightContent", ImVec2(0, 0), false))
        {
            if (appSettings.selectedTab == 0)
            {
                ImGui::Text("General Settings");
            }

            if (appSettings.selectedTab == 1)
            {
                ImGui::Text("Render Settings");

            }
            if (appSettings.selectedTab == 2)
            {
                ImGui::Text("UI Settings");

            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}


