#include "FFmpegPlayer.hpp"
#include "Media/MediaFactory.hpp"
#include <algorithm>

namespace Zenvra::Drivers::Media {

struct FFmpegPlayer::Impl {
    FFmpegDecoder decoder;
    Zenvra::Media::PlaybackState state = Zenvra::Media::PlaybackState::Stopped;
    double current_position = 0.0;
    float volume = 1.0f;
    bool is_looping = false;
    float playback_rate = 1.0f;
};

FFmpegPlayer::FFmpegPlayer() : m_impl(std::make_unique<Impl>()) {}
FFmpegPlayer::~FFmpegPlayer() = default;

bool FFmpegPlayer::open(const Zenvra::Media::MediaSource& source) {
    close();
    bool ok = m_impl->decoder.open(source);
    if (ok) {
        m_impl->state = Zenvra::Media::PlaybackState::Stopped;
        m_impl->current_position = 0.0;
        notify_state_changed(m_impl->state);
    } else {
        m_impl->state = Zenvra::Media::PlaybackState::Error;
        std::string err_msg = "Failed to open media source with FFmpeg driver.";
        if (!m_impl->decoder.last_error().empty()) {
            err_msg += " (" + m_impl->decoder.last_error() + ")";
        }
        notify_error(err_msg);
        notify_state_changed(m_impl->state);
    }
    return ok;
}

void FFmpegPlayer::play() {
    if (!is_open()) return;
    m_impl->state = Zenvra::Media::PlaybackState::Playing;
    notify_state_changed(m_impl->state);
}

void FFmpegPlayer::pause() {
    if (!is_open()) return;
    m_impl->state = Zenvra::Media::PlaybackState::Paused;
    notify_state_changed(m_impl->state);
}

void FFmpegPlayer::stop() {
    if (!is_open()) return;
    m_impl->state = Zenvra::Media::PlaybackState::Stopped;
    seek(0.0, Zenvra::Media::SeekMode::FastKeyframe);
    notify_state_changed(m_impl->state);
}

bool FFmpegPlayer::seek(double timestamp_seconds, Zenvra::Media::SeekMode mode) {
    if (!is_open()) return false;
    bool ok = m_impl->decoder.seek(timestamp_seconds, mode);
    if (ok) {
        m_impl->current_position = timestamp_seconds;
    }
    return ok;
}

void FFmpegPlayer::close() {
    if (m_impl) {
        m_impl->decoder.close();
        m_impl->state = Zenvra::Media::PlaybackState::Stopped;
        m_impl->current_position = 0.0;
    }
}

bool FFmpegPlayer::is_open() const noexcept {
    return m_impl && m_impl->decoder.is_open();
}

Zenvra::Media::PlaybackState FFmpegPlayer::state() const noexcept {
    return m_impl ? m_impl->state : Zenvra::Media::PlaybackState::Stopped;
}

const Zenvra::Media::MediaMetadata& FFmpegPlayer::metadata() const noexcept {
    static const Zenvra::Media::MediaMetadata empty{};
    return m_impl ? m_impl->decoder.metadata() : empty;
}

double FFmpegPlayer::duration() const noexcept {
    return m_impl ? m_impl->decoder.metadata().duration_seconds : 0.0;
}

double FFmpegPlayer::position() const noexcept {
    return m_impl ? m_impl->current_position : 0.0;
}

Zenvra::Media::MediaBackendType FFmpegPlayer::backend_type() const noexcept {
    return Zenvra::Media::MediaBackendType::FFmpeg;
}

std::string_view FFmpegPlayer::backend_name() const noexcept {
    return "FFmpeg";
}

bool FFmpegPlayer::select_video_track(int track_index) {
    if (!is_open()) return false;
    bool ok = m_impl->decoder.select_video_track(track_index);
    if (ok) {
        notify_track_changed(m_impl->decoder.metadata().active_video_track,
                             m_impl->decoder.metadata().active_audio_track);
    }
    return ok;
}

bool FFmpegPlayer::select_audio_track(int track_index) {
    if (!is_open()) return false;
    bool ok = m_impl->decoder.select_audio_track(track_index);
    if (ok) {
        notify_track_changed(m_impl->decoder.metadata().active_video_track,
                             m_impl->decoder.metadata().active_audio_track);
    }
    return ok;
}

bool FFmpegPlayer::select_subtitle_track(int track_index) {
    (void)track_index;
    return false; // Reserved for subtitle renderer
}

void FFmpegPlayer::set_target_video_format(Zenvra::Media::VideoPixelFormat format) {
    if (m_impl) {
        m_impl->decoder.set_target_video_format(format);
    }
}

Zenvra::Media::VideoPixelFormat FFmpegPlayer::target_video_format() const noexcept {
    return m_impl ? m_impl->decoder.target_video_format() : Zenvra::Media::VideoPixelFormat::RGBA32;
}

std::optional<Zenvra::Media::VideoFrame> FFmpegPlayer::get_next_video_frame() {
    if (!is_open()) {
        return std::nullopt;
    }

    auto frame = m_impl->decoder.decode_next_video_frame();
    if (frame) {
        m_impl->current_position = frame->timestamp_seconds;
    } else {
        if (m_impl->is_looping && m_impl->state == Zenvra::Media::PlaybackState::Playing) {
            seek(0.0, Zenvra::Media::SeekMode::FastKeyframe);
            return m_impl->decoder.decode_next_video_frame();
        } else if (m_impl->state == Zenvra::Media::PlaybackState::Playing) {
            m_impl->state = Zenvra::Media::PlaybackState::Stopped;
            notify_end_of_stream();
            notify_state_changed(m_impl->state);
        }
    }
    return frame;
}

std::optional<Zenvra::Media::AudioBuffer> FFmpegPlayer::get_audio_samples(int max_samples) {
    if (!is_open() || m_impl->state != Zenvra::Media::PlaybackState::Playing) {
        return std::nullopt;
    }

    auto audio = m_impl->decoder.decode_next_audio_samples(max_samples);
    if (audio) {
        m_impl->current_position = audio->timestamp_seconds;
        if (m_impl->volume != 1.0f) {
            for (float& sample : audio->samples) {
                sample *= m_impl->volume;
            }
        }
    }
    return audio;
}

void FFmpegPlayer::set_volume(float volume) {
    if (m_impl) {
        m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
    }
}

void FFmpegPlayer::set_looping(bool loop) {
    if (m_impl) {
        m_impl->is_looping = loop;
    }
}

void FFmpegPlayer::set_playback_rate(float rate) {
    if (m_impl) {
        m_impl->playback_rate = std::max(0.1f, rate);
    }
}

void FFmpegPlayer::register_backend() {
    Zenvra::Media::MediaFactory::register_backend(
        Zenvra::Media::MediaBackendType::FFmpeg,
        []() -> Zenvra::Media::MediaPlayerPtr {
            return std::make_unique<FFmpegPlayer>();
        }
    );
}

} // namespace Zenvra::Drivers::Media
