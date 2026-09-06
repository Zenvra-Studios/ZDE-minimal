#include "FFmpegPlayer.hpp"
#include "Media/MediaFactory.hpp"
#include "Drivers/Audio/AudioEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Zenvra::Drivers::Media {

struct FFmpegPlayer::Impl {
    FFmpegDecoder decoder;
    Zenvra::Media::PlaybackState state = Zenvra::Media::PlaybackState::Stopped;
    std::chrono::steady_clock::time_point play_start_time;
    double base_position = 0.0;
    float volume = 1.0f;
    bool is_looping = false;
    float playback_rate = 1.0f;
    std::string error_msg;

    // Multithreaded Decode Engine
    std::atomic<bool> worker_running{false};
    std::thread worker_thread;
    std::deque<Zenvra::Media::VideoFrame> video_frame_queue;
    std::optional<Zenvra::Media::VideoFrame> latest_rendered_frame;
    mutable std::mutex player_mutex;

    // Asynchronous Seek Coalescing for 100% Zero-Latency UI Scrubbing
    std::atomic<bool> seek_requested{false};
    std::atomic<double> seek_target_seconds{0.0};
    std::atomic<Zenvra::Media::SeekMode> seek_target_mode{Zenvra::Media::SeekMode::FastKeyframe};
    std::condition_variable worker_cv;

    FFmpegPlayer* owner = nullptr;

    void start_worker() {
        stop_worker();
        worker_running.store(true, std::memory_order_relaxed);
        worker_thread = std::thread(&Impl::worker_loop, this);
    }

    void stop_worker() {
        if (worker_running.load(std::memory_order_relaxed)) {
            worker_running.store(false, std::memory_order_relaxed);
            worker_cv.notify_all();
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
        }
    }

    void worker_loop() {
        while (worker_running.load(std::memory_order_relaxed)) {
            // Priority 1: Handle asynchronous seek requests immediately (coalesced)
            if (seek_requested.exchange(false, std::memory_order_acq_rel)) {
                double target = seek_target_seconds.load(std::memory_order_acquire);
                auto mode = seek_target_mode.load(std::memory_order_acquire);

                {
                    std::lock_guard<std::mutex> lock(player_mutex);
                    Audio::AudioEngine::instance().device().clear_stream_samples();
                    Audio::AudioEngine::instance().device().reset_stream_clock(target);
                    video_frame_queue.clear();
                    base_position = target;
                    play_start_time = std::chrono::steady_clock::now();
                }

                if (decoder.seek(target, mode)) {
                    bool fast = (mode == Zenvra::Media::SeekMode::FastKeyframe);
                    auto frame = decoder.decode_next_video_frame(fast);
                    if (frame) {
                        std::lock_guard<std::mutex> lock(player_mutex);
                        latest_rendered_frame = std::move(frame);
                    }
                }
                continue;
            }

            if (state != Zenvra::Media::PlaybackState::Playing) {
                std::unique_lock<std::mutex> lock(player_mutex);
                worker_cv.wait_for(lock, std::chrono::milliseconds(20), [this] {
                    return !worker_running.load(std::memory_order_relaxed) ||
                           seek_requested.load(std::memory_order_relaxed) ||
                           state == Zenvra::Media::PlaybackState::Playing;
                });
                continue;
            }

            double cur_pos = get_current_position_unlocked();
            double dur = decoder.metadata().duration_seconds;

            if (dur > 0.0 && cur_pos >= dur) {
                if (is_looping) {
                    std::lock_guard<std::mutex> lock(player_mutex);
                    decoder.seek(0.0, Zenvra::Media::SeekMode::FastKeyframe);
                    video_frame_queue.clear();
                    Audio::AudioEngine::instance().device().clear_stream_samples();
                    base_position = 0.0;
                    play_start_time = std::chrono::steady_clock::now();
                    continue;
                } else {
                    {
                        std::lock_guard<std::mutex> lock(player_mutex);
                        state = Zenvra::Media::PlaybackState::Stopped;
                        base_position = dur;
                    }
                    if (owner) {
                        owner->notify_end_of_stream();
                        owner->notify_state_changed(Zenvra::Media::PlaybackState::Stopped);
                    }
                    continue;
                }
            }

            bool did_work = false;

            // 1. Audio Pre-fetch & Stream Submit
            if (decoder.metadata().has_audio()) {
                size_t buffered = Audio::AudioEngine::instance().device().buffered_stream_samples();
                const auto& spec = Audio::AudioEngine::instance().device().spec();
                size_t target_buffer = static_cast<size_t>(spec.sample_rate * spec.channels); // ~1.0s buffer
                if (buffered < target_buffer) {
                    auto audio = decoder.decode_next_audio_samples(2048);
                    if (audio && !audio->samples.empty()) {
                        Audio::AudioEngine::instance().device().submit_stream_samples(
                            audio->samples.data(), audio->samples.size());
                        did_work = true;
                    }
                }
            }

            // 2. Video Pre-fetch
            if (decoder.metadata().has_video()) {
                size_t q_size = 0;
                {
                    std::lock_guard<std::mutex> lock(player_mutex);
                    q_size = video_frame_queue.size();
                }

                if (q_size < 32) {
                    auto frame = decoder.decode_next_video_frame();
                    if (frame) {
                        std::lock_guard<std::mutex> lock(player_mutex);
                        video_frame_queue.push_back(std::move(*frame));
                        did_work = true;
                    }
                }
            }

            if (!did_work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        }
    }

    double get_current_position_unlocked() const {
        if (state == Zenvra::Media::PlaybackState::Playing) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - play_start_time).count() * playback_rate;
            double cur = base_position + elapsed;
            double dur = decoder.metadata().duration_seconds;
            if (dur > 0.0 && cur > dur) cur = dur;
            return cur;
        }
        return base_position;
    }
};

