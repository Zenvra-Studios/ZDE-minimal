#include <gtest/gtest.h>
#include "Media/MediaTypes.hpp"
#include "Media/MediaSource.hpp"
#include "Media/MediaPlayer.hpp"
#include "Media/MediaFactory.hpp"
#include "Drivers/Media/FFmpegDecoder.hpp"
#include "Drivers/Media/FFmpegPlayer.hpp"
#include "Drivers/Audio/AudioClip.h"
#include "Drivers/Audio/AudioDevice.h"
#include "Drivers/Audio/AudioEngine.h"
#include "UI/Editor/MediaPlayerView.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace Zenvra::Media;
using namespace Zenvra::Drivers::Media;
using namespace Zenvra::Drivers::Audio;
using namespace Zenvra::UI::Editor;

TEST(MediaTests, MediaSourceCreation) {
    auto file_src = MediaSource::from_file("Assets/sounds/click.wav");
    EXPECT_EQ(file_src.type(), MediaSourceType::FilePath);
    EXPECT_EQ(file_src.path(), "Assets/sounds/click.wav");
    EXPECT_TRUE(file_src.is_valid());

    auto empty_file = MediaSource::from_file("");
    EXPECT_FALSE(empty_file.is_valid());

    auto url_src = MediaSource::from_url("https://example.com/stream.mp4");
    EXPECT_EQ(url_src.type(), MediaSourceType::StreamUrl);
    EXPECT_EQ(url_src.uri(), "https://example.com/stream.mp4");
    EXPECT_TRUE(url_src.is_valid());

    auto empty_url = MediaSource::from_url("");
    EXPECT_FALSE(empty_url.is_valid());

    std::vector<uint8_t> dummy_bytes = {0x00, 0x01, 0x02};
    auto mem_src = MediaSource::from_memory(dummy_bytes);
    EXPECT_EQ(mem_src.type(), MediaSourceType::Memory);
    EXPECT_EQ(mem_src.memory_buffer().size(), 3);
    EXPECT_TRUE(mem_src.is_valid());

    auto empty_mem = MediaSource::from_memory({});
    EXPECT_FALSE(empty_mem.is_valid());
}

TEST(MediaTests, MediaFactoryRegistrationAndCreation) {
    FFmpegPlayer::register_backend();
    EXPECT_TRUE(MediaFactory::is_backend_available(MediaBackendType::FFmpeg));

    auto player = MediaFactory::create_player(MediaBackendType::FFmpeg);
    ASSERT_NE(player, nullptr);
    EXPECT_EQ(player->backend_type(), MediaBackendType::FFmpeg);
    EXPECT_EQ(player->backend_name(), "FFmpeg");
    EXPECT_EQ(player->state(), PlaybackState::Stopped);
    EXPECT_FALSE(player->is_open());
}

TEST(MediaTests, FFmpegPlayerLifecycleAndState) {
    FFmpegPlayer player;
    EXPECT_FALSE(player.is_open());
    EXPECT_EQ(player.state(), PlaybackState::Stopped);

    PlaybackState state_observed = PlaybackState::Stopped;
    player.on_state_changed([&](PlaybackState s) {
        state_observed = s;
    });

    auto invalid_source = MediaSource::from_file("non_existent_file.mp4");
    bool opened = player.open(invalid_source);
    EXPECT_FALSE(opened);
    EXPECT_EQ(player.state(), PlaybackState::Error);
    EXPECT_EQ(state_observed, PlaybackState::Error);
    EXPECT_FALSE(player.last_error().empty());

    player.set_volume(0.5f);
    player.set_looping(true);
    player.set_playback_rate(1.5f);

    player.close();
    EXPECT_FALSE(player.is_open());
}

