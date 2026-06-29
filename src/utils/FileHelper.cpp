#include "FileHelper.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string Utils::ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Hata: Dosya bulunamadi: " << path << "\n";
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}