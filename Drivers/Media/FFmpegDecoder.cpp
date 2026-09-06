#include "FFmpegDecoder.hpp"
#include <cmath>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <deque>
#include <mutex>

#ifdef ZDE_HAS_FFMPEG
extern "C" {
#include <libavutil/dict.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libavutil/display.h>
}
#endif

namespace Zenvra::Drivers::Media {

#ifdef ZDE_HAS_FFMPEG

struct MemorySourceReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

static int read_memory_packet(void* opaque, uint8_t* buf, int buf_size) {
    auto* reader = static_cast<MemorySourceReader*>(opaque);
    if (!reader || !reader->data || reader->pos >= reader->size) {
        return AVERROR_EOF;
    }
    size_t available = reader->size - reader->pos;
    size_t to_read = std::min(static_cast<size_t>(buf_size), available);
    std::memcpy(buf, reader->data + reader->pos, to_read);
    reader->pos += to_read;
    return static_cast<int>(to_read);
}

static int64_t seek_memory_packet(void* opaque, int64_t offset, int whence) {
    auto* reader = static_cast<MemorySourceReader*>(opaque);
    if (!reader) return -1;

    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(reader->size);
    }

    int64_t new_pos = -1;
    switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = static_cast<int64_t>(reader->pos) + offset;
            break;
        case SEEK_END:
            new_pos = static_cast<int64_t>(reader->size) + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0 || new_pos > static_cast<int64_t>(reader->size)) {
        return -1;
    }
    reader->pos = static_cast<size_t>(new_pos);
    return new_pos;
}

static AVPixelFormat to_ffmpeg_pixel_format(Zenvra::Media::VideoPixelFormat fmt) {
    switch (fmt) {
        case Zenvra::Media::VideoPixelFormat::RGBA32: return AV_PIX_FMT_RGBA;
        case Zenvra::Media::VideoPixelFormat::BGRA32: return AV_PIX_FMT_BGRA;
        case Zenvra::Media::VideoPixelFormat::RGB24:  return AV_PIX_FMT_RGB24;
        case Zenvra::Media::VideoPixelFormat::BGR24:  return AV_PIX_FMT_BGR24;
        case Zenvra::Media::VideoPixelFormat::Gray8:  return AV_PIX_FMT_GRAY8;
        case Zenvra::Media::VideoPixelFormat::RGBA64_LE: return AV_PIX_FMT_RGBA64LE;
        default: return AV_PIX_FMT_RGBA;
    }
}

static int get_bytes_per_pixel(Zenvra::Media::VideoPixelFormat fmt) {
    switch (fmt) {
        case Zenvra::Media::VideoPixelFormat::RGBA32: return 4;
        case Zenvra::Media::VideoPixelFormat::BGRA32: return 4;
        case Zenvra::Media::VideoPixelFormat::RGB24:  return 3;
        case Zenvra::Media::VideoPixelFormat::BGR24:  return 3;
        case Zenvra::Media::VideoPixelFormat::Gray8:  return 1;
        case Zenvra::Media::VideoPixelFormat::RGBA64_LE: return 8;
        default: return 4;
    }
}

static Zenvra::Media::ColorSpace to_zde_color_space(AVColorSpace spc, AVColorPrimaries pri) {
    if (spc == AVCOL_SPC_BT2020_CL || spc == AVCOL_SPC_BT2020_NCL || pri == AVCOL_PRI_BT2020) {
        return Zenvra::Media::ColorSpace::BT2020_HDR;
    }
    if (spc == AVCOL_SPC_BT709 || pri == AVCOL_PRI_BT709) {
        return Zenvra::Media::ColorSpace::BT709;
    }
    if (spc == AVCOL_SPC_SMPTE170M || spc == AVCOL_SPC_BT470BG) {
        return Zenvra::Media::ColorSpace::BT601;
    }
    return Zenvra::Media::ColorSpace::Unknown;
}

static int get_stream_rotation(AVStream* stream) {
    if (!stream) return 0;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(61, 0, 100)
    if (stream->codecpar) {
        const AVPacketSideData* sd = av_packet_side_data_get(
            stream->codecpar->coded_side_data,
            stream->codecpar->nb_coded_side_data,
            AV_PKT_DATA_DISPLAYMATRIX);
        if (sd && sd->data) {
            double rot = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
            if (std::isnan(rot)) return 0;
            int int_rot = static_cast<int>(std::round(rot)) % 360;
            if (int_rot < 0) int_rot += 360;
            return int_rot;
        }
    }
#elif LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 0, 100)
    const uint8_t* display_matrix = av_stream_get_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
    if (display_matrix) {
        double rot = -av_display_rotation_get(reinterpret_cast<const int32_t*>(display_matrix));
        if (std::isnan(rot)) return 0;
        int int_rot = static_cast<int>(std::round(rot)) % 360;
        if (int_rot < 0) int_rot += 360;
        return int_rot;
    }
#endif
    if (stream->metadata) {
        AVDictionaryEntry* tag = av_dict_get(stream->metadata, "rotate", nullptr, 0);
        if (tag && tag->value) {
            int rot = std::atoi(tag->value) % 360;
            if (rot < 0) rot += 360;
            return rot;
        }
    }
    return 0;
}

#endif // ZDE_HAS_FFMPEG

struct FFmpegDecoder::Impl {
    Zenvra::Media::MediaMetadata metadata;
#ifdef _WIN32
    Zenvra::Media::VideoPixelFormat target_video_format = Zenvra::Media::VideoPixelFormat::BGRA32;
#else
    Zenvra::Media::VideoPixelFormat target_video_format = Zenvra::Media::VideoPixelFormat::RGBA32;
#endif
    bool is_open = false;
    std::string last_error;
    mutable std::mutex decoder_mutex;

#ifdef ZDE_HAS_FFMPEG
    AVFormatInputContextPtr format_ctx;
    AVIOContextPtr avio_ctx;
    std::vector<uint8_t> memory_source_buffer;
    MemorySourceReader memory_reader;
    
    // Active Video State
    AVCodecContextPtr video_codec_ctx;
    SwsContext* sws_ctx = nullptr;
    int current_sws_src_w = 0;
    int current_sws_src_h = 0;
    int target_video_w = 0;
    int target_video_h = 0;
    int current_sws_dst_w = 0;
    int current_sws_dst_h = 0;
    AVPixelFormat current_sws_src_fmt = AV_PIX_FMT_NONE;
    AVPixelFormat current_sws_dst_fmt = AV_PIX_FMT_NONE;
    AVColorSpace current_sws_colorspace = AVCOL_SPC_UNSPECIFIED;
    AVColorRange current_sws_color_range = AVCOL_RANGE_UNSPECIFIED;
    bool deband_enabled = false;
    bool edge_aa_enabled = false;
    int active_video_stream_idx = -1;
    AVRational video_time_base = {1, 1000};
    int video_rotation = 0;
    