TEST(MediaTests, VideoPixelFormatConfiguration) {
    FFmpegPlayer player;
#ifdef _WIN32
    EXPECT_EQ(player.target_video_format(), VideoPixelFormat::BGRA32);
#else
    EXPECT_EQ(player.target_video_format(), VideoPixelFormat::RGBA32);
#endif

    player.set_target_video_format(VideoPixelFormat::BGRA32);
    EXPECT_EQ(player.target_video_format(), VideoPixelFormat::BGRA32);

    player.set_target_video_format(VideoPixelFormat::RGB24);
    EXPECT_EQ(player.target_video_format(), VideoPixelFormat::RGB24);

    player.set_target_video_format(VideoPixelFormat::RGBA64_LE);
    EXPECT_EQ(player.target_video_format(), VideoPixelFormat::RGBA64_LE);
}

TEST(MediaTests, TrackSelectionAndSeekModes) {
    FFmpegPlayer player;
    EXPECT_FALSE(player.select_video_track(0));
    EXPECT_FALSE(player.select_audio_track(0));
    EXPECT_FALSE(player.select_subtitle_track(0));

    EXPECT_FALSE(player.seek(5.0, SeekMode::FastKeyframe));
    EXPECT_FALSE(player.seek(10.0, SeekMode::Exact));
}

TEST(MediaTests, MetadataTrackStructures) {
    MediaMetadata meta;
    EXPECT_FALSE(meta.has_video());
    EXPECT_FALSE(meta.has_audio());
    EXPECT_FALSE(meta.has_subtitles());
    EXPECT_EQ(meta.primary_width(), 0);
    EXPECT_EQ(meta.primary_height(), 0);
    EXPECT_DOUBLE_EQ(meta.primary_fps(), 0.0);

    VideoStreamTrackInfo v1;
    v1.index = 0;
    v1.codec_name = "hevc";
    v1.codec_long_name = "H.265 / HEVC";
    v1.width = 3840;
    v1.height = 2160;
    v1.fps = 60.0;
    v1.is_hdr = true;
    v1.color_space = ColorSpace::BT2020_HDR;
    v1.rotation_degrees = 90;
    meta.video_tracks.push_back(v1);
    meta.active_video_track = 0;

    EXPECT_TRUE(meta.has_video());
    EXPECT_EQ(meta.primary_width(), 3840);
    EXPECT_EQ(meta.primary_height(), 2160);
    EXPECT_DOUBLE_EQ(meta.primary_fps(), 60.0);
    EXPECT_TRUE(meta.video_tracks[0].is_hdr);
    EXPECT_EQ(meta.video_tracks[0].rotation_degrees, 90);
}

TEST(MediaTests, FFmpegDecoderEmptyOperations) {
    FFmpegDecoder decoder;
    EXPECT_FALSE(decoder.is_open());
    EXPECT_FALSE(decoder.metadata().has_video());
    EXPECT_FALSE(decoder.metadata().has_audio());

    decoder.set_target_video_format(VideoPixelFormat::BGRA32);
    EXPECT_EQ(decoder.target_video_format(), VideoPixelFormat::BGRA32);

    decoder.set_target_audio_format(48000, 2);
    EXPECT_EQ(decoder.target_sample_rate(), 48000);
    EXPECT_EQ(decoder.target_channels(), 2);

    auto v_frame = decoder.decode_next_video_frame();
    EXPECT_FALSE(v_frame.has_value());

    auto a_buf = decoder.decode_next_audio_samples();
    EXPECT_FALSE(a_buf.has_value());

    EXPECT_FALSE(decoder.select_video_track(1));
    EXPECT_FALSE(decoder.select_audio_track(1));

    decoder.close();
    EXPECT_FALSE(decoder.is_open());
}

