#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Zenvra::Media {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Error
};

enum class MediaBackendType {
    Auto,
    FFmpeg,
    MPV,
    WindowsMedia,
    AVFoundation
};

enum class SeekMode {
    FastKeyframe,
    Exact
};

enum class VideoPixelFormat {
    RGBA32,
    BGRA32,
    RGB24,
    BGR24,
    Gray8,
    RGBA64_LE, // 16-bit per channel for HDR/Deep color
    YUV420P,
    NV12,
    Unknown
};

enum class AudioSampleFormat {
    Float32,
    Int16,
    Int32,
    Unknown
};

enum class ColorSpace {
    Unknown,
    BT601,
    BT709,
    BT2020_HDR
};

enum class ColorRange {
    Unknown,
    Limited_MPEG,
    Full_JPEG
};

struct VideoStreamTrackInfo {
    int index = -1;
    std::string codec_name;
    std::string codec_long_name;
    std::string profile;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int aspect_ratio_num = 0;
    int aspect_ratio_den = 0;
    std::int64_t bit_rate = 0;
    std::string pixel_format_name;
    int rotation_degrees = 0; // 0, 90, 180, 270
    bool is_hdr = false;
    ColorSpace color_space = ColorSpace::Unknown;
    ColorRange color_range = ColorRange::Unknown;
    bool is_interlaced = false;
};

struct AudioStreamTrackInfo {
    int index = -1;
    std::string codec_name;
    std::string codec_long_name;
    int sample_rate = 0;
    int channels = 0;
    std::string channel_layout;
    std::int64_t bit_rate = 0;
    std::string language;
    std::string title;
};

struct SubtitleStreamTrackInfo {
    int index = -1;
    std::string codec_name;
    std::string language;
    std::string title;
    bool is_default = false;
    bool is_forced = false;
};

struct VideoFrame {
    int width = 0;
    int height = 0;
    int linesize = 0; // Bytes per row
    double timestamp_seconds = 0.0;
    double duration_seconds = 0.0;
    int rotation_degrees = 0;
    bool is_keyframe = false;
    VideoPixelFormat format = VideoPixelFormat::RGBA32;
    std::vector<uint8_t> data; // Pixel payload
};

struct AudioBuffer {
    int sample_rate = 44100;
    int channels = 2;
    double timestamp_seconds = 0.0;
    double duration_seconds = 0.0;
    AudioSampleFormat format = AudioSampleFormat::Float32;
    std::vector<float> samples; // Interleaved Float32 PCM samples [-1.0f, 1.0f]
};

struct MediaMetadata {
    std::string container_format;
    std::string container_long_name;
    double duration_seconds = 0.0;
    std::int64_t bit_rate = 0;
    
    // Standard Tags
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string date;
    std::string comment;
    
    // Multiple Stream Tracks
    std::vector<VideoStreamTrackInfo> video_tracks;
    std::vector<AudioStreamTrackInfo> audio_tracks;
    std::vector<SubtitleStreamTrackInfo> subtitle_tracks;

    int active_video_track = -1;
    int active_audio_track = -1;
    int active_subtitle_track = -1;

    // Helper Convenience Properties (Primary Track)
    [[nodiscard]] bool has_video() const noexcept { return !video_tracks.empty(); }
    [[nodiscard]] bool has_audio() const noexcept { return !audio_tracks.empty(); }
    [[nodiscard]] bool has_subtitles() const noexcept { return !subtitle_tracks.empty(); }

    [[nodiscard]] int primary_width() const noexcept {
        return (active_video_track >= 0 && active_video_track < static_cast<int>(video_tracks.size()))
            ? video_tracks[active_video_track].width : (has_video() ? video_tracks[0].width : 0);
    }

    [[nodiscard]] int primary_height() const noexcept {
        return (active_video_track >= 0 && active_video_track < static_cast<int>(video_tracks.size()))
            ? video_tracks[active_video_track].height : (has_video() ? video_tracks[0].height : 0);
    }

    [[nodiscard]] double primary_fps() const noexcept {
        return (active_video_track >= 0 && active_video_track < static_cast<int>(video_tracks.size()))
            ? video_tracks[active_video_track].fps : (has_video() ? video_tracks[0].fps : 0.0);
    }
};

} // namespace Zenvra::Media
