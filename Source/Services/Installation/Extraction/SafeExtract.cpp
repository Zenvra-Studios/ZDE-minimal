#include "SafeExtract.h"
#include <algorithm>

namespace Zenvra::Services::Installation::Extraction
{

bool SafeExtract::is_safe_target_path(
    const std::filesystem::path& destination_dir,
    const std::filesystem::path& relative_entry_path)
{
    std::error_code ec;
    std::filesystem::path dest = std::filesystem::weakly_canonical(destination_dir, ec);
    if (ec)
        dest = std::filesystem::absolute(destination_dir);

    std::filesystem::path target = std::filesystem::weakly_canonical(dest / relative_entry_path, ec);
    if (ec)
        target = std::filesystem::absolute(dest / relative_entry_path);

    std::string dest_str = dest.lexically_normal().string();
    std::string target_str = target.lexically_normal().string();

    // Prevent Zip Slip / path traversal outside destination
    if (target_str.rfind(dest_str, 0) != 0)
        return false;

    // Check for double dot segments
    for (const auto& part : relative_entry_path)
    {
        if (part == ".." || part == "." || part.string().find(':') != std::string::npos)
            return false;
    }

    return true;
}

std::filesystem::path SafeExtract::resolve_safe_path(
    const std::filesystem::path& destination_dir,
    const std::filesystem::path& relative_entry_path)
{
    if (!is_safe_target_path(destination_dir, relative_entry_path))
        return {};

    return (destination_dir / relative_entry_path).lexically_normal();
}

bool SafeExtract::ensure_directory_exists(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (std::filesystem::exists(dir, ec))
        return std::filesystem::is_directory(dir, ec);

    return std::filesystem::create_directories(dir, ec);
}

} // namespace Zenvra::Services::Installation::Extraction

