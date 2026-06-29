#include "FileHelper.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace Utils {

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);

        if (!file.is_open())
        {
            std::cerr << "Dosya acilamadi:\n"
                << path << std::endl;

            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        return buffer.str();
    }
}