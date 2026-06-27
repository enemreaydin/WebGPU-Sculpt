#include "Application.h"
#include <imgui.h>


void Application::DrawGui() {
    DrawMainMenuBar();
}

// Üstte sabit duran araç çubuðu - placeholder þuanlýk
void Application::DrawMainMenuBar() {
    // Ana viewport'u al (tüm pencere alaný)
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Toolbar'ý viewport'un sol üstüne yapýþtýr
    ImGui::SetNextWindowPos(viewport->WorkPos);
    // Geniþlik tüm ekran, yükseklik sabit (50 px)
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 50));

    // Pencereyi sabitleyen flag'ler
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove          // taþýnamaz
        | ImGuiWindowFlags_NoResize      // boyutlandýrýlamaz
        | ImGuiWindowFlags_NoTitleBar    // baþlýk çubuðu yok
        | ImGuiWindowFlags_NoScrollbar   // kaydýrma çubuðu yok
        | ImGuiWindowFlags_NoCollapse;   // katlanamaz

    ImGui::Begin("Toolbar", nullptr, flags);

    // --- Butonlar yan yana ---
    if (ImGui::Button("Kure")) {
        // küre ekleme iþlemi buraya
    }
    ImGui::SameLine();  // sonraki öðeyi yan yana koy

    if (ImGui::Button("Kutu")) {
        // kutu ekleme
    }
    ImGui::SameLine();

    if (ImGui::Button("Silindir")) {
        // silindir ekleme
    }
    ImGui::SameLine();

    // Araya boþluk koymak için
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();

    if (ImGui::Button("Kaydet")) {
        // kaydet
    }
    ImGui::SameLine();

    if (ImGui::Button("Yukle")) {
        // yükle
    }

    ImGui::End();
}