    // Active Audio State
    AVCodecContextPtr audio_codec_ctx;
    SwrContext* swr_ctx = nullptr;
    int active_audio_stream_idx = -1;
    AVRational audio_time_base = {1, 1000};
    int target_sample_rate = 44100;
    int target_channels = 2;

    // Interleaved Packet Queues
    std::deque<AVPacketPtr> video_packet_queue;
    std::deque<AVPacketPtr> audio_packet_queue;
    bool demux_eof = false;

    ~Impl() {
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        if (swr_ctx) {
            swr_free(&swr_ctx);
            swr_ctx = nullptr;
        }
        video_packet_queue.clear();
        audio_packet_queue.clear();
        avio_ctx.reset();
        memory_source_buffer.clear();
        memory_reader = {};
    }

    void recreate_sws_if_needed(int src_w, int src_h, AVPixelFormat src_fmt, AVPixelFormat dst_fmt,
                                AVColorSpace color_spc = AVCOL_SPC_UNSPECIFIED,
                                AVColorRange color_rng = AVCOL_RANGE_UNSPECIFIED) {
        int out_w = (target_video_w > 0) ? target_video_w : src_w;
        int out_h = (target_video_h > 0) ? target_video_h : src_h;
        out_w = (out_w + 1) & ~1;
        out_h = (out_h + 1) & ~1;

        if (sws_ctx && current_sws_src_w == src_w && current_sws_src_h == src_h &&
            current_sws_dst_w == out_w && current_sws_dst_h == out_h &&
            current_sws_src_fmt == src_fmt && current_sws_dst_fmt == dst_fmt &&
            current_sws_colorspace == color_spc && current_sws_color_range == color_rng) {
            return;
        }
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }

        // Ultra-fast hardware-accelerated scaling flags: Bicubic SIMD (AVX2/SSE4), accurate rounding, full chroma interpolation
        constexpr int flags = SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_BITEXACT;
        sws_ctx = sws_getContext(
            src_w, src_h, src_fmt,
            out_w, out_h, dst_fmt,
            flags,
            nullptr, nullptr, nullptr
        );

        if (sws_ctx) {
            // Determine input color space: BT.2020 (4K/HDR), BT.709 (HD 1080p/720p), or BT.601 (SD)
            int sws_cs = SWS_CS_DEFAULT;
            if (color_spc == AVCOL_SPC_BT2020_CL || color_spc == AVCOL_SPC_BT2020_NCL) {
                sws_cs = SWS_CS_BT2020;
            } else if (color_spc == AVCOL_SPC_BT709 || (color_spc == AVCOL_SPC_UNSPECIFIED && src_w >= 1280)) {
                sws_cs = SWS_CS_ITU709;
            } else if (color_spc == AVCOL_SPC_SMPTE170M || color_spc == AVCOL_SPC_BT470BG) {
                sws_cs = SWS_CS_ITU601;
            }

            int *inv_table = nullptr, *table = nullptr;
            int src_range = 0, dst_range = 1, brightness = 0, contrast = 1 << 16, saturation = 1 << 16;
            if (sws_getColorspaceDetails(sws_ctx, &inv_table, &src_range, &table, &dst_range, &brightness, &contrast, &saturation) >= 0) {
                const int* coeff = sws_getCoefficients(sws_cs);
                int is_full_range = (color_rng == AVCOL_RANGE_JPEG) ? 1 : 0;
                sws_setColorspaceDetails(sws_ctx, coeff, is_full_range, coeff, 1 /* RGB is always full range 0-255 */, brightness, contrast, saturation);
            }
        }

        current_sws_src_w = src_w;
        current_sws_src_h = src_h;
        current_sws_dst_w = out_w;
        current_sws_dst_h = out_h;
        current_sws_src_fmt = src_fmt;
        current_sws_dst_fmt = dst_fmt;
        current_sws_colorspace = color_spc;
        current_sws_color_range = color_rng;
    }

    bool read_next_demux_packet() {
        if (demux_eof || !format_ctx) return false;

        AVPacketPtr pkt = make_packet();
        if (!pkt) return false;

        int ret = av_read_frame(format_ctx.get(), pkt.get());
        if (ret < 0) {
            demux_eof = true;
            return false;
        }

        if (pkt->stream_index == active_video_stream_idx) {
            video_packet_queue.push_back(std::move(pkt));
            return true;
        } else if (pkt->stream_index == active_audio_stream_idx) {
            audio_packet_queue.push_back(std::move(pkt));
            return true;
        }
        return true;
    }
#endif
};

FFmpegDecoder::FFmpegDecoder() : m_impl(std::make_unique<Impl>()) {}
FFmpegDecoder::~FFmpegDecoder() = default;
FFmpegDecoder::FFmpegDecoder(FFmpegDecoder&&) noexcept = default;
FFmpegDecoder& FFmpegDecoder::operator=(FFmpegDecoder&&) noexcept = default;

void FFmpegDecoder::set_target_video_format(Zenvra::Media::VideoPixelFormat format) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        m_impl->target_video_format = format;
#ifdef ZDE_HAS_FFMPEG
        m_impl->current_sws_dst_fmt = AV_PIX_FMT_NONE;
#endif
    }
}

Zenvra::Media::VideoPixelFormat FFmpegDecoder::target_video_format() const noexcept {
    if (!m_impl) return Zenvra::Media::VideoPixelFormat::RGBA32;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->target_video_format;
}

void FFmpegDecoder::set_target_video_size(int width, int height) {
    if (m_impl && width > 0 && height > 0) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        int aligned_w = (width + 1) & ~1;
        int aligned_h = (height + 1) & ~1;
        if (std::abs(m_impl->target_video_w - aligned_w) > 2 ||
            std::abs(m_impl->target_video_h - aligned_h) > 2) {
            m_impl->target_video_w = aligned_w;
            m_impl->target_video_h = aligned_h;
        }
    }
}

std::pair<int, int> FFmpegDecoder::target_video_size() const noexcept {
    if (!m_impl) return {0, 0};
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return {m_impl->target_video_w, m_impl->target_video_h};
}

void FFmpegDecoder::set_deband_enabled(bool enabled) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        m_impl->deband_enabled = enabled;
    }
}

bool FFmpegDecoder::is_deband_enabled() const noexcept {
    if (!m_impl) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->deband_enabled;
}

