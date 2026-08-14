#pragma once

#include "Drivers/Graphics/OpenGLFunctions.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Graphics {

class OpenGLShader {
public:
    OpenGLShader() = default;
    ~OpenGLShader();

    OpenGLShader(const OpenGLShader&) = delete;
    OpenGLShader& operator=(const OpenGLShader&) = delete;

    OpenGLShader(OpenGLShader&& other) noexcept;
    OpenGLShader& operator=(OpenGLShader&& other) noexcept;

    /// Compile vertex and fragment shader sources and link into a shader program.
    bool compile_from_source(std::string_view vertex_source, std::string_view fragment_source);

    /// Bind the shader program for rendering.
    void bind() const;

    /// Unbind the shader program.
    void unbind() const;

    /// Releases OpenGL resources.
    void destroy();

    [[nodiscard]] bool is_valid() const noexcept { return m_program != 0; }
    [[nodiscard]] unsigned int get_program_id() const noexcept { return m_program; }
    [[nodiscard]] const std::string& get_info_log() const noexcept { return m_info_log; }

    /// Set uniforms
    int get_uniform_location(std::string_view name);
    void set_uniform_1i(std::string_view name, int value);
    void set_uniform_1f(std::string_view name, float value);
    void set_uniform_2f(std::string_view name, float x, float y);
    void set_uniform_4f(std::string_view name, float x, float y, float z, float w);

private:
    unsigned int compile_shader(unsigned int type, std::string_view source);

    unsigned int m_program = 0;
    std::string m_info_log;
    std::unordered_map<std::string, int> m_uniform_cache;
};

} // namespace Zenvra::Graphics
