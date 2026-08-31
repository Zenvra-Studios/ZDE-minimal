#include "MediaPlayerView.h"
#include "Media/MediaFactory.hpp"
#include "Media/MediaSource.hpp"
#include "Drivers/Media/FFmpegPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Zenvra::UI::Editor {

bool is_video_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" ||
           ext == ".avi" || ext == ".flv" || ext == ".wmv" || ext == ".ts" ||
           ext == ".m4v" || ext == ".ogv" || ext == ".3gp" || ext == ".vob" ||
           ext == ".rmvb" || ext == ".mjpg";
}

bool is_audio_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" ||
           ext == ".aac" || ext == ".m4a" || ext == ".opus" || ext == ".aiff" ||
           ext == ".wma" || ext == ".ac3" || ext == ".mid" || ext == ".midi";
}

bool is_image_file(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".gif" || ext == ".webp" || ext == ".ico" || ext == ".tiff" ||
           ext == ".tif" || ext == ".tga" || ext == ".svg" || ext == ".dds" ||
           ext == ".exr" || ext == ".hdr" || ext == ".psd";
}

namespace {

std::string format_seconds(double total_seconds) {
    if (total_seconds < 0.0) total_seconds = 0.0;
    int seconds_int = static_cast<int>(total_seconds);
    int hours = seconds_int / 3600;
    int minutes = (seconds_int % 3600) / 60;
    int seconds = seconds_int % 60;

    char buf[64];
    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    }
    return std::string(buf);
}

} // namespace

MediaPlayerView::MediaPlayerView() {
    Zenvra::Drivers::Media::FFmpegPlayer::register_backend();
}

MediaPlayerView::~MediaPlayerView() {
    close();
}

bool MediaPlayerView::open(const std::filesystem::path& file_path) {
    close();

    Zenvra::Drivers::Media::FFmpegPlayer::register_backend();
    m_player = Zenvra::Media::MediaFactory::create_player(Zenvra::Media::MediaBackendType::FFmpeg);
    if (!m_player) {
        return false;
    }

    m_player->set_target_video_format(Zenvra::Media::VideoPixelFormat::BGRA32);
    auto source = Zenvra::Media::MediaSource::from_file(file_path);
    if (!m_player->open(source)) {
        m_player.reset();
        return false;
    }

    m_current_path = file_path;
    m_volume = 1.0f;
    m_muted = false;
    m_player->set_volume(m_volume);
    m_player->play();

    // Pull first frame
    m_current_frame = m_player->get_next_video_frame();
    m_last_mouse_activity = std::chrono::steady_clock::now();
    m_last_frame_time = std::chrono::steady_clock::now();
    return true;
}

void MediaPlayerView::close() {
    if (m_player) {
        m_player->stop();
        m_player->close();
        m_player.reset();
    }
    m_current_frame.reset();
    m_current_path.clear();
    m_is_scrubbing = false;
}

bool MediaPlayerView::is_open() const noexcept {
    return m_player && m_player->is_open();
}

void MediaPlayerView::play() {
    if (m_player) {
        m_player->play();
    }
}

void MediaPlayerView::pause() {
    if (m_player) {
        m_player->pause();
    }
}

void MediaPlayerView::toggle_play_pause() {
    if (!m_player) return;
    if (is_playing()) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

bool MediaPlayerView::is_playing() const noexcept {
    return m_player && m_player->state() == Zenvra::Media::PlaybackState::Playing;
}

void MediaPlayerView::seek(double time_seconds) {
    if (m_player) {
        m_player->seek(time_seconds, Zenvra::Media::SeekMode::Exact);
        m_current_frame = m_player->get_next_video_frame();
    }
}

void MediaPlayerView::seek_relative(double delta_seconds) {
    if (m_player) {
        double target = std::clamp(current_time() + delta_seconds, 0.0, duration());
        seek(target);
    }
}

void MediaPlayerView::set_volume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_muted && m_volume > 0.0f) {
        m_muted = false;
    }
    if (m_player) {
        m_player->set_volume(m_muted ? 0.0f : m_volume);
    }
}

float MediaPlayerView::volume() const noexcept {
    return m_volume;
}

void MediaPlayerView::toggle_mute() {
    m_muted = !m_muted;
    if (m_player) {
        m_player->set_volume(m_muted ? 0.0f : m_volume);
    }
}

bool MediaPlayerView::is_muted() const noexcept {
    return m_muted;
}

double MediaPlayerView::current_time() const noexcept {
    return m_player ? m_player->position() : 0.0;
}

double MediaPlayerView::duration() const noexcept {
    return m_player ? m_player->duration() : 0.0;
}