FFmpegPlayer::FFmpegPlayer() : m_impl(std::make_unique<Impl>()) {
    m_impl->owner = this;
}

FFmpegPlayer::~FFmpegPlayer() {
    close();
}

bool FFmpegPlayer::open(const Zenvra::Media::MediaSource& source) {
    close();

    Audio::AudioEngine::instance().init();
    const auto& audio_spec = Audio::AudioEngine::instance().device().spec();
    m_impl->decoder.set_target_audio_format(audio_spec.sample_rate, audio_spec.channels);

    bool ok = m_impl->decoder.open(source);
    if (ok) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->state = Zenvra::Media::PlaybackState::Stopped;
        m_impl->base_position = 0.0;
        m_impl->error_msg.clear();
        Audio::AudioEngine::instance().device().set_stream_volume(m_impl->volume);
        Audio::AudioEngine::instance().device().set_stream_paused(true);

        // Pre-decode first frame for immediate preview display
        m_impl->latest_rendered_frame = m_impl->decoder.decode_next_video_frame();
        m_impl->start_worker();

        notify_state_changed(m_impl->state);
    } else {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->state = Zenvra::Media::PlaybackState::Error;
        m_impl->error_msg = "Failed to open media source with FFmpeg driver.";
        if (!m_impl->decoder.last_error().empty()) {
            m_impl->error_msg += " (" + m_impl->decoder.last_error() + ")";
        }
        notify_error(m_impl->error_msg);
        notify_state_changed(m_impl->state);
    }
    return ok;
}

void FFmpegPlayer::play() {
    if (!is_open()) return;
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);

    // If reached or at the end of the media, auto-rewind to start (0.0s) for replay
    double dur = m_impl->decoder.metadata().duration_seconds;
    if (dur > 0.0 && m_impl->base_position >= dur - 0.08) {
        m_impl->base_position = 0.0;
        Audio::AudioEngine::instance().device().clear_stream_samples();
        m_impl->video_frame_queue.clear();
        m_impl->decoder.seek(0.0, Zenvra::Media::SeekMode::Exact);
    }

    if (m_impl->state != Zenvra::Media::PlaybackState::Playing) {
        m_impl->play_start_time = std::chrono::steady_clock::now();
        m_impl->state = Zenvra::Media::PlaybackState::Playing;
        Audio::AudioEngine::instance().device().reset_stream_clock(m_impl->base_position);
        Audio::AudioEngine::instance().device().set_stream_paused(false);
        m_impl->worker_cv.notify_one();
        notify_state_changed(m_impl->state);
    }
}