void FFmpegDecoder::apply_deband(Zenvra::Media::VideoFrame& frame, int range, int threshold) {
    if (frame.data.empty() || frame.width <= 4 || frame.height <= 4) return;
    if (frame.format != Zenvra::Media::VideoPixelFormat::BGRA32 &&
        frame.format != Zenvra::Media::VideoPixelFormat::RGBA32) {
        return; // Debanding supports 32-bit pixel formats
    }

    const int w = frame.width;
    const int h = frame.height;
    const int stride = frame.linesize > 0 ? frame.linesize : (w * 4);
    uint8_t* pixels = frame.data.data();

    // Multi-direction sample offsets for gradient banding detection
    const int r1 = std::clamp(range, 2, 8);
    const int r2 = std::max(1, r1 / 2);
    const int thresh = std::clamp(threshold, 2, 14);

    std::vector<uint8_t> temp_row(static_cast<size_t>(w) * 4);

    for (int y = 0; y < h; ++y) {
        const uint8_t* row_src = pixels + y * stride;
        uint8_t* row_dst = temp_row.data();

        const int y_up1 = std::max(0, y - r1);
        const int y_dn1 = std::min(h - 1, y + r1);
        const int y_up2 = std::max(0, y - r2);
        const int y_dn2 = std::min(h - 1, y + r2);

        const uint8_t* p_up1 = pixels + y_up1 * stride;
        const uint8_t* p_dn1 = pixels + y_dn1 * stride;
        const uint8_t* p_up2 = pixels + y_up2 * stride;
        const uint8_t* p_dn2 = pixels + y_dn2 * stride;

        for (int x = 0; x < w; ++x) {
            const int idx = x * 4;

            const int x_lt1 = std::max(0, x - r1) * 4;
            const int x_rt1 = std::min(w - 1, x + r1) * 4;
            const int x_lt2 = std::max(0, x - r2) * 4;
            const int x_rt2 = std::min(w - 1, x + r2) * 4;

            // Deterministic high-frequency subpixel dither hash (breaks banding lines without blurring)
            const uint32_t hash = static_cast<uint32_t>(x * 1234567 + y * 7654321 + 54321);
            const int dither = static_cast<int>((hash >> 29) & 3) - 1; // -1, 0, 1, 2

            bool is_banded = false;
            int smoothed_c[3] = {0, 0, 0};

            for (int c = 0; c < 3; ++c) {
                const int cur = row_src[idx + c];

                const int s1 = row_src[x_lt1 + c];
                const int s2 = row_src[x_rt1 + c];
                const int s3 = p_up1[idx + c];
                const int s4 = p_dn1[idx + c];

                const int s5 = p_up2[x_lt2 + c];
                const int s6 = p_up2[x_rt2 + c];
                const int s7 = p_dn2[x_lt2 + c];
                const int s8 = p_dn2[x_rt2 + c];

                const int min_val = std::min({s1, s2, s3, s4, s5, s6, s7, s8});
                const int max_val = std::max({s1, s2, s3, s4, s5, s6, s7, s8});
                const int avg = (s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + 4) >> 3;
                const int diff = std::abs(cur - avg);

                // Edge preservation: only touch pixels in very flat gradient regions (max_val - min_val <= thresh * 2)
                if (diff > 0 && diff <= thresh && (max_val - min_val) <= (thresh * 2)) {
                    is_banded = true;
                    // Gradient contour smoothing: blend towards local neighborhood average with micro-dither
                    smoothed_c[c] = std::clamp(((cur + avg) >> 1) + dither, 0, 255);
                } else {
                    smoothed_c[c] = cur;
                }
            }

            if (is_banded) {
                row_dst[idx + 0] = static_cast<uint8_t>(smoothed_c[0]);
                row_dst[idx + 1] = static_cast<uint8_t>(smoothed_c[1]);
                row_dst[idx + 2] = static_cast<uint8_t>(smoothed_c[2]);
            } else {
                row_dst[idx + 0] = row_src[idx + 0];
                row_dst[idx + 1] = row_src[idx + 1];
                row_dst[idx + 2] = row_src[idx + 2];
            }
            row_dst[idx + 3] = row_src[idx + 3];
        }

        std::memcpy(pixels + y * stride, temp_row.data(), static_cast<size_t>(w) * 4);
    }
}

void FFmpegDecoder::set_edge_aa_enabled(bool enabled) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        m_impl->edge_aa_enabled = enabled;
    }
}

bool FFmpegDecoder::is_edge_aa_enabled() const noexcept {
    if (!m_impl) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->edge_aa_enabled;
}

void FFmpegDecoder::apply_edge_aa(Zenvra::Media::VideoFrame& frame, int contrast_threshold) {
    if (frame.data.empty() || frame.width < 3 || frame.height < 3) return;
    if (frame.format != Zenvra::Media::VideoPixelFormat::BGRA32 &&
        frame.format != Zenvra::Media::VideoPixelFormat::RGBA32) {
        return;
    }

    const int w = frame.width;
    const int h = frame.height;
    const int stride = frame.linesize > 0 ? frame.linesize : (w * 4);
    uint8_t* pixels = frame.data.data();

    auto get_luma = [](const uint8_t* p, bool is_bgra) noexcept -> int {
        int r = is_bgra ? p[2] : p[0];
        int g = p[1];
        int b = is_bgra ? p[0] : p[2];
        return (r * 77 + g * 150 + b * 29) >> 8;
    };

    const bool is_bgra = (frame.format == Zenvra::Media::VideoPixelFormat::BGRA32);
    const int thresh = std::clamp(contrast_threshold, 8, 64);

    std::vector<uint8_t> prev_row(stride);
    std::vector<uint8_t> curr_row(stride);
    std::memcpy(curr_row.data(), pixels, stride);

    for (int y = 1; y < h - 1; ++y) {
        std::memcpy(prev_row.data(), curr_row.data(), stride);
        std::memcpy(curr_row.data(), pixels + y * stride, stride);
        const uint8_t* next_row = pixels + (y + 1) * stride;
        uint8_t* out_row = pixels + y * stride;

        for (int x = 1; x < w - 1; ++x) {
            const int idx = x * 4;
            const uint8_t* c = curr_row.data() + idx;
            const int l_c = get_luma(c, is_bgra);

            const uint8_t* l = curr_row.data() + idx - 4;
            const uint8_t* r = curr_row.data() + idx + 4;
            const uint8_t* u = prev_row.data() + idx;
            const uint8_t* d = next_row + idx;

            const int l_l = get_luma(l, is_bgra);
            const int l_r = get_luma(r, is_bgra);
            const int l_u = get_luma(u, is_bgra);
            const int l_d = get_luma(d, is_bgra);

            const int min_l = std::min({l_c, l_l, l_r, l_u, l_d});
            const int max_l = std::max({l_c, l_l, l_r, l_u, l_d});
            const int range = max_l - min_l;

            // Flat surface or gentle texture: untouched (0% blur)
            if (range < thresh) {
                continue;
            }

            // High contrast edge: detect edge orientation (horizontal vs vertical step)
            const int l_ul = get_luma(prev_row.data() + idx - 4, is_bgra);
            const int l_ur = get_luma(prev_row.data() + idx + 4, is_bgra);
            const int l_dl = get_luma(next_row + idx - 4, is_bgra);
            const int l_dr = get_luma(next_row + idx + 4, is_bgra);

            const int grad_h = std::abs(l_ul - l_ur) + 2 * std::abs(l_l - l_r) + std::abs(l_dl - l_dr);
            const int grad_v = std::abs(l_ul - l_dl) + 2 * std::abs(l_u - l_d) + std::abs(l_ur - l_dr);

            const uint8_t* blend_neighbor = nullptr;
            if (grad_v >= grad_h) {
                // Horizontal edge (contrast step along vertical direction)
                blend_neighbor = (std::abs(l_u - l_c) > std::abs(l_d - l_c)) ? u : d;
            } else {
                // Vertical edge (contrast step along horizontal direction)
                blend_neighbor = (std::abs(l_l - l_c) > std::abs(l_r - l_c)) ? l : r;
            }

            // Subpixel coverage blend: 75% center pixel + 25% edge neighbor (gentle anti-aliasing)
            out_row[idx + 0] = static_cast<uint8_t>((c[0] * 3 + blend_neighbor[0] + 2) >> 2);
            out_row[idx + 1] = static_cast<uint8_t>((c[1] * 3 + blend_neighbor[1] + 2) >> 2);
            out_row[idx + 2] = static_cast<uint8_t>((c[2] * 3 + blend_neighbor[2] + 2) >> 2);
        }
    }
}

