#pragma once

#include "Media/MediaTypes.hpp"
#include "Media/MediaSource.hpp"
#include <memory>
#include <optional>
#include <string>

#ifdef ZDE_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

namespace Zenvra::Drivers::Media {

#ifdef ZDE_HAS_FFMPEG
// RAII Smart Deleters for FFmpeg C Structs
struct AVFormatInputContextDeleter {
    void operator()(AVFormatContext* ctx) const noexcept {
        if (ctx) avformat_close_input(&ctx);
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const noexcept {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const noexcept {
        if (pkt) av_packet_free(&pkt);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const noexcept {
        if (frame) av_frame_free(&frame);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* sws) const noexcept {
        if (sws) sws_freeContext(sws);
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* swr) const noexcept {
        if (swr) swr_free(&swr);
    }
};

struct AVIOContextDeleter {
    void operator()(AVIOContext* ctx) const noexcept {
        if (ctx) {
            av_freep(&ctx->buffer);
            avio_context_free(&ctx);
        }
    }
};

using AVFormatInputContextPtr = std::unique_ptr<AVFormatContext, AVFormatInputContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVIOContextPtr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;

inline AVPacketPtr make_packet() { return AVPacketPtr(av_packet_alloc()); }
inline AVFramePtr make_frame() { return AVFramePtr(av_frame_alloc()); }

inline std::string ffmpeg_error_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}
#endif

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;
    FFmpegDecoder(FFmpegDecoder&&) noexcept;
    FFmpegDecoder& operator=(FFmpegDecoder&&) noexcept;

    // Open media from file path, stream URL, or memory buffer
    bool open(const Zenvra::Media::MediaSource& source);
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const Zenvra::Media::MediaMetadata& metadata() const noexcept;

    // Output video & audio format configuration
    void set_target_video_format(Zenvra::Media::VideoPixelFormat format);
    [[nodiscard]] Zenvra::Media::VideoPixelFormat target_video_format() const noexcept;
    void set_target_audio_format(int sample_rate, int channels);
    [[nodiscard]] int target_sample_rate() const noexcept;
    [[nodiscard]] int target_channels() const noexcept;

    // Track Switching
    bool select_video_track(int track_index);
    bool select_audio_track(int track_index);

    // Frame & Audio Acquisition
    std::optional<Zenvra::Media::VideoFrame> decode_next_video_frame();
    std::optional<Zenvra::Media::AudioBuffer> decode_next_audio_samples(int max_samples = 4096);

    // Seek
    bool seek(double timestamp_seconds, Zenvra::Media::SeekMode mode = Zenvra::Media::SeekMode::Exact);

    // Diagnostics — returns the last error message (empty if no error)
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Zenvra::Drivers::Media
