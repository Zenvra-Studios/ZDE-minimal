#include <gtest/gtest.h>

#include "Services/Shader/ShaderCompiler.h"
#include "Services/Shader/ShaderService.h"
#include "Services/Shader/VirtualSurfaceBuffer.h"

#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

using namespace Zenvra::Services::Shader;

TEST(ShaderServiceTests, VirtualSurfaceBufferAllocationAndMapping)
{
    VirtualSurfaceBuffer buffer;
    buffer.resize(64, 48);

    SurfaceDescriptor desc{
        .width = 64,
        .height = 48,
        .stride = 64 * 4,
        .frame_index = 1,
        .timestamp_sec = 0.5F,
        .frame_time_ms = 16.0F,
        .fps = 60.0F,
        .backend = RenderBackend::Cpu,
        .status = ShaderStatus::Running,
    };

    std::vector<std::uint32_t> test_pixels(64 * 48, 0xFF112233U);
    buffer.post_frame(test_pixels, desc);

    EXPECT_TRUE(buffer.has_new_frame());

    auto lock = buffer.acquire_mapped_surface();
    EXPECT_TRUE(lock.is_valid());
    EXPECT_EQ(lock.get_width(), 64);
    EXPECT_EQ(lock.get_height(), 48);
    EXPECT_EQ(lock.get_pixels().size(), 64 * 48);
    EXPECT_EQ(lock.get_pixels()[0], 0xFF112233U);
    EXPECT_EQ(lock.get_descriptor().frame_index, 1ULL);
    EXPECT_EQ(lock.get_descriptor().status, ShaderStatus::Running);

    buffer.mark_frame_consumed();
    EXPECT_FALSE(buffer.has_new_frame());
}

TEST(ShaderServiceTests, ShaderServiceLifecycleAndOffscreenStep)
{
    ShaderService service;
    service.initialize(64, 48);

    EXPECT_EQ(service.get_surface_descriptor().width, 64);
    EXPECT_EQ(service.get_surface_descriptor().height, 48);

    // Initial frame stepped during initialize
    {
        auto lock = service.acquire_mapped_surface();
        EXPECT_TRUE(lock.is_valid());
        EXPECT_EQ(lock.get_width(), 64);
        EXPECT_EQ(lock.get_height(), 48);
        EXPECT_FALSE(lock.get_pixels().empty());
    }

    // Step frame offscreen
    std::this_thread::sleep_for(std::chrono::milliseconds(6));
    const bool stepped = service.step_frame();
    EXPECT_TRUE(stepped);
    EXPECT_GE(service.get_time(), 0.0F);
}

TEST(ShaderServiceTests, ShaderServiceCandidateDetection)
{
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("void mainImage() {}", ".glsl"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("", ".frag"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("", ".vert"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("", ".comp"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("", ".shader"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("void mainImage(out vec4 c, in vec2 f) {}", ".txt"));
    EXPECT_TRUE(ShaderService::is_shader_source_candidate("#version 330 core\nvoid main() {}", ".cpp"));
    EXPECT_FALSE(ShaderService::is_shader_source_candidate("#include <iostream>\nint main() { return 0; }", ".cpp"));
}

TEST(ShaderServiceTests, DynamicShaderCompilationAndPlayback)
{
    ShaderService service;
    service.initialize(32, 24);

    const std::string custom_shader = R"(
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord.xy / iResolution.xy;
    fragColor = vec4(uv.x, uv.y, 0.5, 1.0);
}
)";

    service.set_shader_source(custom_shader);
    const bool ok = service.compile_and_render();
    EXPECT_TRUE(ok);
    EXPECT_EQ(service.get_status(), ShaderStatus::Running);

    {
        auto lock = service.acquire_mapped_surface();
        EXPECT_TRUE(lock.is_valid());
        EXPECT_EQ(lock.get_width(), 32);
        EXPECT_EQ(lock.get_height(), 24);
    }

    // Test playback pause and play
    service.pause();
    EXPECT_EQ(service.get_status(), ShaderStatus::Paused);
    service.play();
    EXPECT_EQ(service.get_status(), ShaderStatus::Running);

    // Test resolution scale
    service.set_resolution_scale(ResolutionScale::Half);
    EXPECT_EQ(service.get_resolution_scale(), ResolutionScale::Half);
    service.cycle_resolution_scale();
    EXPECT_EQ(service.get_resolution_scale(), ResolutionScale::Quarter);
}

TEST(ShaderServiceTests, FrameListenerCallback)
{
    ShaderService service;
    service.initialize(32, 24);

    bool listener_called = false;
    std::uint64_t received_frame = 0;

    service.register_frame_listener([&](const SurfaceDescriptor& desc) {
        listener_called = true;
        received_frame = desc.frame_index;
    });

    service.step_frame();

    EXPECT_TRUE(listener_called);
}