void FFmpegDecoder::set_target_audio_format(int sample_rate, int channels) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        if (sample_rate > 0) m_impl->target_sample_rate = sample_rate;
        if (channels > 0) m_impl->target_channels = channels;
#ifdef ZDE_HAS_FFMPEG
        if (m_impl->swr_ctx) {
            swr_free(&m_impl->swr_ctx);
            m_impl->swr_ctx = nullptr;
        }
#endif
    }
}

int FFmpegDecoder::target_sample_rate() const noexcept {
    if (!m_impl) return 44100;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->target_sample_rate;
}

int FFmpegDecoder::target_channels() const noexcept {
    if (!m_impl) return 2;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->target_channels;
}

bool FFmpegDecoder::open(const Zenvra::Media::MediaSource& source) {
    close();

#ifdef ZDE_HAS_FFMPEG
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);

    if (!source.is_valid()) {
        m_impl->last_error = "Invalid media source";
        std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
        return false;
    }

    AVFormatContext* raw_fmt = nullptr;

    if (source.type() == Zenvra::Media::MediaSourceType::Memory) {
        m_impl->memory_source_buffer = source.memory_buffer();
        m_impl->memory_reader.data = m_impl->memory_source_buffer.data();
        m_impl->memory_reader.size = m_impl->memory_source_buffer.size();
        m_impl->memory_reader.pos = 0;

        constexpr size_t avio_buf_size = 32768;
        auto* avio_buf = static_cast<unsigned char*>(av_malloc(avio_buf_size));
        if (!avio_buf) {
            m_impl->last_error = "Failed to allocate AVIO buffer for memory source";
            std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
            return false;
        }

        AVIOContext* raw_avio = avio_alloc_context(
            avio_buf, static_cast<int>(avio_buf_size),
            0,
            &m_impl->memory_reader,
            &read_memory_packet,
            nullptr,
            &seek_memory_packet
        );

        if (!raw_avio) {
            av_free(avio_buf);
            m_impl->last_error = "Failed to create AVIOContext for memory source";
            std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
            return false;
        }
        m_impl->avio_ctx.reset(raw_avio);

        raw_fmt = avformat_alloc_context();
        if (!raw_fmt) {
            m_impl->last_error = "Failed to allocate AVFormatContext for memory source";
            std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
            return false;
        }
        raw_fmt->pb = m_impl->avio_ctx.get();
        raw_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

        int err = avformat_open_input(&raw_fmt, nullptr, nullptr, nullptr);
        if (err < 0) {
            m_impl->last_error = "avformat_open_input failed on memory buffer: " + ffmpeg_error_string(err);
            std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
            if (raw_fmt) avformat_free_context(raw_fmt);
            return false;
        }
    } else {
        std::string uri_str;
        if (source.type() == Zenvra::Media::MediaSourceType::StreamUrl) {
            uri_str = source.uri();
        } else {
            std::filesystem::path p = source.path();
            std::error_code ec;
            if (!std::filesystem::exists(p, ec)) {
                auto from_cwd = std::filesystem::current_path(ec) / p;
                if (std::filesystem::exists(from_cwd, ec)) {
                    p = from_cwd;
                } else {
                    const std::filesystem::path candidates[] = {
                        std::filesystem::current_path(ec) / "Assets" / "vid" / p.filename(),
                        std::filesystem::current_path(ec) / "Assets" / "sounds" / p.filename(),
                        std::filesystem::current_path(ec) / "Assets" / "icons" / p.filename(),
                        std::filesystem::current_path(ec) / "Assets" / p.filename(),
                        std::filesystem::current_path(ec) / "videos" / p.filename(),
                        std::filesystem::current_path(ec) / "videos" / p,
                        std::filesystem::current_path(ec) / "Assets" / p
                    };
                    for (const auto& cand : candidates) {
                        if (std::filesystem::exists(cand, ec)) {
                            p = cand;
                            break;
                        }
                    }
                }
            }
            if (std::filesystem::exists(p, ec)) {
                p = std::filesystem::absolute(p, ec);
            }
            uri_str = p.string();
        }

        int err = avformat_open_input(&raw_fmt, uri_str.c_str(), nullptr, nullptr);
        if (err < 0) {
            m_impl->last_error = "avformat_open_input failed for '" + uri_str + "': " + ffmpeg_error_string(err);
            std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
            return false;
        }
    }

    m_impl->format_ctx.reset(raw_fmt);

    int find_err = avformat_find_stream_info(m_impl->format_ctx.get(), nullptr);
    if (find_err < 0) {
        m_impl->last_error = "avformat_find_stream_info failed: " + ffmpeg_error_string(find_err);
        std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
        m_impl->format_ctx.reset();
        return false;
    }

    // Extract Container Metadata
    if (m_impl->format_ctx->iformat) {
        if (m_impl->format_ctx->iformat->name) {
            m_impl->metadata.container_format = m_impl->format_ctx->iformat->name;
        }
        if (m_impl->format_ctx->iformat->long_name) {
            m_impl->metadata.container_long_name = m_impl->format_ctx->iformat->long_name;
        }
    }
    if (m_impl->format_ctx->duration != AV_NOPTS_VALUE) {
        m_impl->metadata.duration_seconds = static_cast<double>(m_impl->format_ctx->duration) / AV_TIME_BASE;
    }
    m_impl->metadata.bit_rate = m_impl->format_ctx->bit_rate;

    // Tags
    if (m_impl->format_ctx->metadata) {
        auto get_tag = [&](const char* key) -> std::string {
            AVDictionaryEntry* e = av_dict_get(m_impl->format_ctx->metadata, key, nullptr, 0);
            return (e && e->value) ? e->value : "";
        };
        m_impl->metadata.title = get_tag("title");
        m_impl->metadata.artist = get_tag("artist");
        m_impl->metadata.album = get_tag("album");
        m_impl->metadata.genre = get_tag("genre");
        m_impl->metadata.date = get_tag("date");
        m_impl->metadata.comment = get_tag("comment");
    }

    // Enumerate All Stream Tracks
    for (unsigned int i = 0; i < m_impl->format_ctx->nb_streams; ++i) {
        AVStream* st = m_impl->format_ctx->streams[i];
        if (!st || !st->codecpar) continue;

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            Zenvra::Media::VideoStreamTrackInfo v_track;
            v_track.index = static_cast<int>(i);
            const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
            if (c) {
                if (c->name) v_track.codec_name = c->name;
                if (c->long_name) v_track.codec_long_name = c->long_name;
            }
            v_track.width = st->codecpar->width;
            v_track.height = st->codecpar->height;
            v_track.fps = av_q2d(st->avg_frame_rate);
            v_track.aspect_ratio_num = st->sample_aspect_ratio.num;
            v_track.aspect_ratio_den = st->sample_aspect_ratio.den;
            v_track.bit_rate = st->codecpar->bit_rate;
            v_track.rotation_degrees = get_stream_rotation(st);

            const char* pix_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(st->codecpar->format));
            if (pix_name) v_track.pixel_format_name = pix_name;

            v_track.color_space = to_zde_color_space(st->codecpar->color_space, st->codecpar->color_primaries);
            v_track.color_range = (st->codecpar->color_range == AVCOL_RANGE_JPEG) 
                                ? Zenvra::Media::ColorRange::Full_JPEG 
                                : Zenvra::Media::ColorRange::Limited_MPEG;

            v_track.is_hdr = (v_track.color_space == Zenvra::Media::ColorSpace::BT2020_HDR) ||
                             (st->codecpar->color_trc == AVCOL_TRC_SMPTE2084) ||
                             (st->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67);

            v_track.is_interlaced = (st->codecpar->field_order != AV_FIELD_PROGRESSIVE &&
                                     st->codecpar->field_order != AV_FIELD_UNKNOWN);

            m_impl->metadata.video_tracks.push_back(v_track);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            Zenvra::Media::AudioStreamTrackInfo a_track;
            a_track.index = static_cast<int>(i);
            const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
            if (c) {
                if (c->name) a_track.codec_name = c->name;
                if (c->long_name) a_track.codec_long_name = c->long_name;
            }
            a_track.sample_rate = st->codecpar->sample_rate;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
            a_track.channels = st->codecpar->ch_layout.nb_channels;
