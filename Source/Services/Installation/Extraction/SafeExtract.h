#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Services::Installation::Extraction
{

class SafeExtract
{
public:
    [[nodiscard]] static bool is_safe_target_path(
        const std::filesystem::path& destination_dir,
        const std::filesystem::path& relative_entry_path);

    [[nodiscard]] static std::filesystem::path resolve_safe_path(
        const std::filesystem::path& destination_dir,
        const std::filesystem::path& relative_entry_path);

    [[nodiscard]] static bool ensure_directory_exists(const std::filesystem::path& dir);
};

} // namespace Zenvra::Services::Installation::Extraction

