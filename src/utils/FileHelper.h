#pragma once

#include <string>
#include <filesystem>

namespace Utils
{
    // Reads a file at the given path and returns its contents.
    std::string ReadFile(const std::filesystem::path& path);
}