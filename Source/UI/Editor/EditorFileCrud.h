#pragma once

#include "UI/Editor/EditorFileSystem.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Zenvra::UI::Editor
{

class EditorFileCrud
{
public:
    [[nodiscard]] std::optional<TextFileSnapshot> read(
        const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<TextFileSnapshot> create(
        const std::filesystem::path& path) const;
    [[nodiscard]] bool update(
        const std::filesystem::path& path,
        std::span<const std::string> lines,
        std::string_view line_ending) const;
    [[nodiscard]] bool rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) const;
    [[nodiscard]] bool remove(const std::filesystem::path& path) const;

    [[nodiscard]] std::filesystem::path next_available_path(
        const std::filesystem::path& directory,
        std::string_view extension = ".txt") const;
};

} // namespace Zenvra::UI::Editor
