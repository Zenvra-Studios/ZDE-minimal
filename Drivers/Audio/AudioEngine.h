#pragma once

#include "AudioDevice.h"
#include "AudioClip.h"
#include <filesystem>
#include <memory>
#include <string>

namespace Zenvra::Drivers::Audio {

class AudioEngine {
public:
    static AudioEngine& instance();

    bool init(const AudioSpec& spec = {});
    void shutdown();

    [[nodiscard]] bool is_initialized() const noexcept;
    AudioDevice& device();

    // High level: play sound directly from file (MP3, WAV, FLAC, OGG, AAC, etc.)
    VoiceId play_sound(const std::filesystem::path& path, float volume = 1.0f, float pan = 0.0f, bool loop = false);
    
    // Play pre-decoded audio clip
    VoiceId play_clip(AudioClipPtr clip, float volume = 1.0f, float pan = 0.0f, bool loop = false);

    void stop_sound(VoiceId id);
    void stop_all();

    void set_master_volume(float volume);
    [[nodiscard]] float master_volume() const noexcept;

private:
    AudioEngine() = default;
    ~AudioEngine() = default;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    AudioDevice m_device;
    bool m_initialized = false;
};

} // namespace Zenvra::Drivers::Audio
