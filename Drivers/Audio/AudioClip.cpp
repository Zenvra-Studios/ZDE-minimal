#include "AudioClip.h"
#include "Drivers/Media/FFmpegDecoder.hpp"

#if defined(_WIN32)
#include "Platform/Win32/Utility/Win32Compat.h"
#endif

namespace Zenvra::Drivers::Audio {

AudioClip::AudioClip(int sample_rate, int channels, std::vector<float> samples, double duration_seconds)
    : m_sample_rate(sample_rate), m_channels(channels), m_duration_seconds(duration_seconds), m_samples(std::move(samples)) {}

std::shared_ptr<AudioClip> AudioClip::load_from_file(const std::filesystem::path& path, int target_sample_rate, int target_channels) {
#if defined(_WIN32)
    if (!Zenvra::Platform::Win32::Compat::is_windows_8_or_greater()) {
        return nullptr;
    }
#endif
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

    Zenvra::Drivers::Media::FFmpegDecoder decoder;
    auto source = Zenvra::Media::MediaSource::from_file(path);
    if (!decoder.open(source)) {
        return nullptr;
    }

    std::vector<float> all_samples;
    double duration = decoder.metadata().duration_seconds;

    while (auto audio_buf = decoder.decode_next_audio_samples(8192)) {
        all_samples.insert(all_samples.end(), audio_buf->samples.begin(), audio_buf->samples.end());
    }

    if (all_samples.empty()) {
        return nullptr;
    }

    return std::make_shared<AudioClip>(target_sample_rate, target_channels, std::move(all_samples), duration);
}

std::shared_ptr<AudioClip> AudioClip::load_from_memory(std::span<const uint8_t> buffer, int target_sample_rate, int target_channels) {
#if defined(_WIN32)
    if (!Zenvra::Platform::Win32::Compat::is_windows_8_or_greater()) {
        return nullptr;
    }
#endif
    if (buffer.empty()) return nullptr;

    Zenvra::Drivers::Media::FFmpegDecoder decoder;
    auto source = Zenvra::Media::MediaSource::from_memory(buffer);
    if (!decoder.open(source)) {
        return nullptr;
    }

    std::vector<float> all_samples;
    double duration = decoder.metadata().duration_seconds;

    while (auto audio_buf = decoder.decode_next_audio_samples(8192)) {
        all_samples.insert(all_samples.end(), audio_buf->samples.begin(), audio_buf->samples.end());
    }

    if (all_samples.empty()) {
        return nullptr;
    }

    return std::make_shared<AudioClip>(target_sample_rate, target_channels, std::move(all_samples), duration);
}

} // namespace Zenvra::Drivers::Audio
