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
