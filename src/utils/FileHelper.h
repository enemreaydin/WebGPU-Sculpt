#pragma once

#include <string>
#include <filesystem>

namespace Utils
{
    // Proje kökünü (assets ve src klasörlerini içeren dizini) bulur.
    std::filesystem::path FindProjectRoot();

    // Dosyayý verilen tam yoldan okur.
    std::string ReadFile(const std::filesystem::path& path);
}