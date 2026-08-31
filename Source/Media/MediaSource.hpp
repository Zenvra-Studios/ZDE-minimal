#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Zenvra::Media {

enum class MediaSourceType {
    FilePath,
    Memory,
    StreamUrl
};

class MediaSource {
public:
    MediaSource() = default;

    static MediaSource from_file(const std::filesystem::path& path) {
        MediaSource src;
        src.m_type = MediaSourceType::FilePath;
        src.m_path = path;
        src.m_uri = path.string();
        return src;
    }

    static MediaSource from_url(std::string_view url) {
        MediaSource src;
        src.m_type = MediaSourceType::StreamUrl;
        src.m_uri = std::string(url);
        return src;
    }

    static MediaSource from_memory(std::span<const uint8_t> buffer) {
        MediaSource src;
        src.m_type = MediaSourceType::Memory;
        src.m_memory_buffer.assign(buffer.begin(), buffer.end());
        return src;
    }

    [[nodiscard]] MediaSourceType type() const noexcept { return m_type; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
    [[nodiscard]] const std::string& uri() const noexcept { return m_uri; }
    [[nodiscard]] const std::vector<uint8_t>& memory_buffer() const noexcept { return m_memory_buffer; }

    [[nodiscard]] bool is_valid() const noexcept {
        switch (m_type) {
            case MediaSourceType::FilePath:  return !m_path.empty();
            case MediaSourceType::StreamUrl: return !m_uri.empty();
            case MediaSourceType::Memory:    return !m_memory_buffer.empty();
        }
        return false;
    }

private:
    MediaSourceType m_type = MediaSourceType::FilePath;
    std::filesystem::path m_path;
    std::string m_uri;
    std::vector<uint8_t> m_memory_buffer;
};

} // namespace Zenvra::Media
