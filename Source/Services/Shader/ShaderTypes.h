#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Zenvra::Services::Shader
{

enum class ShaderFormat
{
    Shadertoy,       // void mainImage(out vec4 fragColor, in vec2 fragCoord)
    StandardGLSL,    // void main() { gl_FragColor / fragColor = ... }
    RaymarchingSDF,  // Raymarching SDF template
    ComputeMath,     // Generative math canvas
};

enum class ShaderStatus
{
    Idle,
    Compiling,
    Running,
    Error,
    Paused,
};

enum class ResolutionScale
{
    Full,      // 1.0x native
    Half,      // 0.5x downsampled (4x faster)
    Quarter,   // 0.25x downsampled (16x faster)
};

enum class ChannelTextureKind
{
    Empty,
    PerlinNoise,
    SimplexNoise,
    VoronoiCells,
    Checkerboard,
    AudioSpectrum,
};

struct ShaderUniforms
{
    float resolution[3] = {640.0F, 360.0F, 1.0F};
    float time = 0.0F;
    float time_delta = 0.016667F;
    int frame = 0;
    float mouse[4] = {0.0F, 0.0F, 0.0F, 0.0F}; // x, y, click_x, click_y
    float date[4] = {2026.0F, 8.0F, 14.0F, 0.0F};
};

struct ShaderDiagnostic
{
    int line = 0;
    int column = 0;
    std::string message;
    bool is_error = true;
};

struct ShaderChannel
{
    ChannelTextureKind kind = ChannelTextureKind::Empty;
    std::string custom_file_path;
    int width = 256;
    int height = 256;
    std::vector<std::uint32_t> pixels; // 0xAARRGGBB
};

struct ShaderPreset
{
    std::string name;
    ShaderFormat format = ShaderFormat::Shadertoy;
    std::string source_code;
};

} // namespace Zenvra::Services::Shader
