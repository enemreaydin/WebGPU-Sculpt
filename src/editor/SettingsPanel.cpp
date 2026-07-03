#include "editor/Application.h"
#include <imgui.h>

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

        // Right-side content
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
