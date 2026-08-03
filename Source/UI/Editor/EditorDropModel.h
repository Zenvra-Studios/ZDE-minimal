#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace Zenvra::UI::Editor
{

class EditorDropModel
{
public:
    static constexpr std::size_t maximum_dropped_files = 64;

    [[nodiscard]] static std::vector<std::filesystem::path> collect_files(
        std::span<const std::filesystem::path> dropped_paths);
};

} // namespace Zenvra::UI::Editor