TEST(MediaTests, FFmpegDecoderDiagnosticsAndMemorySource) {
    FFmpegDecoder decoder;
    EXPECT_TRUE(decoder.last_error().empty());

    // Invalid source
    auto empty_src = MediaSource::from_file("");
    EXPECT_FALSE(decoder.open(empty_src));
    EXPECT_FALSE(decoder.last_error().empty());

    // Non-existent file
    auto non_existent = MediaSource::from_file("non_existent_media_12345.mp4");
    EXPECT_FALSE(decoder.open(non_existent));
    EXPECT_FALSE(decoder.last_error().empty());

    // Invalid memory buffer data (not valid video)
    std::vector<uint8_t> corrupted_data = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    auto mem_src = MediaSource::from_memory(corrupted_data);
    EXPECT_FALSE(decoder.open(mem_src));
    EXPECT_FALSE(decoder.last_error().empty());
}

#ifdef ZDE_HAS_FFMPEG
TEST(MediaTests, FFmpegRAIIAllocators) {
    auto pkt = make_packet();
    ASSERT_NE(pkt, nullptr);
    EXPECT_EQ(pkt->size, 0);

    auto frame = make_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->width, 0);
    EXPECT_EQ(frame->height, 0);
}

TEST(MediaTests, RealVideoFilePlaybackAndDecoding) {
    std::filesystem::path sample_path = "videos/_Oz8FQMit88.mp4";
    if (!std::filesystem::exists(sample_path)) {
        // Fallback check
        sample_path = "../videos/_Oz8FQMit88.mp4";
    }

    if (std::filesystem::exists(sample_path)) {
        FFmpegDecoder decoder;
        auto source = MediaSource::from_file(sample_path);
        ASSERT_TRUE(decoder.open(source));
        EXPECT_TRUE(decoder.is_open());

        const auto& meta = decoder.metadata();
        EXPECT_TRUE(meta.has_video());
        EXPECT_GT(meta.duration_seconds, 0.0);
        EXPECT_GT(meta.primary_width(), 0);
        EXPECT_GT(meta.primary_height(), 0);

        // Decode first video frame
        auto first_frame = decoder.decode_next_video_frame();
        ASSERT_TRUE(first_frame.has_value());
        EXPECT_GT(first_frame->width, 0);
        EXPECT_GT(first_frame->height, 0);
        EXPECT_FALSE(first_frame->data.empty());

        // Test Seek
        EXPECT_TRUE(decoder.seek(1.0, SeekMode::FastKeyframe));
        auto seek_frame = decoder.decode_next_video_frame();
        EXPECT_TRUE(seek_frame.has_value());

        decoder.close();
        EXPECT_FALSE(decoder.is_open());
    }
}

TEST(MediaTests, MediaPlayerViewEndToEnd) {
    std::filesystem::path sample_path = "videos/_Oz8FQMit88.mp4";
    if (!std::filesystem::exists(sample_path)) {
        sample_path = "../videos/_Oz8FQMit88.mp4";
    }

    MediaPlayerView view;
    EXPECT_FALSE(view.is_open());
    EXPECT_FALSE(view.has_error());

    if (std::filesystem::exists(sample_path)) {
        ASSERT_TRUE(view.open(sample_path));
        EXPECT_TRUE(view.is_open());
        EXPECT_TRUE(view.is_playing());
        EXPECT_GT(view.duration(), 0.0);
        EXPECT_FALSE(view.format_time_display().empty());
        EXPECT_FALSE(view.format_badge_text().empty());

        // Update step
        view.update();
        EXPECT_NE(view.current_frame(), nullptr);

        // Play / Pause toggle
        view.toggle_play_pause();
        EXPECT_FALSE(view.is_playing());
        view.play();
        EXPECT_TRUE(view.is_playing());

        // Volume & Mute
        view.set_volume(0.8f);
        EXPECT_FLOAT_EQ(view.volume(), 0.8f);
        view.toggle_mute();
        EXPECT_TRUE(view.is_muted());
        view.toggle_mute();
        EXPECT_FALSE(view.is_muted());

        // Seek
        view.seek(1.0);
        EXPECT_GE(view.current_time(), 0.0);

        // Audio visualizer
        float viz = view.audio_visualizer_level(5, 24);
        EXPECT_GT(viz, 0.0f);

        view.close();
        EXPECT_FALSE(view.is_open());
    }
}
#endif