#else
            a_track.channels = st->codecpar->channels;
#endif
            a_track.bit_rate = st->codecpar->bit_rate;
            if (st->metadata) {
                AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
                if (lang && lang->value) a_track.language = lang->value;
                AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
                if (title && title->value) a_track.title = title->value;
            }
            m_impl->metadata.audio_tracks.push_back(a_track);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            Zenvra::Media::SubtitleStreamTrackInfo sub_track;
            sub_track.index = static_cast<int>(i);
            const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
            if (c && c->name) sub_track.codec_name = c->name;
            if (st->metadata) {
                AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0);
                if (lang && lang->value) sub_track.language = lang->value;
                AVDictionaryEntry* title = av_dict_get(st->metadata, "title", nullptr, 0);
                if (title && title->value) sub_track.title = title->value;
            }
            m_impl->metadata.subtitle_tracks.push_back(sub_track);
        }
    }

    // Select primary video track if available
    if (!m_impl->metadata.video_tracks.empty()) {
        int v_idx = m_impl->metadata.video_tracks[0].index;
        AVStream* st = m_impl->format_ctx->streams[v_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) {
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            if (ctx) {
                avcodec_parameters_to_context(ctx, st->codecpar);
                ctx->thread_count = 0;
                ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
                ctx->flags |= AV_CODEC_FLAG_LOOP_FILTER;
                ctx->skip_loop_filter = AVDISCARD_DEFAULT;
                if (avcodec_open2(ctx, codec, nullptr) >= 0) {
                    m_impl->video_codec_ctx.reset(ctx);
                    m_impl->active_video_stream_idx = v_idx;
                    m_impl->video_time_base = st->time_base;
                    m_impl->video_rotation = get_stream_rotation(st);
                    m_impl->metadata.active_video_track = 0;
                } else {
                    avcodec_free_context(&ctx);
                }
            }
        }
    }

    // Select primary audio track if available
    if (!m_impl->metadata.audio_tracks.empty()) {
        int a_idx = m_impl->metadata.audio_tracks[0].index;
        AVStream* st = m_impl->format_ctx->streams[a_idx];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (codec) {
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            if (ctx && avcodec_parameters_to_context(ctx, st->codecpar) >= 0 && avcodec_open2(ctx, codec, nullptr) >= 0) {
                m_impl->audio_codec_ctx.reset(ctx);
                m_impl->active_audio_stream_idx = a_idx;
                m_impl->audio_time_base = st->time_base;
                m_impl->metadata.active_audio_track = 0;

                // SwrContext configuration
                if (m_impl->swr_ctx) {
                    swr_free(&m_impl->swr_ctx);
                    m_impl->swr_ctx = nullptr;
                }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
                AVChannelLayout out_ch;
                av_channel_layout_default(&out_ch, m_impl->target_channels);
                swr_alloc_set_opts2(&m_impl->swr_ctx, &out_ch, AV_SAMPLE_FMT_FLT, m_impl->target_sample_rate,
                                    &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&out_ch);
#else
                int64_t in_ch = ctx->channel_layout ? ctx->channel_layout : av_get_default_channel_layout(ctx->channels);
                int64_t out_ch = av_get_default_channel_layout(m_impl->target_channels);
                m_impl->swr_ctx = swr_alloc_set_opts(nullptr, out_ch, AV_SAMPLE_FMT_FLT, m_impl->target_sample_rate,
                                                     in_ch, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
#endif
                if (m_impl->swr_ctx) {
                    swr_init(m_impl->swr_ctx);
                }
            } else if (ctx) {
                avcodec_free_context(&ctx);
            }
        }
    }

    m_impl->demux_eof = false;
    m_impl->is_open = (m_impl->video_codec_ctx != nullptr || m_impl->audio_codec_ctx != nullptr);
    if (!m_impl->is_open) {
        m_impl->last_error = "Failed to initialize any video or audio decoder";
        std::cerr << "[FFmpegDecoder] Error: " << m_impl->last_error << '\n';
    }
    return m_impl->is_open;
#else
    (void)source;
    return false;
#endif
}

bool FFmpegDecoder::select_video_track(int track_index) {
#ifdef ZDE_HAS_FFMPEG
    if (!m_impl || !m_impl->format_ctx) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);

    if (track_index < 0 || track_index >= static_cast<int>(m_impl->format_ctx->nb_streams)) {
        m_impl->last_error = "Invalid video track index: " + std::to_string(track_index);
        return false;
    }

    AVStream* st = m_impl->format_ctx->streams[track_index];
    if (!st || st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        m_impl->last_error = "Stream at index " + std::to_string(track_index) + " is not a video stream";
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        m_impl->last_error = "No decoder found for video codec ID " + std::to_string(st->codecpar->codec_id);
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->thread_count = 0;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    ctx->flags |= AV_CODEC_FLAG_LOOP_FILTER;
    ctx->skip_loop_filter = AVDISCARD_DEFAULT;
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }

    m_impl->video_codec_ctx.reset(ctx);
    m_impl->active_video_stream_idx = track_index;
    m_impl->video_time_base = st->time_base;
    m_impl->video_rotation = get_stream_rotation(st);
    m_impl->video_packet_queue.clear();

    for (size_t i = 0; i < m_impl->metadata.video_tracks.size(); ++i) {
        if (m_impl->metadata.video_tracks[i].index == track_index) {
            m_impl->metadata.active_video_track = static_cast<int>(i);
            break;
        }
    }
    return true;
