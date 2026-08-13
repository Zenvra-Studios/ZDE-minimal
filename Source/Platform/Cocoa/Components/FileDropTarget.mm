#include "Platform/Cocoa/Components/FileDropTarget.h"

#include <string>

namespace Zenvra::Platform::Cocoa::Components
{

std::vector<std::filesystem::path> FileDropTarget::parse_dropped_urls(
    const char* const* urls, std::size_t count)
{
    std::vector<std::filesystem::path> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (urls[i])
        {
            std::string url_string{urls[i]};
            // Strip file:// prefix if present
            constexpr std::string_view prefix = "file://";
            if (url_string.starts_with(prefix))
            {
                url_string = url_string.substr(prefix.size());
            }
            if (!url_string.empty())
            {
                result.emplace_back(url_string);
            }
        }
    }
    return result;
}

} // namespace Zenvra::Platform::Cocoa::Components
