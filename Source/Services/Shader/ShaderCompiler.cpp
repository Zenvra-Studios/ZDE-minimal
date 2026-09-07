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
    static const std::vector<ShaderPreset> presets{};
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
