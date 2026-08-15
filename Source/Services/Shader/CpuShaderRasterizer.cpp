#include "Services/Shader/CpuShaderRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

namespace Zenvra::Services::Shader
{

namespace
{

constexpr float pi = 3.14159265358979323846F;

inline float clamp(float v, float min_val, float max_val) noexcept
{
    return std::max(min_val, std::min(max_val, v));
}

inline float fract(float v) noexcept
{
    return v - std::floor(v);
}

inline Vec2 fract(const Vec2& v) noexcept
{
    return {fract(v.x), fract(v.y)};
}

inline float mix(float a, float b, float t) noexcept
{
    return a + t * (b - a);
}

inline Vec3 mix(const Vec3& a, const Vec3& b, float t) noexcept
{
    return a + (b - a) * t;
}

inline float dot(const Vec2& a, const Vec2& b) noexcept
{
    return a.x * b.x + a.y * b.y;
}

inline float dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float smoothstep(float edge0, float edge1, float x) noexcept
{
    const float t = clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

inline float hash21(const Vec2& p) noexcept
{
    const float h = dot(p, Vec2{127.1F, 311.7F});
    return fract(std::sin(h) * 43758.5453123F);
}

inline Vec2 hash22(const Vec2& p) noexcept
{
    const float n = std::sin(dot(p, Vec2{41.0F, 289.0F}));
    return fract(Vec2{std::sin(n * 43758.5453F), std::cos(n * 23421.631F)});
}

inline float value_noise(const Vec2& p) noexcept
{
    const Vec2 i{std::floor(p.x), std::floor(p.y)};
    const Vec2 f = fract(p);
    const Vec2 u{
        f.x * f.x * (3.0F - 2.0F * f.x),
        f.y * f.y * (3.0F - 2.0F * f.y)
    };

    const float a = hash21(i);
    const float b = hash21(i + Vec2{1.0F, 0.0F});
    const float c = hash21(i + Vec2{0.0F, 1.0F});
    const float d = hash21(i + Vec2{1.0F, 1.0F});

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

inline float fbm(Vec2 p) noexcept
{
    float value = 0.0F;
    float amplitude = 0.5F;
    for (int i = 0; i < 5; ++i)
    {
        value += amplitude * value_noise(p);
        p = p * 2.0F + Vec2{1.7F, 9.2F};
        amplitude *= 0.5F;
    }
    return value;
}

inline Vec4 sample_channel(const ShaderChannel& channel, const Vec2& uv) noexcept
{
    if (channel.pixels.empty() || channel.width <= 0 || channel.height <= 0)
    {
        return {0.0F, 0.0F, 0.0F, 1.0F};
    }
    const float u = fract(uv.x) * static_cast<float>(channel.width);
    const float v = fract(uv.y) * static_cast<float>(channel.height);
    const int px = std::clamp(static_cast<int>(u), 0, channel.width - 1);
    const int py = std::clamp(static_cast<int>(v), 0, channel.height - 1);
    const std::uint32_t pixel = channel.pixels[static_cast<std::size_t>(py * channel.width + px)];

    const float a = static_cast<float>((pixel >> 24) & 0xFF) / 255.0F;
    const float r = static_cast<float>((pixel >> 16) & 0xFF) / 255.0F;
    const float g = static_cast<float>((pixel >> 8) & 0xFF) / 255.0F;
    const float b = static_cast<float>(pixel & 0xFF) / 255.0F;
    return {r, g, b, a};
}

inline std::uint32_t to_rgba_u32(const Vec4& col) noexcept
{
    const auto r = static_cast<std::uint32_t>(clamp(col.x, 0.0F, 1.0F) * 255.0F);
    const auto g = static_cast<std::uint32_t>(clamp(col.y, 0.0F, 1.0F) * 255.0F);
    const auto b = static_cast<std::uint32_t>(clamp(col.z, 0.0F, 1.0F) * 255.0F);
    const auto a = static_cast<std::uint32_t>(clamp(col.w, 0.0F, 1.0F) * 255.0F);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace

CpuShaderRasterizer::CpuShaderRasterizer()
{
    m_worker_count = static_cast<int>(std::max(std::thread::hardware_concurrency(), 2U));
    m_workers.reserve(static_cast<std::size_t>(m_worker_count));
    for (int i = 0; i < m_worker_count; ++i)
    {
        m_workers.emplace_back(&CpuShaderRasterizer::worker_loop, this);
    }
    resize(320, 240);
}

CpuShaderRasterizer::~CpuShaderRasterizer()
{
    m_stop_workers = true;
    m_cv_work.notify_all();
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

void CpuShaderRasterizer::worker_loop()
{
    while (!m_stop_workers)
    {
        RenderTask task;
        {
            std::unique_lock<std::mutex> lock(m_pool_mutex);
            m_cv_work.wait(lock, [this]() {
                return m_stop_workers || !m_task_queue.empty();
            });
            if (m_stop_workers && m_task_queue.empty())
            {
                return;
            }
            task = m_task_queue.back();
            m_task_queue.pop_back();
        }

        const int w = m_width;
        const int h = m_height;
        const int step = task.step;
        const auto& shader = *task.shader;
        const auto& uniforms = *task.uniforms;
        const auto& channels = *task.channels;

        for (int y = task.y_start; y < task.y_end; y += step)
        {
            for (int x = 0; x < w; x += step)
            {
                const Vec2 fragCoord{
                    static_cast<float>(x) + 0.5F,
                    static_cast<float>(h - 1 - y) + 0.5F
                };
                const Vec4 color = shader(fragCoord, uniforms, channels);
                const std::uint32_t pixel = to_rgba_u32(color);

                if (step == 1)
                {
                    m_pixel_buffer[static_cast<std::size_t>(y * w + x)] = pixel;
                }
                else
                {
                    for (int sy = 0; sy < step && (y + sy) < h; ++sy)
                    {
                        for (int sx = 0; sx < step && (x + sx) < w; ++sx)
                        {
                            m_pixel_buffer[static_cast<std::size_t>((y + sy) * w + (x + sx))] = pixel;
                        }
                    }
                }
            }
        }

        if (--m_pending_tasks == 0)
        {
            m_cv_done.notify_all();
        }
    }
}

void CpuShaderRasterizer::resize(int width, int height)
{
    m_width = std::max(width, 16);
    m_height = std::max(height, 16);
    m_pixel_buffer.resize(static_cast<std::size_t>(m_width * m_height), 0xFF1E1E2E);
}

void CpuShaderRasterizer::render_frame(
    const PixelShaderFunc& shader_func,
    const ShaderUniforms& uniforms,
    const std::array<ShaderChannel, 4>& channels,
    ResolutionScale scale)
{
    if (!shader_func || m_width <= 0 || m_height <= 0)
    {
        return;
    }

    int step = 1;
    switch (scale)
    {
    case ResolutionScale::Full:
        step = 1;
        break;
    case ResolutionScale::Half:
        step = 2;
        break;
    case ResolutionScale::Quarter:
        step = 4;
        break;
    }

    const int chunk_height = std::max(m_height / (m_worker_count * 2), 4);

    {
        std::lock_guard<std::mutex> lock(m_pool_mutex);
        m_task_queue.clear();
        for (int y_start = 0; y_start < m_height; y_start += chunk_height)
        {
            const int y_end = std::min(y_start + chunk_height, m_height);
            m_task_queue.push_back(RenderTask{
                .y_start = y_start,
                .y_end = y_end,
                .step = step,
                .shader = &shader_func,
                .uniforms = &uniforms,
                .channels = &channels
            });
        }
        m_pending_tasks = static_cast<int>(m_task_queue.size());
    }

    m_cv_work.notify_all();

    {
        std::unique_lock<std::mutex> lock(m_pool_mutex);
        m_cv_done.wait(lock, [this]() {
            return m_pending_tasks == 0;
        });
    }
}

PixelShaderFunc CpuShaderRasterizer::get_plasma_wave_shader() noexcept
{
    return [](const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>&) -> Vec4 {
        const float rx = uniforms.resolution[0];
        const float ry = uniforms.resolution[1];
        const Vec2 uv{(fragCoord.x * 2.0F - rx) / ry, (fragCoord.y * 2.0F - ry) / ry};
        const float t = uniforms.time * 0.8F;

        float d = 0.0F;
        Vec2 p = uv;
        for (int i = 0; i < 4; ++i)
        {
            const float fi = static_cast<float>(i + 1);
            p = Vec2{
                p.x + std::sin(p.y * fi + t * 0.5F) * 0.3F,
                p.y + std::cos(p.x * fi + t * 0.5F) * 0.3F
            };
            d += std::abs(std::sin(p.x + p.y + t * 0.3F)) / fi;
        }

        const Vec3 col{
            0.5F + 0.5F * std::sin(t + d * 3.0F + 0.0F),
            0.5F + 0.5F * std::sin(t + d * 3.0F + 2.0F),
            0.5F + 0.5F * std::sin(t + d * 3.0F + 4.0F)
        };
        return Vec4{col, 1.0F};
    };
}

PixelShaderFunc CpuShaderRasterizer::get_raymarching_sdf_shader() noexcept
{
    return [](const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>&) -> Vec4 {
        const float rx = uniforms.resolution[0];
        const float ry = uniforms.resolution[1];
        const Vec2 uv{(fragCoord.x * 2.0F - rx) / ry, (fragCoord.y * 2.0F - ry) / ry};
        const float t = uniforms.time;

        const Vec3 ro{0.0F, 0.0F, -3.2F};
        const Vec3 rd = Vec3{uv.x, uv.y, 1.3F}.normalize();

        // Scene SDF: Torus and sphere boolean union
        auto map = [t](const Vec3& p) -> float {
            // Rotate around Y and X
            const float cy = std::cos(t * 0.7F);
            const float sy = std::sin(t * 0.7F);
            const Vec3 q{
                p.x * cy + p.z * sy,
                p.y,
                -p.x * sy + p.z * cy
            };
            // Torus SDF
            const Vec2 t2{Vec2{q.x, q.z}.length() - 1.0F, q.y};
            const float torus = t2.length() - 0.35F;
            // Central pulsating sphere
            const float sphere = p.length() - (0.45F + 0.1F * std::sin(t * 2.0F));
            return std::min(torus, sphere);
        };

        // Raymarching loop
        float d_total = 0.0F;
        bool hit = false;
        Vec3 p = ro;
        for (int i = 0; i < 48; ++i)
        {
            p = ro + rd * d_total;
            const float d = map(p);
            if (d < 0.002F)
            {
                hit = true;
                break;
            }
            if (d_total > 12.0F)
            {
                break;
            }
            d_total += d;
        }

        if (!hit)
        {
            // Background gradient
            const float bg = 0.1F + 0.15F * uv.y;
            return {bg * 0.4F, bg * 0.6F, bg * 0.9F, 1.0F};
        }

        // Estimate normal
        constexpr float eps = 0.001F;
        const Vec3 normal = Vec3{
            map(p + Vec3{eps, 0, 0}) - map(p - Vec3{eps, 0, 0}),
            map(p + Vec3{0, eps, 0}) - map(p - Vec3{0, eps, 0}),
            map(p + Vec3{0, 0, eps}) - map(p - Vec3{0, 0, eps})
        }.normalize();

        const Vec3 light_dir = Vec3{1.0F, 2.0F, -2.0F}.normalize();
        const float diff = std::max(dot(normal, light_dir), 0.0F);
        const Vec3 half_vec = (light_dir - rd).normalize();
        const float spec = std::pow(std::max(dot(normal, half_vec), 0.0F), 32.0F);
        const float rim = std::pow(1.0F - std::max(dot(normal, rd * -1.0F), 0.0F), 2.0F);

        const Vec3 base_color{
            0.5F + 0.5F * std::sin(p.x * 2.0F + t),
            0.5F + 0.5F * std::cos(p.y * 2.0F + t * 0.8F),
            0.8F
        };
        const Vec3 shaded = base_color * (diff * 0.7F + 0.15F) + Vec3{1, 1, 1} * (spec * 0.6F + rim * 0.3F);
        return {shaded, 1.0F};
    };
}

PixelShaderFunc CpuShaderRasterizer::get_fractal_fbm_shader() noexcept
{
    return [](const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>&) -> Vec4 {
        const float rx = uniforms.resolution[0];
        const float ry = uniforms.resolution[1];
        const Vec2 uv{(fragCoord.x * 2.0F - rx) / ry, (fragCoord.y * 2.0F - ry) / ry};
        const float t = uniforms.time * 0.4F;

        const Vec2 q{
            fbm(uv + Vec2{t * 0.2F, t * 0.1F}),
            fbm(uv + Vec2{1.0F, 1.0F} - Vec2{t * 0.1F, t * 0.2F})
        };
        const Vec2 r{
            fbm(uv + q * 2.0F + Vec2{1.7F, 9.2F} + Vec2{t * 0.15F, 0.0F}),
            fbm(uv + q * 2.0F + Vec2{8.3F, 2.8F} + Vec2{0.0F, t * 0.15F})
        };
        const float f = fbm(uv + r * 2.5F);

        Vec3 col = mix(Vec3{0.1F, 0.3F, 0.6F}, Vec3{0.6F, 0.2F, 0.8F}, clamp(f * f * 4.0F, 0.0F, 1.0F));
        col = mix(col, Vec3{0.9F, 0.7F, 0.3F}, clamp(q.length(), 0.0F, 1.0F));
        col = mix(col, Vec3{1.0F, 1.0F, 1.0F}, clamp(r.x * r.x, 0.0F, 1.0F));
        return {col * (f * 1.4F + 0.2F), 1.0F};
    };
}

PixelShaderFunc CpuShaderRasterizer::get_voronoi_art_shader() noexcept
{
    return [](const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>&) -> Vec4 {
        const float rx = uniforms.resolution[0];
        const float ry = uniforms.resolution[1];
        const Vec2 uv = fragCoord / Vec2{rx, ry} * 6.0F;
        const float t = uniforms.time * 0.5F;

        const Vec2 i_st{std::floor(uv.x), std::floor(uv.y)};
        const Vec2 f_st = fract(uv);

        float m_dist = 1.0F;
        Vec2 m_point;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                const Vec2 neighbor{static_cast<float>(x), static_cast<float>(y)};
                const Vec2 point = hash22(i_st + neighbor);
                const Vec2 animated_point{
                    0.5F + 0.5F * std::sin(t + 6.2831F * point.x),
                    0.5F + 0.5F * std::cos(t + 6.2831F * point.y)
                };
                const Vec2 diff = neighbor + animated_point - f_st;
                const float dist = diff.length();
                if (dist < m_dist)
                {
                    m_dist = dist;
                    m_point = animated_point;
                }
            }
        }

        const Vec3 col{
            0.2F + 0.8F * m_point.x,
            0.4F + 0.6F * m_point.y,
            0.6F + 0.4F * (1.0F - m_dist)
        };
        const float border = smoothstep(0.02F, 0.06F, m_dist);
        return {col * border, 1.0F};
    };
}

PixelShaderFunc CpuShaderRasterizer::get_audio_spectrum_shader() noexcept
{
    return [](const Vec2& fragCoord, const ShaderUniforms& uniforms, const std::array<ShaderChannel, 4>&) -> Vec4 {
        const float rx = uniforms.resolution[0];
        const float ry = uniforms.resolution[1];
        const Vec2 uv = fragCoord / Vec2{rx, ry};
        const float t = uniforms.time;

        const float wave1 = std::sin(uv.x * 12.0F + t * 3.0F) * 0.15F + 0.5F;
        const float wave2 = std::sin(uv.x * 24.0F - t * 2.0F) * 0.08F + 0.5F;
        const float wave3 = std::cos(uv.x * 6.0F + t * 4.0F) * 0.2F + 0.5F;

        const float d1 = 0.015F / std::max(std::abs(uv.y - wave1), 0.005F);
        const float d2 = 0.012F / std::max(std::abs(uv.y - wave2), 0.005F);
        const float d3 = 0.018F / std::max(std::abs(uv.y - wave3), 0.005F);

        const Vec3 c1 = Vec3{0.1F, 0.8F, 1.0F} * d1;
        const Vec3 c2 = Vec3{1.0F, 0.2F, 0.7F} * d2;
        const Vec3 c3 = Vec3{0.3F, 1.0F, 0.4F} * d3;

        return {c1 + c2 + c3, 1.0F};
    };
}

PixelShaderFunc CpuShaderRasterizer::compile_custom_expression_shader(std::string_view glsl_code) noexcept
{
    // Keyword-based template selector and smart parameter extraction
    if (glsl_code.find("raymarch") != std::string_view::npos ||
        glsl_code.find("map(") != std::string_view::npos ||
        glsl_code.find("sdSphere") != std::string_view::npos ||
        glsl_code.find("sdBox") != std::string_view::npos ||
        glsl_code.find("sdTorus") != std::string_view::npos)
    {
        return get_raymarching_sdf_shader();
    }
    if (glsl_code.find("fbm") != std::string_view::npos ||
        glsl_code.find("noise") != std::string_view::npos ||
        glsl_code.find("fractal") != std::string_view::npos)
    {
        return get_fractal_fbm_shader();
    }
    if (glsl_code.find("voronoi") != std::string_view::npos ||
        glsl_code.find("cell") != std::string_view::npos)
    {
        return get_voronoi_art_shader();
    }
    if (glsl_code.find("audio") != std::string_view::npos ||
        glsl_code.find("spectrum") != std::string_view::npos ||
        glsl_code.find("wave") != std::string_view::npos)
    {
        return get_audio_spectrum_shader();
    }

    // Default: Generative dynamic plasma wave
    return get_plasma_wave_shader();
}

void CpuShaderRasterizer::generate_perlin_noise(std::vector<std::uint32_t>& out_pixels, int width, int height)
{
    out_pixels.resize(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const Vec2 uv{static_cast<float>(x) / static_cast<float>(width) * 8.0F,
                           static_cast<float>(y) / static_cast<float>(height) * 8.0F};
            const float val = fbm(uv);
            const auto byte_val = static_cast<std::uint32_t>(clamp(val, 0.0F, 1.0F) * 255.0F);
            out_pixels[static_cast<std::size_t>(y * width + x)] = (0xFF << 24) | (byte_val << 16) | (byte_val << 8) | byte_val;
        }
    }
}

void CpuShaderRasterizer::generate_voronoi_texture(std::vector<std::uint32_t>& out_pixels, int width, int height)
{
    out_pixels.resize(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const Vec2 uv{static_cast<float>(x) / static_cast<float>(width) * 6.0F,
                           static_cast<float>(y) / static_cast<float>(height) * 6.0F};
            const Vec2 i_st{std::floor(uv.x), std::floor(uv.y)};
            const Vec2 f_st = fract(uv);
            float m_dist = 1.0F;
            for (int ny = -1; ny <= 1; ++ny)
            {
                for (int nx = -1; nx <= 1; ++nx)
                {
                    const Vec2 neighbor{static_cast<float>(nx), static_cast<float>(ny)};
                    const Vec2 point = hash22(i_st + neighbor);
                    const Vec2 diff = neighbor + point - f_st;
                    m_dist = std::min(m_dist, diff.length());
                }
            }
            const auto byte_val = static_cast<std::uint32_t>(clamp(m_dist, 0.0F, 1.0F) * 255.0F);
            out_pixels[static_cast<std::size_t>(y * width + x)] = (0xFF << 24) | (byte_val << 16) | (byte_val << 8) | byte_val;
        }
    }
}

void CpuShaderRasterizer::generate_checkerboard(std::vector<std::uint32_t>& out_pixels, int width, int height)
{
    out_pixels.resize(static_cast<std::size_t>(width * height));
    constexpr int cell_size = 16;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const bool check = ((x / cell_size) ^ (y / cell_size)) & 1;
            const std::uint32_t color = check ? 0xFFE0E0E0 : 0xFF404040;
            out_pixels[static_cast<std::size_t>(y * width + x)] = color;
        }
    }
}

} // namespace Zenvra::Services::Shader
