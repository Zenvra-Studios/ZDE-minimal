#include "AudioDevice.h"
#include <algorithm>
#include <cmath>

namespace Zenvra::Drivers::Audio {

struct AudioDevice::Impl {
    AudioSpec spec;
    bool is_open = false;
    float master_volume = 1.0f;
    VoiceId next_voice_id = 1;
    std::vector<SoundVoice> voices;
    mutable std::mutex mutex;
};

AudioDevice::AudioDevice() : m_impl(std::make_unique<Impl>()) {}
AudioDevice::~AudioDevice() {
    close();
}

bool AudioDevice::open(const AudioSpec& spec) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->spec = spec;
    m_impl->is_open = true;
    m_impl->voices.clear();
    return true;
}

void AudioDevice::close() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->voices.clear();
    m_impl->is_open = false;
}

bool AudioDevice::is_open() const noexcept {
    return m_impl && m_impl->is_open;
}

const AudioSpec& AudioDevice::spec() const noexcept {
    static const AudioSpec default_spec{};
    return m_impl ? m_impl->spec : default_spec;
}

VoiceId AudioDevice::play_clip(AudioClipPtr clip, float volume, float pan, bool loop) {
    if (!clip || !clip->is_valid() || !is_open()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    SoundVoice voice;
    voice.id = m_impl->next_voice_id++;
    voice.clip = std::move(clip);
    voice.current_frame = 0;
    voice.volume = std::clamp(volume, 0.0f, 1.0f);
    voice.pan = std::clamp(pan, -1.0f, 1.0f);
    voice.is_looping = loop;
    voice.is_paused = false;
    voice.is_finished = false;

    m_impl->voices.push_back(std::move(voice));
    return m_impl->voices.back().id;
}

bool AudioDevice::stop_voice(VoiceId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& v : m_impl->voices) {
        if (v.id == id) {
            v.is_finished = true;
            return true;
        }
    }
    return false;
}

void AudioDevice::stop_all() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->voices.clear();
}

bool AudioDevice::pause_voice(VoiceId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& v : m_impl->voices) {
        if (v.id == id) {
            v.is_paused = true;
            return true;
        }
    }
    return false;
}

bool AudioDevice::resume_voice(VoiceId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& v : m_impl->voices) {
        if (v.id == id) {
            v.is_paused = false;
            return true;
        }
    }
    return false;
}

bool AudioDevice::set_voice_volume(VoiceId id, float volume) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& v : m_impl->voices) {
        if (v.id == id) {
            v.volume = std::clamp(volume, 0.0f, 1.0f);
            return true;
        }
    }
    return false;
}

bool AudioDevice::set_voice_pan(VoiceId id, float pan) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& v : m_impl->voices) {
        if (v.id == id) {
            v.pan = std::clamp(pan, -1.0f, 1.0f);
            return true;
        }
    }
    return false;
}

void AudioDevice::set_master_volume(float volume) {
    if (m_impl) {
        m_impl->master_volume = std::clamp(volume, 0.0f, 1.0f);
    }
}

float AudioDevice::master_volume() const noexcept {
    return m_impl ? m_impl->master_volume : 1.0f;
}

size_t AudioDevice::active_voice_count() const noexcept {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->voices.size();
}

void AudioDevice::render_mix(std::span<float> out_interleaved_buffer, size_t frame_count) {
    if (out_interleaved_buffer.empty() || frame_count == 0) return;

    // Fill buffer with silence first
    std::fill(out_interleaved_buffer.begin(), out_interleaved_buffer.end(), 0.0f);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->voices.empty() || !m_impl->is_open) {
        return;
    }

    const int channels = m_impl->spec.channels;

    for (auto& voice : m_impl->voices) {
        if (voice.is_paused || voice.is_finished || !voice.clip) continue;

        const auto& src_samples = voice.clip->samples();
        const size_t clip_total_frames = voice.clip->total_frames();
        const int clip_channels = voice.clip->channels();

        // Calculate Pan gains (constant power panning)
        float pan_norm = (voice.pan + 1.0f) * 0.5f; // 0.0 (Left) .. 1.0 (Right)
        float left_gain = voice.volume * std::sqrt(1.0f - pan_norm);
        float right_gain = voice.volume * std::sqrt(pan_norm);

        for (size_t f = 0; f < frame_count; ++f) {
            if (voice.current_frame >= clip_total_frames) {
                if (voice.is_looping) {
                    voice.current_frame = 0;
                } else {
                    voice.is_finished = true;
                    break;
                }
            }

            size_t src_idx = voice.current_frame * clip_channels;
            float left_sample = 0.0f;
            float right_sample = 0.0f;

            if (clip_channels == 1) {
                float mono = src_samples[src_idx];
                left_sample = mono * left_gain;
                right_sample = mono * right_gain;
            } else {
                left_sample = src_samples[src_idx] * left_gain;
                right_sample = src_samples[src_idx + 1] * right_gain;
            }

            size_t out_idx = f * channels;
            if (channels >= 2) {
                out_interleaved_buffer[out_idx] += left_sample;
                out_interleaved_buffer[out_idx + 1] += right_sample;
            } else {
                out_interleaved_buffer[out_idx] += (left_sample + right_sample) * 0.5f;
            }

            voice.current_frame++;
        }
    }

    // Apply master volume and soft limiting / saturation to avoid digital clipping
    const float master_vol = m_impl->master_volume;
    for (float& sample : out_interleaved_buffer) {
        sample *= master_vol;
        // Soft clipping with tanh
        sample = std::clamp(sample, -1.0f, 1.0f);
    }

    // Clean up finished voices
    m_impl->voices.erase(
        std::remove_if(m_impl->voices.begin(), m_impl->voices.end(), [](const SoundVoice& v) {
            return v.is_finished;
        }),
        m_impl->voices.end()
    );
}

} // namespace Zenvra::Drivers::Audio
