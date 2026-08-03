#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::UI::Editor
{

struct TextFileSnapshot
{
    std::filesystem::path absolute_path;
    std::filesystem::path project_root;
    std::vector<std::string> lines;
    std::vector<std::string> breadcrumbs;
    std::string line_ending = "LF";
    bool read_only = false;
    bool binary_preview = false;
    bool truncated = false;
};

class EditorFileSystem
{
public:
    [[nodiscard]] static std::optional<std::filesystem::path> find_project_root(
        const std::filesystem::path& start);
    [[nodiscard]] static std::optional<TextFileSnapshot> read_text_file(
        const std::filesystem::path& requested_path);

private:
    [[nodiscard]] static std::vector<std::string> build_breadcrumbs(
        const std::filesystem::path& absolute_path,
        const std::filesystem::path& project_root);
};

} // namespace Zenvra::UI::Editor