#else
    (void)track_index;
    return false;
#endif
}

bool FFmpegDecoder::select_audio_track(int track_index) {
#ifdef ZDE_HAS_FFMPEG
    if (!m_impl || !m_impl->format_ctx) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);

    if (track_index < 0 || track_index >= static_cast<int>(m_impl->format_ctx->nb_streams)) {
        m_impl->last_error = "Invalid audio track index: " + std::to_string(track_index);
        return false;
    }

    AVStream* st = m_impl->format_ctx->streams[track_index];
    if (!st || st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        m_impl->last_error = "Stream at index " + std::to_string(track_index) + " is not an audio stream";
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        m_impl->last_error = "No decoder found for audio codec ID " + std::to_string(st->codecpar->codec_id);
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }

    m_impl->audio_codec_ctx.reset(ctx);
    m_impl->active_audio_stream_idx = track_index;
    m_impl->audio_time_base = st->time_base;
    m_impl->audio_packet_queue.clear();

    if (m_impl->swr_ctx) {
        swr_free(&m_impl->swr_ctx);
        m_impl->swr_ctx = nullptr;
    }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    AVChannelLayout out_ch;
    av_channel_layout_default(&out_ch, m_impl->target_channels);
    swr_alloc_set_opts2(&m_impl->swr_ctx, &out_ch, AV_SAMPLE_FMT_FLT, m_impl->target_sample_rate,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&out_ch);
#else
    int64_t in_ch = ctx->channel_layout ? ctx->channel_layout : av_get_default_channel_layout(ctx->channels);
    int64_t out_ch = av_get_default_channel_layout(m_impl->target_channels);
    m_impl->swr_ctx = swr_alloc_set_opts(nullptr, out_ch, AV_SAMPLE_FMT_FLT, m_impl->target_sample_rate,
                                         in_ch, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
#endif
    if (m_impl->swr_ctx) {
        swr_init(m_impl->swr_ctx);
    }

    for (size_t i = 0; i < m_impl->metadata.audio_tracks.size(); ++i) {
        if (m_impl->metadata.audio_tracks[i].index == track_index) {
            m_impl->metadata.active_audio_track = static_cast<int>(i);
            break;
        }
    }
    return true;
#else
    (void)track_index;
    return false;
#endif
}

void FFmpegDecoder::close() {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
        m_impl->metadata = {};
        m_impl->is_open = false;
#ifdef ZDE_HAS_FFMPEG
        if (m_impl->sws_ctx) {
            sws_freeContext(m_impl->sws_ctx);
            m_impl->sws_ctx = nullptr;
        }
        if (m_impl->swr_ctx) {
            swr_free(&m_impl->swr_ctx);
            m_impl->swr_ctx = nullptr;
        }
        m_impl->video_packet_queue.clear();
        m_impl->audio_packet_queue.clear();
        m_impl->video_codec_ctx.reset();
        m_impl->audio_codec_ctx.reset();
        m_impl->format_ctx.reset();
        m_impl->avio_ctx.reset();
        m_impl->memory_source_buffer.clear();
        m_impl->memory_reader = {};
        m_impl->active_video_stream_idx = -1;
        m_impl->active_audio_stream_idx = -1;
        m_impl->current_sws_src_w = 0;
        m_impl->current_sws_src_h = 0;
        m_impl->current_sws_src_fmt = AV_PIX_FMT_NONE;
        m_impl->current_sws_dst_fmt = AV_PIX_FMT_NONE;
        m_impl->current_sws_colorspace = AVCOL_SPC_UNSPECIFIED;
        m_impl->current_sws_color_range = AVCOL_RANGE_UNSPECIFIED;
        m_impl->demux_eof = false;
#endif
    }
}

bool FFmpegDecoder::is_open() const noexcept {
    if (!m_impl) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->is_open;
}

const Zenvra::Media::MediaMetadata& FFmpegDecoder::metadata() const noexcept {
    static const Zenvra::Media::MediaMetadata empty{};
    if (!m_impl) return empty;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->metadata;
}

std::optional<Zenvra::Media::VideoFrame> FFmpegDecoder::decode_next_video_frame(bool fast_preview) {
#ifdef ZDE_HAS_FFMPEG
    if (!m_impl) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);

    if (!m_impl->is_open || !m_impl->video_codec_ctx) {
        return std::nullopt;
    }

    AVFramePtr frame = make_frame();
    if (!frame) return std::nullopt;

    AVPixelFormat dst_pix_fmt = to_ffmpeg_pixel_format(m_impl->target_video_format);
    int bpp = get_bytes_per_pixel(m_impl->target_video_format);

    while (true) {
        // 1. Try to receive already decoded frame
        int ret = avcodec_receive_frame(m_impl->video_codec_ctx.get(), frame.get());
        if (ret == 0) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 0, 100)
            bool is_interlaced = (frame->flags & AV_FRAME_FLAG_INTERLACED);
#else
            bool is_interlaced = (frame->interlaced_frame != 0);