TEST(AudioDriverTests, AudioClipCreationAndPlayback) {
    std::vector<float> sine_wave(44100 * 2, 0.5f); // 1 second of stereo 44.1kHz
    auto clip = std::make_shared<AudioClip>(44100, 2, sine_wave, 1.0);
    
    EXPECT_TRUE(clip->is_valid());
    EXPECT_EQ(clip->sample_rate(), 44100);
    EXPECT_EQ(clip->channels(), 2);
    EXPECT_EQ(clip->total_frames(), 44100);
    EXPECT_DOUBLE_EQ(clip->duration_seconds(), 1.0);

    AudioDevice device;
    AudioSpec spec;
    spec.sample_rate = 44100;
    spec.channels = 2;
    spec.buffer_frames = 512;
    EXPECT_TRUE(device.open(spec));
    EXPECT_TRUE(device.is_open());

    VoiceId v_id = device.play_clip(clip, 0.8f, -0.5f, false);
    EXPECT_GT(v_id, 0);
    EXPECT_EQ(device.active_voice_count(), 1);

    std::vector<float> mix_buffer(512 * 2, 0.0f);
    device.render_mix(mix_buffer, 512);

    // Verify non-zero mixed audio
    bool has_sound = false;
    for (float val : mix_buffer) {
        if (val != 0.0f) {
            has_sound = true;
            break;
        }
    }
    EXPECT_TRUE(has_sound);

    device.stop_voice(v_id);
    device.render_mix(mix_buffer, 512);
    EXPECT_EQ(device.active_voice_count(), 0);

    // Test stream sample submission & pause/resume
    std::vector<float> stream_data(256, 0.3f);
    device.submit_stream_samples(stream_data.data(), stream_data.size());
    EXPECT_EQ(device.buffered_stream_samples(), 256);

    device.set_stream_volume(0.9f);
    EXPECT_FLOAT_EQ(device.stream_volume(), 0.9f);

    device.set_stream_paused(true);
    EXPECT_TRUE(device.is_stream_paused());
    std::fill(mix_buffer.begin(), mix_buffer.end(), 0.0f);
    device.render_mix(mix_buffer, 64);
    // When paused, stream samples should NOT be consumed
    EXPECT_EQ(device.buffered_stream_samples(), 256);

    device.set_stream_paused(false);
    EXPECT_FALSE(device.is_stream_paused());
    device.render_mix(mix_buffer, 64);
    EXPECT_LT(device.buffered_stream_samples(), 256);

    device.clear_stream_samples();
    EXPECT_EQ(device.buffered_stream_samples(), 0);

    device.close();
    EXPECT_FALSE(device.is_open());
}

TEST(AudioDriverTests, AudioEngineSingletonLifecycle) {
    auto& engine = AudioEngine::instance();
    EXPECT_TRUE(engine.init());
    EXPECT_TRUE(engine.is_initialized());

    engine.set_master_volume(0.9f);
    EXPECT_FLOAT_EQ(engine.master_volume(), 0.9f);

    auto clip = std::make_shared<AudioClip>(44100, 2, std::vector<float>(1000, 0.1f), 0.02);
    VoiceId id = engine.play_clip(clip);
    EXPECT_GT(id, 0);

    engine.stop_sound(id);
    engine.stop_all();
    engine.shutdown();
    EXPECT_FALSE(engine.is_initialized());
}

