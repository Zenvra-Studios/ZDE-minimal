#include "Drivers/Graphics/OpenGLShader.h"

#include <vector>
#include <utility>

namespace Zenvra::Graphics {

OpenGLShader::~OpenGLShader() {
    destroy();
}

OpenGLShader::OpenGLShader(OpenGLShader&& other) noexcept
    : m_program(std::exchange(other.m_program, 0))
    , m_info_log(std::move(other.m_info_log))
    , m_uniform_cache(std::move(other.m_uniform_cache)) {
}

OpenGLShader& OpenGLShader::operator=(OpenGLShader&& other) noexcept {
    if (this != &other) {
        destroy();
        m_program = std::exchange(other.m_program, 0);
        m_info_log = std::move(other.m_info_log);
        m_uniform_cache = std::move(other.m_uniform_cache);
    }
    return *this;
}

void OpenGLShader::destroy() {
    if (m_program != 0) {
        auto& gl = get_gl_api();
        if (gl.DeleteProgram != nullptr) {
            gl.DeleteProgram(m_program);
        }
        m_program = 0;
    }
    m_uniform_cache.clear();
    m_info_log.clear();
}

unsigned int OpenGLShader::compile_shader(unsigned int type, std::string_view source) {
    auto& gl = get_gl_api();
    if (gl.CreateShader == nullptr || gl.ShaderSource == nullptr || gl.CompileShader == nullptr) {
        m_info_log = "OpenGL shader functions not available";
        return 0;
    }

    const unsigned int shader = gl.CreateShader(type);
    if (shader == 0) {
        m_info_log = "Failed to create OpenGL shader object";
        return 0;
    }

    const char* src_ptr = source.data();
    const int src_len = static_cast<int>(source.length());
    gl.ShaderSource(shader, 1, &src_ptr, &src_len);
    gl.CompileShader(shader);

    int success = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0) {
        int log_length = 0;
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            std::vector<char> buffer(static_cast<std::size_t>(log_length));
            gl.GetShaderInfoLog(shader, log_length, nullptr, buffer.data());
            m_info_log = buffer.data();
        } else {
            m_info_log = "Unknown shader compilation failure";
        }
        gl.DeleteShader(shader);
        return 0;
    }

    return shader;
}

bool OpenGLShader::compile_from_source(std::string_view vertex_source, std::string_view fragment_source) {
    destroy();

    if (!initialize_opengl_functions()) {
        m_info_log = "Failed to load OpenGL dynamic function pointers";
        return false;
    }

    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0) {
        return false;
    }

    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0) {
        get_gl_api().DeleteShader(vertex_shader);
        return false;
    }

    auto& gl = get_gl_api();
    const unsigned int program = gl.CreateProgram();
    if (program == 0) {
        gl.DeleteShader(vertex_shader);
        gl.DeleteShader(fragment_shader);
        m_info_log = "Failed to create OpenGL program object";
        return false;
    }

    gl.AttachShader(program, vertex_shader);
    gl.AttachShader(program, fragment_shader);
    gl.LinkProgram(program);

    int linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        int log_length = 0;
        gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            std::vector<char> buffer(static_cast<std::size_t>(log_length));
            gl.GetProgramInfoLog(program, log_length, nullptr, buffer.data());
            m_info_log = buffer.data();
        } else {
            m_info_log = "Unknown program link failure";
        }
        gl.DeleteShader(vertex_shader);
        gl.DeleteShader(fragment_shader);
        gl.DeleteProgram(program);
        return false;
    }

    // Clean up detached shaders once program is linked
    gl.DeleteShader(vertex_shader);
    gl.DeleteShader(fragment_shader);

    m_program = program;
    m_info_log.clear();
    return true;
}

void OpenGLShader::bind() const {
    if (m_program != 0) {
        auto& gl = get_gl_api();
        if (gl.UseProgram != nullptr) {
            gl.UseProgram(m_program);
        }
    }
}

void OpenGLShader::unbind() const {
    auto& gl = get_gl_api();
    if (gl.UseProgram != nullptr) {
        gl.UseProgram(0);
    }
}

int OpenGLShader::get_uniform_location(std::string_view name) {
    if (m_program == 0) {
        return -1;
    }

    const std::string name_str(name);
    auto it = m_uniform_cache.find(name_str);
    if (it != m_uniform_cache.end()) {
        return it->second;
    }

    auto& gl = get_gl_api();
    if (gl.GetUniformLocation == nullptr) {
        return -1;
    }

    const int location = gl.GetUniformLocation(m_program, name_str.c_str());
    m_uniform_cache.emplace(name_str, location);
    return location;
}

void OpenGLShader::set_uniform_1i(std::string_view name, int value) {
    const int loc = get_uniform_location(name);
    if (loc >= 0) {
        auto& gl = get_gl_api();
        if (gl.Uniform1i != nullptr) {
            gl.Uniform1i(loc, value);
        }
    }
}

void OpenGLShader::set_uniform_1f(std::string_view name, float value) {
    const int loc = get_uniform_location(name);
    if (loc >= 0) {
        auto& gl = get_gl_api();
        if (gl.Uniform1f != nullptr) {
            gl.Uniform1f(loc, value);
        }
    }
}

void OpenGLShader::set_uniform_2f(std::string_view name, float x, float y) {
    const int loc = get_uniform_location(name);
    if (loc >= 0) {
        auto& gl = get_gl_api();
        if (gl.Uniform2f != nullptr) {
            gl.Uniform2f(loc, x, y);
        }
    }
}

void OpenGLShader::set_uniform_4f(std::string_view name, float x, float y, float z, float w) {
    const int loc = get_uniform_location(name);
    if (loc >= 0) {
        auto& gl = get_gl_api();
        if (gl.Uniform4f != nullptr) {
            gl.Uniform4f(loc, x, y, z, w);
        }
    }
}

} // namespace Zenvra::Graphics