#endif
            // Fast deinterlace: eliminate comb-teeth / serrated gerigi on moving fields
            if (is_interlaced && frame->data[0] && frame->linesize[0] > 0) {
                const int fh = frame->height;
                const int stride = frame->linesize[0];
                uint8_t* y_plane = frame->data[0];
                for (int y = 1; y < fh - 1; y += 2) {
                    uint8_t* prev = y_plane + (y - 1) * stride;
                    uint8_t* curr = y_plane + y * stride;
                    uint8_t* next = y_plane + (y + 1) * stride;
                    for (int x = 0; x < frame->width; ++x) {
                        curr[x] = static_cast<uint8_t>((prev[x] + 2 * curr[x] + next[x] + 2) >> 2);
                    }
                }
            }

            m_impl->recreate_sws_if_needed(frame->width, frame->height,
                                          static_cast<AVPixelFormat>(frame->format), dst_pix_fmt,
                                          frame->colorspace, frame->color_range);

            if (!m_impl->sws_ctx) {
                m_impl->last_error = "Failed to initialize SwsContext";
                return std::nullopt;
            }

            int out_w = (m_impl->target_video_w > 0) ? m_impl->target_video_w : frame->width;
            int out_h = (m_impl->target_video_h > 0) ? m_impl->target_video_h : frame->height;
            out_w = (out_w + 1) & ~1;
            out_h = (out_h + 1) & ~1;

            Zenvra::Media::VideoFrame vf;
            vf.width = out_w;
            vf.height = out_h;
            vf.linesize = vf.width * bpp;
            vf.format = m_impl->target_video_format;
            vf.rotation_degrees = m_impl->video_rotation;
            vf.is_keyframe = (frame->flags & AV_FRAME_FLAG_KEY);
            vf.data.resize(static_cast<size_t>(vf.linesize) * vf.height);

            int64_t pts_val = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            if (pts_val != AV_NOPTS_VALUE) {
                vf.timestamp_seconds = pts_val * av_q2d(m_impl->video_time_base);
            }
            if (frame->duration > 0) {
                vf.duration_seconds = frame->duration * av_q2d(m_impl->video_time_base);
            }

            uint8_t* dst[4] = { vf.data.data(), nullptr, nullptr, nullptr };
            int dst_stride[4] = { vf.linesize, 0, 0, 0 };

            sws_scale(m_impl->sws_ctx, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
            if (!fast_preview && m_impl->deband_enabled) {
                apply_deband(vf);
            }
            if (!fast_preview && m_impl->edge_aa_enabled) {
                apply_edge_aa(vf);
            }
            return vf;
        }

        if (ret != AVERROR(EAGAIN)) {
            // EOF or unrecoverable error
            if (ret == AVERROR_EOF || m_impl->demux_eof) {
                return std::nullopt;
            }
        }

        // 2. Feed packets from queue or demuxer
        if (!m_impl->video_packet_queue.empty()) {
            AVPacketPtr pkt = std::move(m_impl->video_packet_queue.front());
            m_impl->video_packet_queue.pop_front();
            avcodec_send_packet(m_impl->video_codec_ctx.get(), pkt.get());
        } else if (!m_impl->demux_eof) {
            // Read from container until we get a video packet
            bool got_video = false;
            while (!m_impl->demux_eof && !got_video) {
                if (m_impl->read_next_demux_packet()) {
                    if (!m_impl->video_packet_queue.empty()) {
                        got_video = true;
                    }
                }
            }
            if (!m_impl->video_packet_queue.empty()) {
                AVPacketPtr pkt = std::move(m_impl->video_packet_queue.front());
                m_impl->video_packet_queue.pop_front();
                avcodec_send_packet(m_impl->video_codec_ctx.get(), pkt.get());
            } else if (m_impl->demux_eof) {
                avcodec_send_packet(m_impl->video_codec_ctx.get(), nullptr);
            }
        } else {
            // Flush decoder
            avcodec_send_packet(m_impl->video_codec_ctx.get(), nullptr);
            int flush_ret = avcodec_receive_frame(m_impl->video_codec_ctx.get(), frame.get());
            if (flush_ret == 0) {
                m_impl->recreate_sws_if_needed(frame->width, frame->height,
                                              static_cast<AVPixelFormat>(frame->format), dst_pix_fmt,
                                              frame->colorspace, frame->color_range);
                if (m_impl->sws_ctx) {
                    Zenvra::Media::VideoFrame vf;
                    vf.width = frame->width;
                    vf.height = frame->height;
                    vf.linesize = vf.width * bpp;
                    vf.format = m_impl->target_video_format;
                    vf.rotation_degrees = m_impl->video_rotation;
                    vf.is_keyframe = (frame->flags & AV_FRAME_FLAG_KEY);
                    vf.data.resize(static_cast<size_t>(vf.linesize) * vf.height);
                    int64_t pts_val = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                    if (pts_val != AV_NOPTS_VALUE) {
                        vf.timestamp_seconds = pts_val * av_q2d(m_impl->video_time_base);
                    }
                    uint8_t* dst[4] = { vf.data.data(), nullptr, nullptr, nullptr };
                    int dst_stride[4] = { vf.linesize, 0, 0, 0 };
                    sws_scale(m_impl->sws_ctx, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
                    if (m_impl->deband_enabled) {
                        apply_deband(vf);
                    }
                    return vf;
                }
            }
            return std::nullopt;
        }
    }
#else
    return std::nullopt;
#endif
}

std::optional<Zenvra::Media::AudioBuffer> FFmpegDecoder::decode_next_audio_samples(int max_samples) {
#ifdef ZDE_HAS_FFMPEG
    if (!m_impl) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);

    if (!m_impl->is_open || !m_impl->audio_codec_ctx || !m_impl->swr_ctx) {
        return std::nullopt;
    }

    AVFramePtr frame = make_frame();
    if (!frame) return std::nullopt;

    Zenvra::Media::AudioBuffer buf;
    buf.sample_rate = m_impl->target_sample_rate;
    buf.channels = m_impl->target_channels;
    buf.format = Zenvra::Media::AudioSampleFormat::Float32;

    std::vector<float> temp_out(static_cast<size_t>(max_samples) * m_impl->target_channels);

    while (true) {
        int ret = avcodec_receive_frame(m_impl->audio_codec_ctx.get(), frame.get());
        if (ret == 0) {
            int in_rate = m_impl->audio_codec_ctx->sample_rate > 0 ? m_impl->audio_codec_ctx->sample_rate : m_impl->target_sample_rate;
            int64_t delay = swr_get_delay(m_impl->swr_ctx, in_rate);
            int dst_nb_samples = static_cast<int>(av_rescale_rnd(delay + frame->nb_samples, m_impl->target_sample_rate, in_rate, AV_ROUND_UP));
            int out_capacity = std::max(dst_nb_samples, max_samples);
            temp_out.resize(static_cast<size_t>(out_capacity) * m_impl->target_channels);

            uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(temp_out.data()) };
            int converted = swr_convert(m_impl->swr_ctx, out_data, out_capacity,
                                        const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            if (converted > 0) {
                size_t total = static_cast<size_t>(converted) * m_impl->target_channels;
                buf.samples.assign(temp_out.begin(), temp_out.begin() + total);
                int64_t pts_val = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                if (pts_val != AV_NOPTS_VALUE) {
                    buf.timestamp_seconds = pts_val * av_q2d(m_impl->audio_time_base);
                }
                buf.duration_seconds = static_cast<double>(converted) / m_impl->target_sample_rate;
                return buf;
            }
        }

        if (ret != AVERROR(EAGAIN)) {
            if (ret == AVERROR_EOF || m_impl->demux_eof) {
                return std::nullopt;
            }
        }

        if (!m_impl->audio_packet_queue.empty()) {
            AVPacketPtr pkt = std::move(m_impl->audio_packet_queue.front());
            m_impl->audio_packet_queue.pop_front();
            avcodec_send_packet(m_impl->audio_codec_ctx.get(), pkt.get());
        } else if (!m_impl->demux_eof) {
            bool got_audio = false;
            while (!m_impl->demux_eof && !got_audio) {
                if (m_impl->read_next_demux_packet()) {
                    if (!m_impl->audio_packet_queue.empty()) {
                        got_audio = true;
                    }
                }
            }
            if (!m_impl->audio_packet_queue.empty()) {
                AVPacketPtr pkt = std::move(m_impl->audio_packet_queue.front());
                m_impl->audio_packet_queue.pop_front();
                avcodec_send_packet(m_impl->audio_codec_ctx.get(), pkt.get());
            } else if (m_impl->demux_eof) {
                avcodec_send_packet(m_impl->audio_codec_ctx.get(), nullptr);
            }
        } else {
            avcodec_send_packet(m_impl->audio_codec_ctx.get(), nullptr);
            int flush_ret = avcodec_receive_frame(m_impl->audio_codec_ctx.get(), frame.get());
            if (flush_ret == 0) {
                int in_rate = m_impl->audio_codec_ctx->sample_rate > 0 ? m_impl->audio_codec_ctx->sample_rate : m_impl->target_sample_rate;
                int64_t delay = swr_get_delay(m_impl->swr_ctx, in_rate);
                int dst_nb_samples = static_cast<int>(av_rescale_rnd(delay + frame->nb_samples, m_impl->target_sample_rate, in_rate, AV_ROUND_UP));
                int out_capacity = std::max(dst_nb_samples, max_samples);
                temp_out.resize(static_cast<size_t>(out_capacity) * m_impl->target_channels);

                uint8_t* out_data[1] = { reinterpret_cast<uint8_t*>(temp_out.data()) };
                int converted = swr_convert(m_impl->swr_ctx, out_data, out_capacity,
                                            const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                if (converted > 0) {
                    size_t total = static_cast<size_t>(converted) * m_impl->target_channels;
                    buf.samples.assign(temp_out.begin(), temp_out.begin() + total);
                    int64_t pts_val = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                    if (pts_val != AV_NOPTS_VALUE) {
                        buf.timestamp_seconds = pts_val * av_q2d(m_impl->audio_time_base);
                    }
                    buf.duration_seconds = static_cast<double>(converted) / m_impl->target_sample_rate;
                    return buf;
                }
            }
            return std::nullopt;
        }
    }
#else
    (void)max_samples;
    return std::nullopt;
#endif
}