TEST(MediaTests, FFmpegDecoderDebandingAndQuality) {
    FFmpegDecoder decoder;
    EXPECT_FALSE(decoder.is_deband_enabled());
    decoder.set_deband_enabled(true);
    EXPECT_TRUE(decoder.is_deband_enabled());
    decoder.set_deband_enabled(false);
    EXPECT_FALSE(decoder.is_deband_enabled());

    FFmpegPlayer player;
    EXPECT_FALSE(player.is_debanding_enabled());
    player.set_debanding(true);
    EXPECT_TRUE(player.is_debanding_enabled());
    player.set_debanding(false);
    EXPECT_FALSE(player.is_debanding_enabled());

    MediaPlayerView view;
    EXPECT_FALSE(view.is_deband_enabled());
    view.toggle_deband();
    EXPECT_TRUE(view.is_deband_enabled());
    view.toggle_deband();
    EXPECT_FALSE(view.is_deband_enabled());

    // Verify Deband algorithm on synthetic banded frame
    VideoFrame frame;
    frame.width = 64;
    frame.height = 32;
    frame.linesize = 64 * 4;
    frame.format = VideoPixelFormat::BGRA32;
    frame.data.resize(frame.linesize * frame.height);

    // Create a stepped color band (e.g. step from 100 to 116 at column 32)
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            int idx = y * frame.linesize + x * 4;
            uint8_t val = (x < 32) ? 100 : 116;
            frame.data[idx + 0] = val; // B
            frame.data[idx + 1] = val; // G
            frame.data[idx + 2] = val; // R
            frame.data[idx + 3] = 255; // A
        }
    }

    // Measure step difference before deband
    int diff_before = std::abs(static_cast<int>(frame.data[0 * frame.linesize + 31 * 4]) -
                               static_cast<int>(frame.data[0 * frame.linesize + 32 * 4]));
    EXPECT_EQ(diff_before, 16);

    // Apply Debanding filter
    FFmpegDecoder::apply_deband(frame, 12, 28);

    // Verify boundary transition was smoothed (step between adjacent pixels decreased)
    int diff_after = std::abs(static_cast<int>(frame.data[0 * frame.linesize + 31 * 4]) -
                              static_cast<int>(frame.data[0 * frame.linesize + 32 * 4]));
    EXPECT_LT(diff_after, diff_before);
}

TEST(MediaTests, VideoEdgeAntiAliasingAndSmoothness) {
    FFmpegPlayer player;
    EXPECT_FALSE(player.is_open());
    player.set_edge_aa(true);
    EXPECT_TRUE(player.is_edge_aa_enabled());
    player.set_edge_aa(false);
    EXPECT_FALSE(player.is_edge_aa_enabled());

    MediaPlayerView view;
    EXPECT_FALSE(view.is_edge_aa_enabled()); // Default false for ultra-fast zero-lag playback
    view.toggle_edge_aa();
    EXPECT_TRUE(view.is_edge_aa_enabled());
    view.toggle_edge_aa();
    EXPECT_FALSE(view.is_edge_aa_enabled());

    // Construct a synthetic 32x32 frame with a sharp high-contrast vertical edge
    VideoFrame frame;
    frame.width = 32;
    frame.height = 32;
    frame.linesize = 32 * 4;
    frame.format = VideoPixelFormat::BGRA32;
    frame.data.resize(frame.linesize * frame.height);

    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            int idx = y * frame.linesize + x * 4;
            // Left side dark (20), right side bright (220)
            uint8_t val = (x < 16) ? 20 : 220;
            frame.data[idx + 0] = val; // B
            frame.data[idx + 1] = val; // G
            frame.data[idx + 2] = val; // R
            frame.data[idx + 3] = 255; // A
        }
    }

    // Flat interior pixels must be strictly preserved
    const uint8_t interior_before = frame.data[5 * frame.linesize + 5 * 4];
    EXPECT_EQ(interior_before, 20);

    // Apply CPU edge anti-aliasing (MSAA concept)
    FFmpegDecoder::apply_edge_aa(frame, 24);

    // 1. Flat interior is 100% untouched (zero texture blur)
    const uint8_t interior_after = frame.data[5 * frame.linesize + 5 * 4];
    EXPECT_EQ(interior_after, interior_before);

    // 2. Pixels at the boundary edge (x=15, 16) have been smoothed with subpixel blending
    const uint8_t edge_left = frame.data[10 * frame.linesize + 15 * 4];
    const uint8_t edge_right = frame.data[10 * frame.linesize + 16 * 4];

    // Left edge (was 20) receives blend from right neighbor (220) -> should be > 20
    EXPECT_GT(edge_left, 20);
    // Right edge (was 220) receives blend from left neighbor (20) -> should be < 220
    EXPECT_LT(edge_right, 220);
    // Smooth transition: edge step is softer than the original 200 contrast jump
    EXPECT_LT(edge_right - edge_left, 200);

    // Verify Target Display Resampling
    player.set_target_video_size(640, 360);
    EXPECT_EQ(player.target_video_size().first, 640);
    EXPECT_EQ(player.target_video_size().second, 360);

    view.set_target_display_size(800, 450);
}

