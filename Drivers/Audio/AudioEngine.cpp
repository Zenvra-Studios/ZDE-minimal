#include "AudioEngine.h"

namespace Zenvra::Drivers::Audio {

AudioEngine& AudioEngine::instance() {
    static AudioEngine s_instance;
    return s_instance;
}

bool AudioEngine::init(const AudioSpec& spec) {
    if (m_initialized) return true;
    m_initialized = m_device.open(spec);
    return m_initialized;
}

void AudioEngine::shutdown() {
    if (m_initialized) {
        m_device.close();
        m_initialized = false;
    }
}

bool AudioEngine::is_initialized() const noexcept {
    return m_initialized;
}

AudioDevice& AudioEngine::device() {
    return m_device;
}

VoiceId AudioEngine::play_sound(const std::filesystem::path& path, float volume, float pan, bool loop) {
    if (!m_initialized) {
        if (!init()) return 0;
    }

    auto clip = AudioClip::load_from_file(path, m_device.spec().sample_rate, m_device.spec().channels);
    if (!clip) {
        return 0;
    }

    return m_device.play_clip(clip, volume, pan, loop);
}

VoiceId AudioEngine::play_clip(AudioClipPtr clip, float volume, float pan, bool loop) {
    if (!m_initialized) {
        if (!init()) return 0;
    }
    return m_device.play_clip(clip, volume, pan, loop);
}

void AudioEngine::stop_sound(VoiceId id) {
    m_device.stop_voice(id);
}

void AudioEngine::stop_all() {
    m_device.stop_all();
}

void AudioEngine::set_master_volume(float volume) {
    m_device.set_master_volume(volume);
}

float AudioEngine::master_volume() const noexcept {
    return m_device.master_volume();
}

} // namespace Zenvra::Drivers::Audio