bool FFmpegDecoder::seek(double timestamp_seconds, Zenvra::Media::SeekMode mode) {
#ifdef ZDE_HAS_FFMPEG
    if (!m_impl) return false;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    if (!m_impl->is_open || !m_impl->format_ctx) return false;

    int stream_idx = (m_impl->active_video_stream_idx >= 0) ? m_impl->active_video_stream_idx : m_impl->active_audio_stream_idx;
    AVRational tb = (m_impl->active_video_stream_idx >= 0) ? m_impl->video_time_base : m_impl->audio_time_base;
    int64_t ts = av_rescale_q(static_cast<int64_t>(timestamp_seconds * AV_TIME_BASE), AV_TIME_BASE_Q, tb);

    if (av_seek_frame(m_impl->format_ctx.get(), stream_idx, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        int64_t fallback_ts = static_cast<int64_t>(timestamp_seconds * AV_TIME_BASE);
        av_seek_frame(m_impl->format_ctx.get(), -1, fallback_ts, AVSEEK_FLAG_BACKWARD);
    }

    m_impl->video_packet_queue.clear();
    m_impl->audio_packet_queue.clear();
    m_impl->demux_eof = false;

    if (m_impl->video_codec_ctx) avcodec_flush_buffers(m_impl->video_codec_ctx.get());
    if (m_impl->audio_codec_ctx) avcodec_flush_buffers(m_impl->audio_codec_ctx.get());
    if (m_impl->swr_ctx) {
        swr_close(m_impl->swr_ctx);
        swr_init(m_impl->swr_ctx);
    }

    // In FastKeyframe mode (used for ultra-responsive live scrubbing like VLC),
    // skip the frame-by-frame forward decoding loop to return immediately.
    if (mode == Zenvra::Media::SeekMode::Exact && timestamp_seconds > 0.0) {
        // 1. Fast forward video stream to timestamp_seconds
        AVFramePtr v_frame = make_frame();
        AVPixelFormat dst_pix_fmt = to_ffmpeg_pixel_format(m_impl->target_video_format);
        while (m_impl->video_codec_ctx && v_frame) {
            int ret = avcodec_receive_frame(m_impl->video_codec_ctx.get(), v_frame.get());
            if (ret == 0) {
                int64_t pts_val = (v_frame->best_effort_timestamp != AV_NOPTS_VALUE) ? v_frame->best_effort_timestamp : v_frame->pts;
                double pts = (pts_val != AV_NOPTS_VALUE) ? (pts_val * av_q2d(m_impl->video_time_base)) : 0.0;
                if (pts >= timestamp_seconds - 0.04) {
                    break;
                }
            } else if (ret == AVERROR(EAGAIN)) {
                if (!m_impl->video_packet_queue.empty()) {
                    AVPacketPtr pkt = std::move(m_impl->video_packet_queue.front());
                    m_impl->video_packet_queue.pop_front();
                    avcodec_send_packet(m_impl->video_codec_ctx.get(), pkt.get());
                } else if (!m_impl->demux_eof) {
                    if (m_impl->read_next_demux_packet() && !m_impl->video_packet_queue.empty()) {
                        AVPacketPtr pkt = std::move(m_impl->video_packet_queue.front());
                        m_impl->video_packet_queue.pop_front();
                        avcodec_send_packet(m_impl->video_codec_ctx.get(), pkt.get());
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        // 2. Fast forward audio stream to timestamp_seconds
        AVFramePtr a_frame = make_frame();
        while (m_impl->audio_codec_ctx && a_frame) {
            int ret = avcodec_receive_frame(m_impl->audio_codec_ctx.get(), a_frame.get());
            if (ret == 0) {
                int64_t pts_val = (a_frame->best_effort_timestamp != AV_NOPTS_VALUE) ? a_frame->best_effort_timestamp : a_frame->pts;
                double pts = (pts_val != AV_NOPTS_VALUE) ? (pts_val * av_q2d(m_impl->audio_time_base)) : 0.0;
                if (pts >= timestamp_seconds - 0.05) {
                    break;
                }
            } else if (ret == AVERROR(EAGAIN)) {
                if (!m_impl->audio_packet_queue.empty()) {
                    AVPacketPtr pkt = std::move(m_impl->audio_packet_queue.front());
                    m_impl->audio_packet_queue.pop_front();
                    avcodec_send_packet(m_impl->audio_codec_ctx.get(), pkt.get());
                } else if (!m_impl->demux_eof) {
                    if (m_impl->read_next_demux_packet() && !m_impl->audio_packet_queue.empty()) {
                        AVPacketPtr pkt = std::move(m_impl->audio_packet_queue.front());
                        m_impl->audio_packet_queue.pop_front();
                        avcodec_send_packet(m_impl->audio_codec_ctx.get(), pkt.get());
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }
    return true;
#else
    (void)timestamp_seconds;
    (void)mode;
    return false;
#endif
}

const std::string& FFmpegDecoder::last_error() const noexcept {
    static const std::string empty;
    if (!m_impl) return empty;
    std::lock_guard<std::mutex> lock(m_impl->decoder_mutex);
    return m_impl->last_error;
}

} // namespace Zenvra::Drivers::Media