TEST(MediaTests, VolumeMuteMemoryAndHudSpacing) {
    MediaPlayerView view;
    // 1. Initial default volume
    view.set_volume(0.75f);
    EXPECT_FLOAT_EQ(view.volume(), 0.75f);
    EXPECT_FALSE(view.is_muted());

    // 2. Mute remembers original volume
    view.toggle_mute();
    EXPECT_TRUE(view.is_muted());
    EXPECT_FLOAT_EQ(view.volume(), 0.75f);

    // 3. Unmute restores volume cleanly
    view.toggle_mute();
    EXPECT_FALSE(view.is_muted());
    EXPECT_FLOAT_EQ(view.volume(), 0.75f);

    // 4. Modifying volume while muted automatically un-mutes
    view.toggle_mute(); // now muted
    EXPECT_TRUE(view.is_muted());
    view.set_volume(0.40f);
    EXPECT_FALSE(view.is_muted());
    EXPECT_FLOAT_EQ(view.volume(), 0.40f);

    // 5. Scrubber layout never collides with play button or time display
    Zenvra::UI::Rect hud{0.0F, 500.0F, 800.0F, 44.0F};
    Zenvra::UI::Rect play_btn = view.calculate_play_button_bounds(hud, 1.0F);
    Zenvra::UI::Rect scrubber = view.calculate_scrubber_bounds(hud, 1.0F);
    Zenvra::UI::Rect volume_btn = view.calculate_volume_bounds(hud, 1.0F);

    // Scrubber starts comfortably past play button and time display
    EXPECT_GT(scrubber.x, play_btn.right() + 100.0F);
    // Scrubber ends comfortably before volume button
    EXPECT_LT(scrubber.right(), volume_btn.x - 10.0F);
}

TEST(MediaTests, MediaPlayerViewClassEncapsulation) {
    // 1. Static unified media detector
    EXPECT_TRUE(MediaPlayerView::is_media_file("video.mp4"));
    EXPECT_TRUE(MediaPlayerView::is_media_file("video.MKV"));
    EXPECT_TRUE(MediaPlayerView::is_media_file("audio.mp3"));
    EXPECT_TRUE(MediaPlayerView::is_media_file("sound.WAV"));
    EXPECT_TRUE(MediaPlayerView::is_media_file("image.png"));
    EXPECT_TRUE(MediaPlayerView::is_media_file("vector.svg"));
    EXPECT_FALSE(MediaPlayerView::is_media_file("source.cpp"));
    EXPECT_FALSE(MediaPlayerView::is_media_file("document.txt"));
    EXPECT_FALSE(MediaPlayerView::is_media_file("config.json"));

    // 2. Class instance state inspectors
    MediaPlayerView view;
    EXPECT_FALSE(view.is_video());
    EXPECT_FALSE(view.is_audio());
    EXPECT_FALSE(view.is_image());

    view.open("sample.mp4");
    EXPECT_TRUE(view.is_video());
    EXPECT_FALSE(view.is_audio());
    EXPECT_FALSE(view.is_image());

    view.open("sample.wav");
    EXPECT_FALSE(view.is_video());
    EXPECT_TRUE(view.is_audio());
    EXPECT_FALSE(view.is_image());

    view.open("sample.png");
    EXPECT_FALSE(view.is_video());
    EXPECT_FALSE(view.is_audio());
    EXPECT_TRUE(view.is_image());
}