void FFmpegPlayer::pause() {
    if (!is_open()) return;
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    if (m_impl->state == Zenvra::Media::PlaybackState::Playing) {
        m_impl->base_position = m_impl->get_current_position_unlocked();
        m_impl->state = Zenvra::Media::PlaybackState::Paused;
        Audio::AudioEngine::instance().device().set_stream_paused(true);
        m_impl->worker_cv.notify_one();
        notify_state_changed(m_impl->state);
    }
}

void FFmpegPlayer::stop() {
    if (!is_open()) return;
    seek(0.0, Zenvra::Media::SeekMode::FastKeyframe);
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    m_impl->state = Zenvra::Media::PlaybackState::Stopped;
    Audio::AudioEngine::instance().device().set_stream_paused(true);
    Audio::AudioEngine::instance().device().clear_stream_samples();
    m_impl->worker_cv.notify_one();
    notify_state_changed(m_impl->state);
}

bool FFmpegPlayer::seek(double timestamp_seconds, Zenvra::Media::SeekMode mode) {
    if (!is_open()) return false;

    if (mode == Zenvra::Media::SeekMode::FastKeyframe) {
        // Fast asynchronous seek coalescing: guarantees 0ms UI blocking during aggressive scrubbing
        m_impl->seek_target_seconds.store(timestamp_seconds, std::memory_order_release);
        m_impl->seek_target_mode.store(mode, std::memory_order_release);
        m_impl->seek_requested.store(true, std::memory_order_release);
        m_impl->worker_cv.notify_one();
        return true;
    }

    // Exact seek: synchronous precision when mouse is released or jumping to precise timestamp
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    m_impl->seek_requested.store(false, std::memory_order_release);
    Audio::AudioEngine::instance().device().clear_stream_samples();
    Audio::AudioEngine::instance().device().reset_stream_clock(timestamp_seconds);
    m_impl->video_frame_queue.clear();

    bool ok = m_impl->decoder.seek(timestamp_seconds, mode);
    if (ok) {
        m_impl->base_position = timestamp_seconds;
        m_impl->play_start_time = std::chrono::steady_clock::now();

        // Fetch fresh preview frame after seek
        auto fresh_frame = m_impl->decoder.decode_next_video_frame(false);
        if (fresh_frame) {
            m_impl->latest_rendered_frame = std::move(fresh_frame);
        }
    }
    return ok;
}

void FFmpegPlayer::close() {
    if (m_impl) {
        m_impl->stop_worker();
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        Audio::AudioEngine::instance().device().set_stream_paused(true);
        Audio::AudioEngine::instance().device().clear_stream_samples();
        m_impl->video_frame_queue.clear();
        m_impl->latest_rendered_frame.reset();
        m_impl->decoder.close();
        m_impl->state = Zenvra::Media::PlaybackState::Stopped;
        m_impl->base_position = 0.0;
        m_impl->error_msg.clear();
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
    if (!m_impl) return 0.0;
    if (m_impl->seek_requested.load(std::memory_order_relaxed)) {
        return m_impl->seek_target_seconds.load(std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    return m_impl->get_current_position_unlocked();
}

Zenvra::Media::MediaBackendType FFmpegPlayer::backend_type() const noexcept {
    return Zenvra::Media::MediaBackendType::FFmpeg;
}

std::string_view FFmpegPlayer::backend_name() const noexcept {
    return "FFmpeg";
}

std::string FFmpegPlayer::last_error() const {
    if (!m_impl) return "";
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    return m_impl->error_msg.empty() ? m_impl->decoder.last_error() : m_impl->error_msg;
}

bool FFmpegPlayer::select_video_track(int track_index) {
    if (!is_open()) return false;
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    m_impl->video_frame_queue.clear();
    bool ok = m_impl->decoder.select_video_track(track_index);
    if (ok) {
        notify_track_changed(m_impl->decoder.metadata().active_video_track,
                             m_impl->decoder.metadata().active_audio_track);
    }
    return ok;
}

bool FFmpegPlayer::select_audio_track(int track_index) {
    if (!is_open()) return false;
    std::lock_guard<std::mutex> lock(m_impl->player_mutex);
    Audio::AudioEngine::instance().device().clear_stream_samples();
    bool ok = m_impl->decoder.select_audio_track(track_index);
    if (ok) {
        notify_track_changed(m_impl->decoder.metadata().active_video_track,
                             m_impl->decoder.metadata().active_audio_track);
    }
    return ok;
}

bool FFmpegPlayer::select_subtitle_track(int track_index) {
    (void)track_index;
    return false;
}

void FFmpegPlayer::set_target_video_format(Zenvra::Media::VideoPixelFormat format) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->decoder.set_target_video_format(format);
        m_impl->video_frame_queue.clear();
    }
}

Zenvra::Media::VideoPixelFormat FFmpegPlayer::target_video_format() const noexcept {
    return m_impl ? m_impl->decoder.target_video_format() : Zenvra::Media::VideoPixelFormat::RGBA32;
}

void FFmpegPlayer::set_target_video_size(int width, int height) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->decoder.set_target_video_size(width, height);
    }
}

