#include "Services/Shader/ShaderCompiler.h"

#include <algorithm>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>

namespace Zenvra::Services::Shader
{

namespace
{

const std::vector<ShaderPreset>& init_presets()
{
    static const std::vector<ShaderPreset> presets{
        ShaderPreset{
            "Plasma Spectrum",
            ShaderFormat::Shadertoy,
            "// Simple Psychedelic Plasma Wave Shader\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
            "    float t = iTime * 0.8;\n"
            "    float d = 0.0;\n"
            "    vec2 p = uv;\n"
            "    for (int i = 0; i < 4; ++i) {\n"
            "        float fi = float(i + 1);\n"
            "        p += vec2(sin(p.y * fi + t * 0.5) * 0.3, cos(p.x * fi + t * 0.5) * 0.3);\n"
            "        d += abs(sin(p.x + p.y + t * 0.3)) / fi;\n"
            "    }\n"
            "    vec3 col = 0.5 + 0.5 * sin(t + d * 3.0 + vec3(0.0, 2.0, 4.0));\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "3D Raymarching SDF",
            ShaderFormat::Shadertoy,
            "// 3D Raymarching SDF Torus & Sphere with Phong Lighting\n"
            "float map(vec3 p) {\n"
            "    vec3 q = p;\n"
            "    float cy = cos(iTime * 0.7), sy = sin(iTime * 0.7);\n"
            "    q.xz = mat2(cy, -sy, sy, cy) * q.xz;\n"
            "    vec2 t = vec2(length(q.xz) - 1.0, q.y);\n"
            "    float torus = length(t) - 0.35;\n"
            "    float sphere = length(p) - (0.45 + 0.1 * sin(iTime * 2.0));\n"
            "    return min(torus, sphere);\n"
            "}\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
            "    vec3 ro = vec3(0.0, 0.0, -3.2);\n"
            "    vec3 rd = normalize(vec3(uv, 1.3));\n"
            "    float d = 0.0;\n"
            "    for (int i = 0; i < 64; ++i) {\n"
            "        vec3 p = ro + rd * d;\n"
            "        float dist = map(p);\n"
            "        if (dist < 0.001) break;\n"
            "        d += dist;\n"
            "        if (d > 20.0) break;\n"
            "    }\n"
            "    vec3 col = vec3(0.05, 0.08, 0.15);\n"
            "    if (d < 20.0) {\n"
            "        vec3 p = ro + rd * d;\n"
            "        vec2 eps = vec2(0.002, 0.0);\n"
            "        vec3 n = normalize(vec3(\n"
            "            map(p + eps.xyy) - map(p - eps.xyy),\n"
            "            map(p + eps.yxy) - map(p - eps.yxy),\n"
            "            map(p + eps.yyx) - map(p - eps.yyx)\n"
            "        ));\n"
            "        vec3 light = normalize(vec3(1.0, 2.0, -1.5));\n"
            "        float diff = max(dot(n, light), 0.0);\n"
            "        float spec = pow(max(dot(reflect(-light, n), -rd), 0.0), 16.0);\n"
            "        vec3 mat = mix(vec3(0.1, 0.6, 0.9), vec3(0.9, 0.3, 0.6), 0.5 + 0.5 * sin(p.y * 3.0 + iTime));\n"
            "        col = mat * (diff + 0.15) + vec3(1.0) * spec * 0.5;\n"
            "    }\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Fractal Noise FBM",
            ShaderFormat::Shadertoy,
            "// Domain Warping Fractal Brownian Motion (FBM noise)\n"
            "float hash(vec2 p) {\n"
            "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
            "    p3 += dot(p3, p3.yzx + 33.33);\n"
            "    return fract((p3.x + p3.y) * p3.z);\n"
            "}\n"
            "float noise(vec2 p) {\n"
            "    vec2 i = floor(p), f = fract(p);\n"
            "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
            "    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),\n"
            "               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
            "}\n"
            "float fbm(vec2 p) {\n"
            "    float v = 0.0, a = 0.5;\n"
            "    for (int i = 0; i < 5; ++i) { v += a * noise(p); p = p * 2.0 + vec2(1.7, 9.2); a *= 0.5; }\n"
            "    return v;\n"
            "}\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
            "    float t = iTime * 0.4;\n"
            "    vec2 q = vec2(fbm(uv + vec2(t * 0.2, t * 0.1)), fbm(uv + vec2(1.0, 1.0) - vec2(t * 0.1, t * 0.2)));\n"
            "    vec2 r = vec2(fbm(uv + q * 2.0 + vec2(1.7, 9.2) + vec2(t * 0.15, 0.0)), fbm(uv + q * 2.0 + vec2(8.3, 2.8) + vec2(0.0, t * 0.15)));\n"
            "    float f = fbm(uv + r * 2.5);\n"
            "    vec3 col = mix(vec3(0.1, 0.3, 0.6), vec3(0.6, 0.2, 0.8), clamp(f * f * 4.0, 0.0, 1.0));\n"
            "    col = mix(col, vec3(0.9, 0.7, 0.3), clamp(length(q), 0.0, 1.0));\n"
            "    fragColor = vec4(col * (f * 1.4 + 0.2), 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Voronoi Cells",
            ShaderFormat::Shadertoy,
            "// Dynamic Animated Voronoi Cell Diagram\n"
            "vec2 hash2(vec2 p) {\n"
            "    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
            "    p3 += dot(p3, p3.yzx + 33.33);\n"
            "    return fract((p3.xx + p3.yz) * p3.zy);\n"
            "}\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy * 6.0;\n"
            "    float t = iTime * 0.5;\n"
            "    vec2 i_st = floor(uv), f_st = fract(uv);\n"
            "    float m_dist = 1.0;\n"
            "    vec2 m_point = vec2(0.0);\n"
            "    for (int y = -1; y <= 1; ++y) {\n"
            "        for (int x = -1; x <= 1; ++x) {\n"
            "            vec2 neighbor = vec2(float(x), float(y));\n"
            "            vec2 point = hash2(i_st + neighbor);\n"
            "            vec2 anim = 0.5 + 0.5 * vec2(sin(t + 6.2831 * point.x), cos(t + 6.2831 * point.y));\n"
            "            vec2 diff = neighbor + anim - f_st;\n"
            "            float dist = length(diff);\n"
            "            if (dist < m_dist) { m_dist = dist; m_point = anim; }\n"
            "        }\n"
            "    }\n"
            "    vec3 col = vec3(0.2 + 0.8 * m_point.x, 0.4 + 0.6 * m_point.y, 0.6 + 0.4 * (1.0 - m_dist));\n"
            "    fragColor = vec4(col * smoothstep(0.02, 0.06, m_dist), 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Audio Reactive Waves",
            ShaderFormat::Shadertoy,
            "// Audio Reactive Neon Spectrum Waveform\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float t = iTime;\n"
            "    float wave1 = sin(uv.x * 12.0 + t * 3.0) * 0.15 + 0.5;\n"
            "    float wave2 = sin(uv.x * 24.0 - t * 2.0) * 0.08 + 0.5;\n"
            "    float wave3 = cos(uv.x * 6.0 + t * 4.0) * 0.2 + 0.5;\n"
            "    float d1 = 0.015 / max(abs(uv.y - wave1), 0.005);\n"
            "    float d2 = 0.012 / max(abs(uv.y - wave2), 0.005);\n"
            "    float d3 = 0.018 / max(abs(uv.y - wave3), 0.005);\n"
            "    vec3 col = vec3(0.1, 0.8, 1.0) * d1 + vec3(1.0, 0.2, 0.7) * d2 + vec3(0.3, 1.0, 0.4) * d3;\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Cyber Tunnel",
            ShaderFormat::Shadertoy,
            "// Infinite Cyber Tunnel Vortex\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 p = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
            "    float t = iTime * 1.2;\n"
            "    float r = length(p);\n"
            "    float a = atan(p.y, p.x);\n"
            "    vec2 uv = vec2(1.0 / r + t, a * 3.0 / 3.14159);\n"
            "    float grid = max(abs(fract(uv.x) - 0.5), abs(fract(uv.y) - 0.5));\n"
            "    float wire = smoothstep(0.40, 0.48, grid);\n"
            "    vec3 col = mix(vec3(0.05, 0.2, 0.5), vec3(0.9, 0.1, 0.6), 0.5 + 0.5 * sin(uv.x * 2.0 + t));\n"
            "    fragColor = vec4((col * 0.4 + vec3(0.2, 0.9, 1.0) * wire) * min(r * 2.2, 1.0), 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Galaxy Spiral",
            ShaderFormat::Shadertoy,
            "// Cosmic Spiral Galaxy & Nebula\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 p = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;\n"
            "    float t = iTime * 0.5;\n"
            "    float r = length(p), a = atan(p.y, p.x);\n"
            "    float arms = sin(a * 2.0 + 4.0 * log(r + 0.01) - t * 1.5);\n"
            "    float density = exp(-r * 2.0) * (0.6 + 0.4 * arms);\n"
            "    float core = 0.08 / (r + 0.05);\n"
            "    vec3 arm_col = vec3(0.3 + 0.3 * sin(t + r * 5.0), 0.5 + 0.3 * cos(t + r * 3.0), 0.9);\n"
            "    vec3 col = arm_col * density + vec3(1.0, 0.85, 0.5) * core;\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        },
        ShaderPreset{
            "Fire & Flame",
            ShaderFormat::Shadertoy,
            "// Procedural Volumetric Fire & Flame\n"
            "float hash(vec2 p) {\n"
            "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
            "    p3 += dot(p3, p3.yzx + 33.33);\n"
            "    return fract((p3.x + p3.y) * p3.z);\n"
            "}\n"
            "float noise(vec2 p) {\n"
            "    vec2 i = floor(p), f = fract(p);\n"
            "    vec2 u = f * f * (3.0 - 2.0 * f);\n"
            "    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),\n"
            "               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
            "}\n"
            "float fbm(vec2 p) {\n"
            "    float v = 0.0, a = 0.5;\n"
            "    for (int i = 0; i < 5; ++i) {\n"
            "        v += a * noise(p);\n"
            "        p = p * 2.02 + vec2(1.7, 9.2);\n"
            "        a *= 0.5;\n"
            "    }\n"
            "    return v;\n"
            "}\n"
            "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution.xy;\n"
            "    float t = iTime * 2.8;\n"
            "    vec2 p = uv * vec2(2.5, 3.5);\n"
            "    vec2 q = vec2(fbm(p + vec2(0.0, -t)), fbm(p + vec2(5.2, -t * 0.8)));\n"
            "    vec2 r = vec2(fbm(p + q * 2.5 + vec2(1.7, -t * 1.2)), fbm(p + q * 2.0 + vec2(8.3, -t * 1.5)));\n"
            "    float f = fbm(p + r * 1.8);\n"
            "    float center_dist = abs(uv.x - 0.5) * 2.2;\n"
            "    float vertical_fade = pow(clamp(1.0 - uv.y, 0.0, 1.0), 0.8);\n"
            "    float width_profile = (1.0 - uv.y * 0.75);\n"
            "    float mask = clamp((width_profile - center_dist * 0.9) * 2.0, 0.0, 1.0);\n"
            "    float intensity = clamp(f * mask * vertical_fade * 2.4, 0.0, 1.0);\n"
            "    vec3 col = vec3(0.0);\n"
            "    col += vec3(1.0, 0.2, 0.02) * smoothstep(0.05, 0.45, intensity);\n"
            "    col += vec3(1.0, 0.65, 0.08) * smoothstep(0.35, 0.75, intensity);\n"
            "    col += vec3(1.0, 0.95, 0.75) * smoothstep(0.70, 0.98, intensity);\n"
            "    float base_glow = (1.0 - length((uv - vec2(0.5, 0.0)) * vec2(1.5, 3.0)));\n"
            "    col += vec3(0.3, 0.08, 0.01) * clamp(base_glow, 0.0, 1.0) * 0.6;\n"
            "    fragColor = vec4(col, 1.0);\n"
            "}\n"
        },
    };
    return presets;
}

} // namespace

ShaderCompiler::ShaderCompiler() = default;
ShaderCompiler::~ShaderCompiler() = default;

const std::vector<ShaderPreset>& ShaderCompiler::get_presets_internal()
{
    return init_presets();
}

std::span<const ShaderPreset> ShaderCompiler::get_starter_presets() noexcept
{
    const auto& presets = get_presets_internal();
    return std::span<const ShaderPreset>(presets.data(), presets.size());
}

ShaderFormat ShaderCompiler::detect_format(std::string_view source_code) const noexcept
{
    if (source_code.find("mainImage") != std::string_view::npos)
    {
        return ShaderFormat::Shadertoy;
    }
    if (source_code.find("map(") != std::string_view::npos || source_code.find("sdSphere") != std::string_view::npos)
    {
        return ShaderFormat::RaymarchingSDF;
    }
    if (source_code.find("gl_FragColor") != std::string_view::npos || source_code.find("void main()") != std::string_view::npos)
    {
        return ShaderFormat::StandardGLSL;
    }
    return ShaderFormat::Shadertoy;
}

std::vector<ShaderDiagnostic> ShaderCompiler::validate_syntax(std::string_view source_code) const
{
    std::vector<ShaderDiagnostic> diagnostics;
    if (source_code.empty())
    {
        diagnostics.push_back({1, 1, "Shader source code is empty", false});
        return diagnostics;
    }

    struct BracketInfo
    {
        char ch;
        int line;
        int col;
    };
    std::stack<BracketInfo> bracket_stack;

    int current_line = 1;
    int current_col = 1;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < source_code.size(); ++i)
    {
        const char c = source_code[i];
        const char next = (i + 1 < source_code.size()) ? source_code[i + 1] : '\0';

        if (c == '\n')
        {
            current_line++;
            current_col = 1;
            in_line_comment = false;
            continue;
        }

        if (in_line_comment)
        {
            current_col++;
            continue;
        }

        if (in_block_comment)
        {
            if (c == '*' && next == '/')
            {
                in_block_comment = false;
                i++;
                current_col += 2;
                continue;
            }
            current_col++;
            continue;
        }

        if (c == '/' && next == '/')
        {
            in_line_comment = true;
            i++;
            current_col += 2;
            continue;
        }

        if (c == '/' && next == '*')
        {
            in_block_comment = true;
            i++;
            current_col += 2;
            continue;
        }

        if (c == '{' || c == '(' || c == '[')
        {
            bracket_stack.push({c, current_line, current_col});
        }
        else if (c == '}' || c == ')' || c == ']')
        {
            if (bracket_stack.empty())
            {
                diagnostics.push_back({
                    current_line,
                    current_col,
                    std::string("Unmatched closing bracket '") + c + "'",
                    true
                });
            }
            else
            {
                const BracketInfo top = bracket_stack.top();
                bracket_stack.pop();
                const bool match = (top.ch == '{' && c == '}') ||
                                   (top.ch == '(' && c == ')') ||
                                   (top.ch == '[' && c == ']');
                if (!match)
                {
                    diagnostics.push_back({
                        current_line,
                        current_col,
                        std::string("Mismatched brackets: opened '") + top.ch + "' at line " +
                            std::to_string(top.line) + " but found '" + c + "'",
                        true
                    });
                }
            }
        }

        current_col++;
    }