TEST(MediaTests, MediaPlayerViewFullscreenAndHUDLayout) {
    MediaPlayerView view;
    EXPECT_FALSE(view.is_fullscreen());

    view.toggle_fullscreen();
    EXPECT_TRUE(view.is_fullscreen());

    view.toggle_fullscreen();
    EXPECT_FALSE(view.is_fullscreen());

    view.set_fullscreen(true);
    EXPECT_TRUE(view.is_fullscreen());

    view.set_fullscreen(false);
    EXPECT_FALSE(view.is_fullscreen());

    // Check bounds calculation with fullscreen button
    Zenvra::UI::Rect hud{0.0F, 500.0F, 800.0F, 44.0F};
    Zenvra::UI::Rect play_btn = view.calculate_play_button_bounds(hud, 1.0F);
    Zenvra::UI::Rect scrubber = view.calculate_scrubber_bounds(hud, 1.0F);
    Zenvra::UI::Rect volume_btn = view.calculate_volume_bounds(hud, 1.0F);
    Zenvra::UI::Rect fs_btn = view.calculate_fullscreen_button_bounds(hud, 1.0F);

    // Fullscreen button is within HUD bounds on the far right
    EXPECT_GT(fs_btn.x, hud.x);
    EXPECT_LE(fs_btn.right(), hud.right());

    // Volume button is positioned before fullscreen button with margin
    EXPECT_LT(volume_btn.right(), fs_btn.x);

    // Scrubber is positioned between play button (left) and volume/fs button (right)
    EXPECT_GT(scrubber.x, play_btn.right());
    EXPECT_LT(scrubber.right(), volume_btn.x);

    // Check VLC-style true rapet fullscreen bounds (no gaps at top, bottom, left, right)
    Zenvra::UI::Rect monitor_bounds{0.0F, 0.0F, 1920.0F, 1080.0F};
    view.set_fullscreen(true);
    Zenvra::UI::Rect fs_canvas = view.calculate_video_canvas_bounds(monitor_bounds);
    EXPECT_FLOAT_EQ(fs_canvas.x, 0.0F);
    EXPECT_FLOAT_EQ(fs_canvas.y, 0.0F);
    EXPECT_FLOAT_EQ(fs_canvas.width, 1920.0F);
    EXPECT_FLOAT_EQ(fs_canvas.height, 1080.0F);
}

TEST(MediaTests, AggressiveScrubberLiveTracking) {
    MediaPlayerView view;
    std::filesystem::path test_vid = "Assets/vid/Tutorial Golang Dasar (Bahasa Indonesia) - 1699333216.mp4";
    if (std::filesystem::exists(test_vid)) {
        ASSERT_TRUE(view.open(test_vid));
        Zenvra::UI::Rect editor_bounds{0.0F, 0.0F, 1000.0F, 600.0F};
        Zenvra::UI::Rect hud = view.calculate_hud_bounds(editor_bounds, 1.0F);
        Zenvra::UI::Rect scrubber = view.calculate_scrubber_bounds(hud, 1.0F);

        // 1. Mouse down on scrubber at 25%
        float click_x = scrubber.x + scrubber.width * 0.25F;
        EXPECT_TRUE(view.handle_mouse_down(click_x, scrubber.y + 2.0F, editor_bounds, 1.0F));
        EXPECT_TRUE(view.is_scrubbing());
        EXPECT_NEAR(view.progress_ratio(), 0.25F, 0.02F);

        // 2. Aggressive drag forward to 75%
        float drag_x1 = scrubber.x + scrubber.width * 0.75F;
        EXPECT_TRUE(view.handle_mouse_move(drag_x1, scrubber.y + 2.0F, editor_bounds, 1.0F));
        EXPECT_NEAR(view.progress_ratio(), 0.75F, 0.02F);

        // 3. Aggressive drag backward to 10%
        float drag_x2 = scrubber.x + scrubber.width * 0.10F;
        EXPECT_TRUE(view.handle_mouse_move(drag_x2, scrubber.y + 2.0F, editor_bounds, 1.0F));
        EXPECT_NEAR(view.progress_ratio(), 0.10F, 0.02F);

        // 4. Release mouse
        EXPECT_TRUE(view.handle_mouse_up(drag_x2, scrubber.y + 2.0F, editor_bounds, 1.0F));
        EXPECT_FALSE(view.is_scrubbing());
        view.close();
    }
}

