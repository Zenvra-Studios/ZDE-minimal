#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Installation::Extraction
{

struct ExtractProgress
{
    std::size_t extracted_files{0};
    std::size_t total_files{0};
    std::string current_entry;
    float percentage{0.0F};
};

using ExtractProgressCallback = std::function<void(const ExtractProgress&)>;

class ArchiveExtractor
{
public:
    ArchiveExtractor() = default;

    [[nodiscard]] static bool is_archive(const std::filesystem::path& file_path);

    [[nodiscard]] static bool extract(
        const std::filesystem::path& archive_path,
        const std::filesystem::path& destination_dir,
        ExtractProgressCallback progress_callback = nullptr);

    [[nodiscard]] static std::vector<std::filesystem::path> list_entries(
        const std::filesystem::path& archive_path);
};

} // namespace Zenvra::Services::Installation::Extraction

