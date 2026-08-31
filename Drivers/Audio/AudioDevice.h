#pragma once

#include "AudioClip.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace Zenvra::Drivers::Audio {

struct AudioSpec {
    int sample_rate = 44100;
    int channels = 2;
    int buffer_frames = 1024;
};

using VoiceId = std::uint32_t;

struct SoundVoice {
    VoiceId id = 0;
    AudioClipPtr clip;
    size_t current_frame = 0;
    float volume = 1.0f;
    float pan = 0.0f; // -1.0 (Left) .. 0.0 (Center) .. 1.0 (Right)
    float pitch = 1.0f;
    bool is_looping = false;
    bool is_paused = false;
    bool is_finished = false;
};

class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();

    bool open(const AudioSpec& spec = {});
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const AudioSpec& spec() const noexcept;

    // Voice Playback Controls
    VoiceId play_clip(AudioClipPtr clip, float volume = 1.0f, float pan = 0.0f, bool loop = false);
    bool stop_voice(VoiceId id);
    void stop_all();
    bool pause_voice(VoiceId id);
    bool resume_voice(VoiceId id);
    bool set_voice_volume(VoiceId id, float volume);
    bool set_voice_pan(VoiceId id, float pan);

    // Master Controls
    void set_master_volume(float volume);
    [[nodiscard]] float master_volume() const noexcept;
    [[nodiscard]] size_t active_voice_count() const noexcept;

    // Software Audio Mixer render step (for output hardware or test buffers)
    void render_mix(std::span<float> out_interleaved_buffer, size_t frame_count);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Zenvra::Drivers::Audio
