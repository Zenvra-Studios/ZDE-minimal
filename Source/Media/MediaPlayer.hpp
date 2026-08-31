#pragma once

#include "MediaTypes.hpp"
#include "MediaSource.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace Zenvra::Media {

class IMediaPlayer {
public:
    virtual ~IMediaPlayer() = default;

    // Core Lifecycle
    virtual bool open(const MediaSource& source) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool seek(double timestamp_seconds, SeekMode mode = SeekMode::Exact) = 0;
    virtual void close() = 0;

    // Status and Metadata
    [[nodiscard]] virtual bool is_open() const noexcept = 0;
    [[nodiscard]] virtual PlaybackState state() const noexcept = 0;
    [[nodiscard]] virtual const MediaMetadata& metadata() const noexcept = 0;
    [[nodiscard]] virtual double duration() const noexcept = 0;
    [[nodiscard]] virtual double position() const noexcept = 0;
    [[nodiscard]] virtual MediaBackendType backend_type() const noexcept = 0;
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
    [[nodiscard]] virtual std::string last_error() const { return ""; }

    // Track Selection
    virtual bool select_video_track(int track_index) = 0;
    virtual bool select_audio_track(int track_index) = 0;
    virtual bool select_subtitle_track(int track_index) = 0;

    // Frame & Audio Acquisition (for UI rendering & Audio Engine)
    virtual std::optional<VideoFrame> get_next_video_frame() = 0;
    virtual std::optional<AudioBuffer> get_audio_samples(int max_samples = 4096) = 0;

    // Video Output Configuration
    virtual void set_target_video_format(VideoPixelFormat format) = 0;
    [[nodiscard]] virtual VideoPixelFormat target_video_format() const noexcept = 0;

    // Controls
    virtual void set_volume(float volume) = 0; // 0.0f to 1.0f
    virtual void set_looping(bool loop) = 0;
    virtual void set_playback_rate(float rate) = 0; // 1.0f = normal speed

    // Callbacks
    using StateCallback = std::function<void(PlaybackState)>;
    using EndOfStreamCallback = std::function<void()>;
    using ErrorCallback = std::function<void(std::string_view)>;
    using TrackChangedCallback = std::function<void(int video_track, int audio_track)>;

    virtual void on_state_changed(StateCallback callback) { m_on_state_changed = std::move(callback); }
    virtual void on_end_of_stream(EndOfStreamCallback callback) { m_on_end_of_stream = std::move(callback); }
    virtual void on_error(ErrorCallback callback) { m_on_error = std::move(callback); }
    virtual void on_track_changed(TrackChangedCallback callback) { m_on_track_changed = std::move(callback); }

protected:
    void notify_state_changed(PlaybackState new_state) {
        if (m_on_state_changed) m_on_state_changed(new_state);
    }

    void notify_end_of_stream() {
        if (m_on_end_of_stream) m_on_end_of_stream();
    }

    void notify_error(std::string_view err) {
        if (m_on_error) m_on_error(err);
    }

    void notify_track_changed(int video_track, int audio_track) {
        if (m_on_track_changed) m_on_track_changed(video_track, audio_track);
    }

private:
    StateCallback m_on_state_changed;
    EndOfStreamCallback m_on_end_of_stream;
    ErrorCallback m_on_error;
    TrackChangedCallback m_on_track_changed;
};

using MediaPlayerPtr = std::unique_ptr<IMediaPlayer>;

} // namespace Zenvra::Media
