#pragma once

#include <filesystem>
#include <vector>

namespace Zenvra::Platform::Cocoa::Components
{

/// macOS file drop target.  On macOS the NSDraggingDestination protocol is
/// implemented on the NSView side (ZenvraContentView) and forwarded here
/// for path parsing and workspace integration.
class FileDropTarget
{
public:
    /// Parse a list of file URLs from a pasteboard string representation.
    [[nodiscard]] static std::vector<std::filesystem::path> parse_dropped_urls(
        const char* const* urls, std::size_t count);
};

} // namespace Zenvra::Platform::Cocoa::Components
