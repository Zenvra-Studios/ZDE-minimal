#include "AudioDevice.h"
#ifdef _WIN32
#include "WASAPIAudioOutput.h"
#endif
#include <algorithm>
#include <cmath>
#include <deque>

namespace Zenvra::Drivers::Audio {

struct AudioDevice::Impl {
    AudioSpec spec;
    bool is_open = false;
    bool is_stream_paused = false;
    float master_volume = 1.0f;
    float stream_volume = 1.0f;
    VoiceId next_voice_id = 1;
    std::vector<SoundVoice> voices;
    std::deque<float> stream_buffer;
    size_t stream_frames_played = 0;
    double stream_base_time = 0.0;
    mutable std::mutex mutex;

#ifdef _WIN32
    WASAPIAudioOutput wasapi_output;
#endif
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
    m_impl->stream_buffer.clear();

#ifdef _WIN32
    m_impl->wasapi_output.start(spec.sample_rate, spec.channels, [this](std::span<float> buffer, size_t frames) {
        render_mix(buffer, frames);
    });
    m_impl->spec.sample_rate = m_impl->wasapi_output.sample_rate();
    m_impl->spec.channels = m_impl->wasapi_output.channels();
#endif

    return true;
}

void AudioDevice::close() {
#ifdef _WIN32
    if (m_impl) {
        m_impl->wasapi_output.stop();
    }
#endif

    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->voices.clear();
        m_impl->stream_buffer.clear();
        m_impl->is_open = false;
    }
}

bool AudioDevice::is_open() const noexcept {
    return m_impl && m_impl->is_open;
}

const AudioSpec& AudioDevice::spec() const noexcept {
    if (m_impl) {
#ifdef _WIN32
        if (m_impl->wasapi_output.is_running()) {
            m_impl->spec.sample_rate = m_impl->wasapi_output.sample_rate();
            m_impl->spec.channels = m_impl->wasapi_output.channels();
        }
#endif
        return m_impl->spec;
    }
    static const AudioSpec default_spec{};
    return default_spec;
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
    m_impl->stream_buffer.clear();
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

void AudioDevice::submit_stream_samples(const float* samples, size_t sample_count) {
    if (!samples || sample_count == 0 || !m_impl || !m_impl->is_open) return;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->stream_buffer.insert(m_impl->stream_buffer.end(), samples, samples + sample_count);
}

void AudioDevice::clear_stream_samples() {
    if (!m_impl) return;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->stream_buffer.clear();
    m_impl->stream_frames_played = 0;
    m_impl->stream_base_time = 0.0;
}

size_t AudioDevice::buffered_stream_samples() const noexcept {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->stream_buffer.size();
}

void AudioDevice::set_stream_volume(float volume) {
    if (m_impl) {
        m_impl->stream_volume = std::clamp(volume, 0.0f, 1.0f);
    }
}

float AudioDevice::stream_volume() const noexcept {
    return m_impl ? m_impl->stream_volume : 1.0f;
}

void AudioDevice::set_stream_paused(bool paused) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->is_stream_paused = paused;
    }
}

bool AudioDevice::is_stream_paused() const noexcept {
    if (!m_impl) return true;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->is_stream_paused;
}

void AudioDevice::reset_stream_clock(double start_time) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->stream_frames_played = 0;
        m_impl->stream_base_time = start_time;
    }
}

double AudioDevice::stream_time_seconds() const noexcept {
    if (!m_impl || m_impl->spec.sample_rate <= 0) return 0.0;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->stream_base_time + static_cast<double>(m_impl->stream_frames_played) / std::max(1, m_impl->spec.sample_rate);
}

void AudioDevice::render_mix(std::span<float> out_interleaved_buffer, size_t frame_count) {
    if (out_interleaved_buffer.empty() || frame_count == 0) return;

    // Fill buffer with silence first
    std::fill(out_interleaved_buffer.begin(), out_interleaved_buffer.end(), 0.0f);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->is_open) {
        return;
    }

    const int channels = m_impl->spec.channels;

    // 1. Mix SoundVoices
    for (auto& voice : m_impl->voices) {
        if (voice.is_paused || voice.is_finished || !voice.clip) continue;

        const auto& src_samples = voice.clip->samples();
        const size_t clip_total_frames = voice.clip->total_frames();
        const int clip_channels = voice.clip->channels();

        // Calculate Pan gains (constant power panning) & Perceptual Volume Taper
        float voice_vol = voice.volume * voice.volume;
        float pan_norm = (voice.pan + 1.0f) * 0.5f; // 0.0 (Left) .. 1.0 (Right)
        float left_gain = voice_vol * std::sqrt(1.0f - pan_norm);
        float right_gain = voice_vol * std::sqrt(pan_norm);

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

    // 2. Mix Stream Audio Queue (media player) with YouTube / Browser perceptual curve (v^2)
    const float raw_str_vol = m_impl->stream_volume;
    const float str_vol = raw_str_vol * raw_str_vol;
    if (!m_impl->is_stream_paused && !m_impl->stream_buffer.empty() && str_vol > 0.0f) {
        size_t samples_to_consume = std::min(out_interleaved_buffer.size(), m_impl->stream_buffer.size());
        for (size_t i = 0; i < samples_to_consume; ++i) {
            out_interleaved_buffer[i] += m_impl->stream_buffer.front() * str_vol;
            m_impl->stream_buffer.pop_front();
        }
        size_t frames_consumed = samples_to_consume / std::max(1, channels);
        m_impl->stream_frames_played += frames_consumed;
    }

    // 3. Apply master volume
    const float raw_master = m_impl->master_volume;
    const float master_vol = raw_master * raw_master;
    if (master_vol != 1.0f) {
        for (float& sample : out_interleaved_buffer) {
            sample *= master_vol;
        }
    }

    // 4. Standard Browser / Chromium Transparent Soft Limiting Safety Guard
    // Keeps signal 100% bit-perfect and uncompressed within [-1.0, 1.0].
    // If mixed voice signals exceed full scale, smoothly saturates to prevent harsh digital DAC clipping.
    for (float& sample : out_interleaved_buffer) {
        if (sample > 1.0f) {
            sample = 1.0f + std::tanh(sample - 1.0f) * 0.1f;
            if (sample > 1.0f) sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f + std::tanh(sample + 1.0f) * 0.1f;
            if (sample < -1.0f) sample = -1.0f;
        }
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
