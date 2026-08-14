#pragma once

#include "Services/Shader/CpuShaderRasterizer.h"
#include "Services/Shader/ShaderTypes.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Shader
{

class ShaderCompiler
{
public:
    ShaderCompiler();
    ~ShaderCompiler();

    [[nodiscard]] ShaderFormat detect_format(std::string_view source_code) const noexcept;
    [[nodiscard]] std::vector<ShaderDiagnostic> validate_syntax(std::string_view source_code) const;

    [[nodiscard]] std::optional<PixelShaderFunc> compile(
        std::string_view source_code,
        std::vector<ShaderDiagnostic>& out_diagnostics);

    [[nodiscard]] std::string wrap_shadertoy_source(std::string_view user_code) const;

    [[nodiscard]] static std::span<const ShaderPreset> get_starter_presets() noexcept;

private:
    static const std::vector<ShaderPreset>& get_presets_internal();
};

} // namespace Zenvra::Services::Shader
