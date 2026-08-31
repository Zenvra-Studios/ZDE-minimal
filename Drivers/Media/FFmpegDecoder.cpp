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
    AVPixelFormat current_sws_src_fmt = AV_PIX_FMT_NONE;
    AVPixelFormat current_sws_dst_fmt = AV_PIX_FMT_NONE;
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

    void recreate_sws_if_needed(int src_w, int src_h, AVPixelFormat src_fmt, AVPixelFormat dst_fmt) {
        if (sws_ctx && current_sws_src_w == src_w && current_sws_src_h == src_h &&
            current_sws_src_fmt == src_fmt && current_sws_dst_fmt == dst_fmt) {
            return;
        }
        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }
        sws_ctx = sws_getContext(
            src_w, src_h, src_fmt,
            src_w, src_h, dst_fmt,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr
        );
        current_sws_src_w = src_w;
        current_sws_src_h = src_h;
        current_sws_src_fmt = src_fmt;
        current_sws_dst_fmt = dst_fmt;
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

std::optional<Zenvra::Media::VideoFrame> FFmpegDecoder::decode_next_video_frame() {
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
            m_impl->recreate_sws_if_needed(frame->width, frame->height,
                                          static_cast<AVPixelFormat>(frame->format), dst_pix_fmt);

            if (!m_impl->sws_ctx) {
                m_impl->last_error = "Failed to initialize SwsContext";
                return std::nullopt;
            }

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
            if (frame->duration > 0) {
                vf.duration_seconds = frame->duration * av_q2d(m_impl->video_time_base);
            }

            uint8_t* dst[4] = { vf.data.data(), nullptr, nullptr, nullptr };
            int dst_stride[4] = { vf.linesize, 0, 0, 0 };

            sws_scale(m_impl->sws_ctx, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
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
                                              static_cast<AVPixelFormat>(frame->format), dst_pix_fmt);
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

    if (timestamp_seconds > 0.0) {
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