std::pair<int, int> FFmpegPlayer::target_video_size() const noexcept {
    return m_impl ? m_impl->decoder.target_video_size() : std::pair<int, int>{0, 0};
}

void FFmpegPlayer::set_debanding(bool enabled) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->decoder.set_deband_enabled(enabled);
    }
}

bool FFmpegPlayer::is_debanding_enabled() const noexcept {
    return m_impl ? m_impl->decoder.is_deband_enabled() : false;
}

void FFmpegPlayer::set_edge_aa(bool enabled) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->decoder.set_edge_aa_enabled(enabled);
    }
}

bool FFmpegPlayer::is_edge_aa_enabled() const noexcept {
    return m_impl ? m_impl->decoder.is_edge_aa_enabled() : false;
}

std::optional<Zenvra::Media::VideoFrame> FFmpegPlayer::get_next_video_frame() {
    if (!is_open() || !m_impl) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_impl->player_mutex);

    if (m_impl->state != Zenvra::Media::PlaybackState::Playing) {
        return m_impl->latest_rendered_frame;
    }

    double current_clock = m_impl->get_current_position_unlocked();
    double dur = m_impl->decoder.metadata().duration_seconds;
    if (dur > 0.0 && current_clock >= dur) {
        m_impl->state = Zenvra::Media::PlaybackState::Stopped;
        m_impl->base_position = dur;
        Audio::AudioEngine::instance().device().set_stream_paused(true);
        notify_state_changed(m_impl->state);
        return m_impl->latest_rendered_frame;
    }

    while (!m_impl->video_frame_queue.empty()) {
        auto& front = m_impl->video_frame_queue.front();

        // Adaptive frame pacing window: tailored to video cadence (e.g. 16.6ms for 60fps, 41.6ms for 24fps)
        const double frame_dur = (front.duration_seconds > 0.005 && front.duration_seconds < 0.2)
                                     ? front.duration_seconds : 0.033;
        const double hold_threshold = std::max(0.008, frame_dur * 0.50);
        const double drop_threshold = std::max(0.060, frame_dur * 2.50);

        // 1. Next frame is in the future: hold current frame until clock catches up
        if (front.timestamp_seconds > current_clock + hold_threshold) {
            return m_impl->latest_rendered_frame;
        }

        // 2. Late frame dropping: frame is behind current clock: drop it if queue has newer frames
        if (front.timestamp_seconds < current_clock - drop_threshold && m_impl->video_frame_queue.size() > 1) {
            m_impl->video_frame_queue.pop_front();
            continue;
        }

        // 3. Frame is on time for presentation at normal 1x speed
        m_impl->latest_rendered_frame = std::move(front);
        m_impl->video_frame_queue.pop_front();
        return m_impl->latest_rendered_frame;
    }

    return m_impl->latest_rendered_frame;
}

std::optional<Zenvra::Media::AudioBuffer> FFmpegPlayer::get_audio_samples(int max_samples) {
    if (!is_open()) {
        return std::nullopt;
    }
    return m_impl->decoder.decode_next_audio_samples(max_samples);
}

void FFmpegPlayer::set_volume(float volume) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
        Audio::AudioEngine::instance().device().set_stream_volume(m_impl->volume);
    }
}

void FFmpegPlayer::set_looping(bool loop) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        m_impl->is_looping = loop;
    }
}

void FFmpegPlayer::set_playback_rate(float rate) {
    if (m_impl) {
        std::lock_guard<std::mutex> lock(m_impl->player_mutex);
        if (m_impl->state == Zenvra::Media::PlaybackState::Playing) {
            m_impl->base_position = m_impl->get_current_position_unlocked();
            m_impl->play_start_time = std::chrono::steady_clock::now();
        }
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
