#pragma once

#include "Media/MediaSource.hpp"
#include "Media/MediaTypes.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Zenvra::Drivers::Audio {

class AudioClip {
public:
    AudioClip() = default;
    AudioClip(int sample_rate, int channels, std::vector<float> samples, double duration_seconds);

    // Load and decode audio file (MP3, WAV, FLAC, OGG, AAC, Opus, AIFF, M4A, etc.) via FFmpeg
    static std::shared_ptr<AudioClip> load_from_file(const std::filesystem::path& path, int target_sample_rate = 44100, int target_channels = 2);

    // Load and decode audio from memory buffer via FFmpeg
    static std::shared_ptr<AudioClip> load_from_memory(std::span<const uint8_t> buffer, int target_sample_rate = 44100, int target_channels = 2);

    [[nodiscard]] int sample_rate() const noexcept { return m_sample_rate; }
    [[nodiscard]] int channels() const noexcept { return m_channels; }
    [[nodiscard]] double duration_seconds() const noexcept { return m_duration_seconds; }
    [[nodiscard]] const std::vector<float>& samples() const noexcept { return m_samples; }
    [[nodiscard]] size_t total_frames() const noexcept {
        return m_channels > 0 ? (m_samples.size() / m_channels) : 0;
    }
    [[nodiscard]] bool is_valid() const noexcept { return !m_samples.empty(); }

private:
    int m_sample_rate = 44100;
    int m_channels = 2;
    double m_duration_seconds = 0.0;
    std::vector<float> m_samples; // Interleaved Float32 PCM samples [-1.0f, 1.0f]
};

using AudioClipPtr = std::shared_ptr<AudioClip>;

} // namespace Zenvra::Drivers::Audio