TEST(MediaTests, HudAutoCrossfadeAndMouseMoveWake) {
    MediaPlayerView view;
    std::filesystem::path test_vid = "Assets/vid/Tutorial Golang Dasar (Bahasa Indonesia) - 1699333216.mp4";
    if (std::filesystem::exists(test_vid)) {
        ASSERT_TRUE(view.open(test_vid));
        EXPECT_TRUE(view.is_video());
        EXPECT_TRUE(view.is_playing());

        Zenvra::UI::Rect editor_bounds{0.0F, 0.0F, 1000.0F, 600.0F};

        // 1. Initially HUD is fully visible
        EXPECT_FLOAT_EQ(view.hud_opacity(), 1.0f);
        EXPECT_FALSE(view.is_hud_faded_out());

        // 2. Ticking with recent activity stays at 1.0f
        view.set_hud_inactivity_for_testing(500);
        EXPECT_FALSE(view.tick_hud_fade(0.016f));
        EXPECT_FLOAT_EQ(view.hud_opacity(), 1.0f);

        // 3. After 2.5s (2600ms) of inactivity, tick starts crossfading down
        view.set_hud_inactivity_for_testing(2600);
        EXPECT_TRUE(view.tick_hud_fade(0.10f));
        EXPECT_LT(view.hud_opacity(), 1.0f);
        EXPECT_GT(view.hud_opacity(), 0.5f);

        // Continue ticking to completion (400ms fade-out total)
        for (int i = 0; i < 10; ++i) {
            view.tick_hud_fade(0.05f);
        }
        EXPECT_TRUE(view.is_hud_faded_out());
        EXPECT_FLOAT_EQ(view.hud_opacity(), 0.0f);

        // 4. Moving mouse OUTSIDE the video area does NOT wake HUD
        EXPECT_FALSE(view.handle_mouse_move(-10.0F, -10.0F, editor_bounds, 1.0F));
        EXPECT_TRUE(view.is_hud_faded_out());

        // 5. Moving mouse INSIDE the video area immediately wakes HUD (VLC behavior)
        EXPECT_TRUE(view.handle_mouse_move(500.0F, 300.0F, editor_bounds, 1.0F));
        // Crossfade in (~200ms)
        EXPECT_TRUE(view.tick_hud_fade(0.10f));
        EXPECT_GT(view.hud_opacity(), 0.0f);
        for (int i = 0; i < 10; ++i) {
            view.tick_hud_fade(0.05f);
        }
        EXPECT_FLOAT_EQ(view.hud_opacity(), 1.0f);
        EXPECT_FALSE(view.is_hud_faded_out());

        // 6. When video is paused, HUD never fades out even after inactivity
        view.toggle_play_pause();
        EXPECT_FALSE(view.is_playing());
        view.set_hud_inactivity_for_testing(5000);
        EXPECT_FALSE(view.tick_hud_fade(0.10f));
        EXPECT_FLOAT_EQ(view.hud_opacity(), 1.0f);

        view.close();
    }
}