float MediaPlayerView::progress_ratio() const noexcept {
    double dur = duration();
    if (dur <= 0.0) return 0.0f;
    return static_cast<float>(std::clamp(current_time() / dur, 0.0, 1.0));
}

std::string MediaPlayerView::format_time_display() const {
    return format_seconds(current_time()) + " / " + format_seconds(duration());
}

std::string MediaPlayerView::format_badge_text() const {
    if (!m_player) return "";
    const auto& meta = m_player->metadata();
    std::string text;
    if (meta.has_video()) {
        const auto& vt = meta.video_tracks[meta.active_video_track];
        text += std::to_string(vt.width) + "x" + std::to_string(vt.height);
        if (vt.fps > 0.0) {
            char fps_buf[16];
            std::snprintf(fps_buf, sizeof(fps_buf), " %.0ffps", vt.fps);
            text += fps_buf;
        }
        if (!vt.codec_name.empty()) {
            text += " (" + vt.codec_name + ")";
        }
    }
    return text;
}

void MediaPlayerView::update() {
    if (!m_player || !is_playing()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    // Advance video frame
    if (auto frame = m_player->get_next_video_frame()) {
        m_current_frame = std::move(frame);
    }
    m_last_frame_time = now;
}

const Zenvra::Media::VideoFrame* MediaPlayerView::current_frame() const noexcept {
    return m_current_frame ? &(*m_current_frame) : nullptr;
}

int MediaPlayerView::video_width() const noexcept {
    if (m_current_frame && m_current_frame->width > 0) {
        return m_current_frame->width;
    }
    if (m_player && m_player->metadata().has_video()) {
        return m_player->metadata().primary_width();
    }
    return 1920;
}

int MediaPlayerView::video_height() const noexcept {
    if (m_current_frame && m_current_frame->height > 0) {
        return m_current_frame->height;
    }
    if (m_player && m_player->metadata().has_video()) {
        return m_player->metadata().primary_height();
    }
    return 1080;
}

UI::Rect MediaPlayerView::calculate_video_canvas_bounds(const UI::Rect& editor_bounds) const noexcept {
    if (editor_bounds.width <= 0.0F || editor_bounds.height <= 0.0F) {
        return editor_bounds;
    }

    const float src_w = static_cast<float>(video_width());
    const float src_h = static_cast<float>(video_height());
    const float aspect = (src_h > 0.0F) ? (src_w / src_h) : (16.0F / 9.0F);

    float target_w = editor_bounds.width;
    float target_h = target_w / aspect;

    if (target_h > editor_bounds.height) {
        target_h = editor_bounds.height;
        target_w = target_h * aspect;
    }

    const float offset_x = editor_bounds.x + (editor_bounds.width - target_w) * 0.5F;
    const float offset_y = editor_bounds.y + (editor_bounds.height - target_h) * 0.5F;

    return UI::Rect{offset_x, offset_y, target_w, target_h};
}

UI::Rect MediaPlayerView::calculate_hud_bounds(const UI::Rect& editor_bounds, float dpi_scale) const noexcept {
    const float hud_h = 44.0F * dpi_scale;
    const UI::Rect canvas = calculate_video_canvas_bounds(editor_bounds);
    const float hud_bottom = std::min(editor_bounds.bottom(), canvas.bottom());
    const float hud_w = std::min(editor_bounds.width, canvas.width);
    const float hud_x = canvas.x + (canvas.width - hud_w) * 0.5F;
    return UI::Rect{hud_x, hud_bottom - hud_h, hud_w, hud_h};
}

UI::Rect MediaPlayerView::calculate_play_button_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept {
    const float btn_sz = 28.0F * dpi_scale;
    const float btn_x = hud_bounds.x + 10.0F * dpi_scale;
    const float btn_y = hud_bounds.y + (hud_bounds.height - btn_sz) * 0.5F;
    return UI::Rect{btn_x, btn_y, btn_sz, btn_sz};
}

UI::Rect MediaPlayerView::calculate_scrubber_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept {
    const float play_w = 38.0F * dpi_scale;
    const float time_w = (hud_bounds.width < 340.0F * dpi_scale) ? 0.0F : (90.0F * dpi_scale);
    const float left_offset = play_w + time_w + 10.0F * dpi_scale;
    const float vol_w = (hud_bounds.width < 280.0F * dpi_scale) ? (30.0F * dpi_scale) : (80.0F * dpi_scale);
    const float right_offset = vol_w + 16.0F * dpi_scale;
    const float track_x = hud_bounds.x + left_offset;
    const float track_w = std::max(10.0F, hud_bounds.width - left_offset - right_offset);
    const float track_h = 24.0F * dpi_scale;
    const float track_y = hud_bounds.y + (hud_bounds.height - track_h) * 0.5F;
    return UI::Rect{track_x, track_y, track_w, track_h};
}

UI::Rect MediaPlayerView::calculate_volume_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept {
    const float vol_w = (hud_bounds.width < 280.0F * dpi_scale) ? (26.0F * dpi_scale) : (75.0F * dpi_scale);
    const float vol_h = 22.0F * dpi_scale;
    const float vol_x = hud_bounds.right() - vol_w - 10.0F * dpi_scale;
    const float vol_y = hud_bounds.y + (hud_bounds.height - vol_h) * 0.5F;
    return UI::Rect{vol_x, vol_y, vol_w, vol_h};
}

bool MediaPlayerView::handle_mouse_down(float x, float y, const UI::Rect& editor_bounds, float dpi_scale) {
    if (!is_open()) return false;
    m_last_mouse_activity = std::chrono::steady_clock::now();

    const UI::Rect hud = calculate_hud_bounds(editor_bounds, dpi_scale);
    const UI::Rect play_btn = calculate_play_button_bounds(hud, dpi_scale);
    const UI::Rect scrubber = calculate_scrubber_bounds(hud, dpi_scale);
    const UI::Rect volume_btn = calculate_volume_bounds(hud, dpi_scale);

    if (play_btn.contains(x, y)) {
        toggle_play_pause();
        return true;
    }

    if (scrubber.contains(x, y)) {
        m_is_scrubbing = true;
        float ratio = std::clamp((x - scrubber.x) / scrubber.width, 0.0F, 1.0F);
        seek(ratio * duration());
        return true;
    }

    if (volume_btn.contains(x, y)) {
        float ratio = std::clamp((x - volume_btn.x) / volume_btn.width, 0.0F, 1.0F);
        set_volume(ratio);
        return true;
    }

    const UI::Rect canvas = calculate_video_canvas_bounds(editor_bounds);
    if (canvas.contains(x, y)) {
        toggle_play_pause();
        return true;
    }

    return false;
}

bool MediaPlayerView::handle_mouse_move(float x, float y, const UI::Rect& editor_bounds, float dpi_scale) {
    if (!is_open()) return false;
    m_last_mouse_activity = std::chrono::steady_clock::now();

    const UI::Rect hud = calculate_hud_bounds(editor_bounds, dpi_scale);
    const UI::Rect play_btn = calculate_play_button_bounds(hud, dpi_scale);
    const UI::Rect scrubber = calculate_scrubber_bounds(hud, dpi_scale);
    const UI::Rect volume_btn = calculate_volume_bounds(hud, dpi_scale);

    m_play_hovered = play_btn.contains(x, y);
    m_scrubber_hovered = scrubber.contains(x, y);
    m_volume_hovered = volume_btn.contains(x, y);

    if (m_scrubber_hovered && scrubber.width > 0.0F) {
        m_hover_scrub_ratio = std::clamp((x - scrubber.x) / scrubber.width, 0.0F, 1.0F);
    }

    if (m_is_scrubbing && scrubber.width > 0.0F) {
        float ratio = std::clamp((x - scrubber.x) / scrubber.width, 0.0F, 1.0F);
        seek(ratio * duration());
        return true;
    }

    return m_play_hovered || m_scrubber_hovered || m_volume_hovered;
}

bool MediaPlayerView::handle_mouse_up(float x, float y, const UI::Rect& editor_bounds, float dpi_scale) {
    (void)x; (void)y; (void)editor_bounds; (void)dpi_scale;
    if (m_is_scrubbing) {
        m_is_scrubbing = false;
        return true;
    }
    return false;
}

bool MediaPlayerView::handle_key_down(int key_code) {
    if (!is_open()) return false;

    // VK_SPACE (0x20)
    if (key_code == 0x20) {
        toggle_play_pause();
        return true;
    }
    // VK_LEFT (0x25) -> Seek -5s
    if (key_code == 0x25) {
        seek_relative(-5.0);
        return true;
    }
    // VK_RIGHT (0x27) -> Seek +5s
    if (key_code == 0x27) {
        seek_relative(5.0);
        return true;
    }
    // VK_UP (0x26) -> Volume +10%
    if (key_code == 0x26) {
        set_volume(m_volume + 0.1f);
        return true;
    }
    // VK_DOWN (0x28) -> Volume -10%
    if (key_code == 0x28) {
        set_volume(m_volume - 0.1f);
        return true;
    }
    // 'M' (0x4D) -> Toggle Mute
    if (key_code == 0x4D) {
        toggle_mute();
        return true;
    }

    return false;
}

} // namespace Zenvra::UI::Editor
