#include "core/Application.h"
#include <imgui.h>
#include "Theme.h"

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
            if (ImGui::MenuItem("Open")) {}
            if (ImGui::MenuItem("New")) {}
            if (ImGui::MenuItem("Save")) {}
            if (ImGui::MenuItem("Export as")) {}
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
            if (ImGui::MenuItem("Show/Hide Grid")) {}
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
            ImVec2 avail = ImGui::GetContentRegionAvail();
            sceneViewSize = avail;
            uint32_t w = static_cast<uint32_t>(avail.x);
            uint32_t h = static_cast<uint32_t>(avail.y);

            if (w > 0 && h > 0) {
                renderer.CreateRenderTarget(device, w, h);
                ImTextureID texId = (ImTextureID)(WGPUTextureView)renderer.GetRenderTargetView();
                ImGui::Image(texId, avail);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("SceneProperties", ImVec2(300, 0), true)) {
            ImGui::Text("Nesne ayarlari");
        }
        ImGui::EndChild(); 
    }
    ImGui::PopStyleVar(2);
    ImGui::End();
}