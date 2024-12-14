#pragma once

#include "vulkan_template/app/PlatformWindow.hpp"
#include <filesystem>
#include <string>

namespace vkt
{
// Format the filter like ".TXT;.DOC;.BAK" to filter out files that can be
// selected
auto openFile(std::string const& title, PlatformWindow const& parent)
    -> std::optional<std::filesystem::path>;
auto openFiles(std::string const& title, PlatformWindow const& parent)
    -> std::vector<std::filesystem::path>;
auto openDirectory(std::string const& title, PlatformWindow const& parent)
    -> std::optional<std::filesystem::path>;
auto openDirectories(std::string const& title, PlatformWindow const& parent)
    -> std::vector<std::filesystem::path>;
} // namespace vkt