    while (!bracket_stack.empty())
    {
        const BracketInfo top = bracket_stack.top();
        bracket_stack.pop();
        diagnostics.push_back({
            top.line,
            top.col,
            std::string("Unclosed bracket '") + top.ch + "'",
            true
        });
    }

    return diagnostics;
}

std::optional<PixelShaderFunc> ShaderCompiler::compile(
    std::string_view source_code,
    std::vector<ShaderDiagnostic>& out_diagnostics)
{
    out_diagnostics = validate_syntax(source_code);
    for (const auto& diag : out_diagnostics)
    {
        if (diag.is_error)
        {
            return std::nullopt;
        }
    }

    // Compile into CPU pixel shader function
    return CpuShaderRasterizer::compile_custom_expression_shader(source_code);
}

std::string ShaderCompiler::wrap_shadertoy_source(std::string_view user_code) const
{
    std::string wrapped;
    wrapped.reserve(user_code.size() + 256);
    wrapped += "#version 330 core\n";
    wrapped += "uniform vec3 iResolution;\n";
    wrapped += "uniform float iTime;\n";
    wrapped += "uniform float iTimeDelta;\n";
    wrapped += "uniform int iFrame;\n";
    wrapped += "uniform vec4 iMouse;\n";
    wrapped += "uniform vec4 iDate;\n";
    wrapped += "uniform sampler2D iChannel0;\n";
    wrapped += "uniform sampler2D iChannel1;\n";
    wrapped += "uniform sampler2D iChannel2;\n";
    wrapped += "uniform sampler2D iChannel3;\n\n";
    wrapped += user_code;
    wrapped += "\nvoid main() {\n";
    wrapped += "    mainImage(gl_FragColor, gl_FragCoord.xy);\n";
    wrapped += "}\n";
    return wrapped;
}

} // namespace Zenvra::Services::Shader
