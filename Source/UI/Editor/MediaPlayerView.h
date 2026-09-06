#pragma once

#include "Media/MediaPlayer.hpp"
#include "Media/MediaTypes.hpp"
#include "UI/Geometry.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Zenvra::UI::Editor {

class MediaPlayerView {
public:
    MediaPlayerView();
    ~MediaPlayerView();

    // Unified media file check for external callers
    [[nodiscard]] static bool is_media_file(const std::filesystem::path& path);

    bool open(const std::filesystem::path& file_path);
    void close();

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::filesystem::path& current_path() const noexcept { return m_current_path; }

    // Media type inspectors for current loaded media
    [[nodiscard]] bool is_video() const noexcept { return is_video_file(m_current_path); }
    [[nodiscard]] bool is_audio() const noexcept { return is_audio_file(m_current_path); }
    [[nodiscard]] bool is_image() const noexcept { return is_image_file(m_current_path); }

    void play();
    void pause();
    void toggle_play_pause();
    [[nodiscard]] bool is_playing() const noexcept;

    void seek(double time_seconds, Zenvra::Media::SeekMode mode = Zenvra::Media::SeekMode::FastKeyframe);
    void seek_relative(double delta_seconds);

    void set_volume(float volume);
    [[nodiscard]] float volume() const noexcept;
    void toggle_mute();
    [[nodiscard]] bool is_muted() const noexcept;

    [[nodiscard]] double current_time() const noexcept;
    [[nodiscard]] double duration() const noexcept;
    [[nodiscard]] float progress_ratio() const noexcept;

    [[nodiscard]] std::string format_time_display() const;
    [[nodiscard]] std::string format_badge_text() const;

    void toggle_deband();
    void set_deband_enabled(bool enabled);
    [[nodiscard]] bool is_deband_enabled() const noexcept;
    void toggle_edge_aa();
    void set_edge_aa_enabled(bool enabled);
    [[nodiscard]] bool is_edge_aa_enabled() const noexcept;

    [[nodiscard]] bool has_error() const noexcept { return m_has_error; }
    [[nodiscard]] const std::string& error_message() const noexcept { return m_error_message; }
    [[nodiscard]] const Zenvra::Media::MediaMetadata& metadata() const noexcept;
    [[nodiscard]] float audio_visualizer_level(int bar_index, int total_bars) const noexcept;

    // Frame update & retrieval
    void update();
    [[nodiscard]] const Zenvra::Media::VideoFrame* current_frame() const noexcept;
    [[nodiscard]] int video_width() const noexcept;
    [[nodiscard]] int video_height() const noexcept;
    void set_target_display_size(int width, int height);
    [[nodiscard]] std::pair<int, int> target_display_size() const noexcept;

    void toggle_fullscreen();
    void set_fullscreen(bool fullscreen);
    [[nodiscard]] bool is_fullscreen() const noexcept { return m_is_fullscreen; }

    // UI Layout Calculations (Letterboxed video viewport & HUD overlay)
    [[nodiscard]] UI::Rect calculate_video_canvas_bounds(const UI::Rect& editor_bounds) const noexcept;
    [[nodiscard]] UI::Rect calculate_hud_bounds(const UI::Rect& editor_bounds, float dpi_scale) const noexcept;
    [[nodiscard]] UI::Rect calculate_play_button_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept;
    [[nodiscard]] UI::Rect calculate_scrubber_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept;
    [[nodiscard]] UI::Rect calculate_volume_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept;
    [[nodiscard]] UI::Rect calculate_fullscreen_button_bounds(const UI::Rect& hud_bounds, float dpi_scale) const noexcept;

    // Mouse & Keyboard Input Event Handlers
    bool handle_mouse_down(float x, float y, const UI::Rect& editor_bounds, float dpi_scale);
    bool handle_mouse_move(float x, float y, const UI::Rect& editor_bounds, float dpi_scale);
    bool handle_mouse_up(float x, float y, const UI::Rect& editor_bounds, float dpi_scale);
    bool handle_key_down(int key_code);

    [[nodiscard]] bool is_scrubbing() const noexcept { return m_is_scrubbing; }
    [[nodiscard]] bool is_dragging_volume() const noexcept { return m_is_dragging_volume; }
    [[nodiscard]] bool is_hud_visible() const noexcept { return m_hud_visible; }
    [[nodiscard]] float hud_opacity() const noexcept { return m_hud_opacity; }
    [[nodiscard]] bool is_hud_faded_out() const noexcept { return m_hud_opacity <= 0.01f; }
    bool tick_hud_fade(float delta_seconds) noexcept;
    void show_hud() noexcept;
    void set_hud_inactivity_for_testing(int milliseconds) noexcept;

    [[nodiscard]] bool is_play_hovered() const noexcept { return m_play_hovered; }
    [[nodiscard]] bool is_scrubber_hovered() const noexcept { return m_scrubber_hovered; }
    [[nodiscard]] bool is_volume_hovered() const noexcept { return m_volume_hovered; }
    [[nodiscard]] bool is_fullscreen_hovered() const noexcept { return m_fullscreen_hovered; }
    [[nodiscard]] float hover_scrub_ratio() const noexcept { return m_hover_scrub_ratio; }

private:
    [[nodiscard]] static bool is_video_file(const std::filesystem::path& path);
    [[nodiscard]] static bool is_audio_file(const std::filesystem::path& path);
    [[nodiscard]] static bool is_image_file(const std::filesystem::path& path);

    std::filesystem::path m_current_path;
    std::shared_ptr<Zenvra::Media::IMediaPlayer> m_player;
    std::optional<Zenvra::Media::VideoFrame> m_current_frame;

    float m_volume = 0.70f;
    float m_unmuted_volume = 0.70f;
    bool m_muted = false;
    bool m_deband_enabled = false;
    bool m_edge_aa_enabled = false;
    bool m_has_error = false;
    std::string m_error_message;

    bool m_is_scrubbing = false;
    bool m_is_dragging_volume = false;
    bool m_hud_visible = true;
    float m_hud_opacity = 1.0f;
    float m_target_hud_opacity = 1.0f;
    bool m_play_hovered = false;
    bool m_scrubber_hovered = false;
    bool m_volume_hovered = false;
    bool m_fullscreen_hovered = false;
    bool m_is_fullscreen = false;
    float m_hover_scrub_ratio = 0.0f;
    float m_scrub_ratio = 0.0f;

    std::chrono::steady_clock::time_point m_last_mouse_activity;
    std::chrono::steady_clock::time_point m_last_frame_time;
    std::chrono::steady_clock::time_point m_last_seek_time;
};

} // namespace Zenvra::UI::Editor
