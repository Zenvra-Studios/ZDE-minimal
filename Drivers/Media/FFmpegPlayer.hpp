#pragma once

#include "Media/MediaPlayer.hpp"
#include "FFmpegDecoder.hpp"
#include <memory>

namespace Zenvra::Drivers::Media {

class FFmpegPlayer : public Zenvra::Media::IMediaPlayer {
public:
    FFmpegPlayer();
    ~FFmpegPlayer() override;

    // IMediaPlayer implementation
    bool open(const Zenvra::Media::MediaSource& source) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(double timestamp_seconds, Zenvra::Media::SeekMode mode = Zenvra::Media::SeekMode::Exact) override;
    void close() override;

    [[nodiscard]] bool is_open() const noexcept override;
    [[nodiscard]] Zenvra::Media::PlaybackState state() const noexcept override;
    [[nodiscard]] const Zenvra::Media::MediaMetadata& metadata() const noexcept override;
    [[nodiscard]] double duration() const noexcept override;
    [[nodiscard]] double position() const noexcept override;
    [[nodiscard]] Zenvra::Media::MediaBackendType backend_type() const noexcept override;
    [[nodiscard]] std::string_view backend_name() const noexcept override;

    // Track Switching
    bool select_video_track(int track_index) override;
    bool select_audio_track(int track_index) override;
    bool select_subtitle_track(int track_index) override;

    // Video Output Configuration
    void set_target_video_format(Zenvra::Media::VideoPixelFormat format) override;
    [[nodiscard]] Zenvra::Media::VideoPixelFormat target_video_format() const noexcept override;

    // Frame & Audio Acquisition
    std::optional<Zenvra::Media::VideoFrame> get_next_video_frame() override;
    std::optional<Zenvra::Media::AudioBuffer> get_audio_samples(int max_samples = 4096) override;

    // Controls
    void set_volume(float volume) override;
    void set_looping(bool loop) override;
    void set_playback_rate(float rate) override;

    // Self-registration helper for MediaFactory
    static void register_backend();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Zenvra::Drivers::Media
