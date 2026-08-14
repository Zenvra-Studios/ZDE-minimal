#pragma once

#include "Services/Shader/ShaderTypes.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Shader
{

struct Vec2
{
    float x = 0.0F;
    float y = 0.0F;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_) {}

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    [[nodiscard]] constexpr Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    [[nodiscard]] constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
    [[nodiscard]] constexpr Vec2 operator*(const Vec2& o) const noexcept { return {x * o.x, y * o.y}; }
    [[nodiscard]] constexpr Vec2 operator/(float s) const noexcept { return {x / s, y / s}; }
    [[nodiscard]] constexpr Vec2 operator/(const Vec2& o) const noexcept { return {x / o.x, y / o.y}; }
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y); }
};

struct Vec3
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}
    constexpr Vec3(const Vec2& v, float z_) noexcept : x(v.x), y(v.y), z(z_) {}

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    [[nodiscard]] constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    [[nodiscard]] constexpr Vec3 operator*(const Vec3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
    [[nodiscard]] constexpr Vec3 operator/(float s) const noexcept { return {x / s, y / s, z / s}; }
    [[nodiscard]] float length() const noexcept { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] Vec3 normalize() const noexcept
    {
        const float len = length();
        return len > 0.00001F ? (*this * (1.0F / len)) : Vec3{0, 0, 0};
    }
};

struct Vec4
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;

    constexpr Vec4() noexcept = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}

    [[nodiscard]] constexpr Vec4 operator+(const Vec4& o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    [[nodiscard]] constexpr Vec4 operator*(float s) const noexcept { return {x * s, y * s, z * s, w * s}; }
};

using PixelShaderFunc = std::function<Vec4(const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>& channels)>;

class CpuShaderRasterizer
{
public:
    CpuShaderRasterizer();
    ~CpuShaderRasterizer();

    void resize(int width, int height);
    [[nodiscard]] int get_width() const noexcept { return m_width; }
    [[nodiscard]] int get_height() const noexcept { return m_height; }

    void render_frame(
        const PixelShaderFunc& shader_func,
        const ShaderUniforms& uniforms,
        const std::array<ShaderChannel, 4>& channels,
        ResolutionScale scale);

    [[nodiscard]] std::span<const std::uint32_t> get_pixel_buffer() const noexcept
    {
        return m_pixel_buffer;
    }

    // Built-in reference shader templates for instant live simulation
    [[nodiscard]] static PixelShaderFunc get_plasma_wave_shader() noexcept;
    [[nodiscard]] static PixelShaderFunc get_raymarching_sdf_shader() noexcept;
    [[nodiscard]] static PixelShaderFunc get_fractal_fbm_shader() noexcept;
    [[nodiscard]] static PixelShaderFunc get_voronoi_art_shader() noexcept;
    [[nodiscard]] static PixelShaderFunc get_audio_spectrum_shader() noexcept;
    [[nodiscard]] static PixelShaderFunc compile_custom_expression_shader(std::string_view glsl_code) noexcept;

    // Procedural texture generators for channel inputs
    static void generate_perlin_noise(std::vector<std::uint32_t>& out_pixels, int width, int height);
    static void generate_voronoi_texture(std::vector<std::uint32_t>& out_pixels, int width, int height);
    static void generate_checkerboard(std::vector<std::uint32_t>& out_pixels, int width, int height);

private:
    int m_width = 320;
    int m_height = 240;
    std::vector<std::uint32_t> m_pixel_buffer;
};

} // namespace Zenvra::Services::Shader
