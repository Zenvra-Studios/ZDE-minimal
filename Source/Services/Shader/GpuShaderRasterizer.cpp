#include "Services/Shader/GpuShaderRasterizer.h"
#include "Drivers/Graphics/OpenGLFunctions.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Shader
{

#if defined(_WIN32)
struct GpuShaderRasterizer::ContextImpl
{
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;

    ~ContextImpl()
    {
        destroy();
    }

    bool ensure_context()
    {
        if (hdc != nullptr && hglrc != nullptr)
        {
            return wglMakeCurrent(hdc, hglrc) == TRUE;
        }

        HINSTANCE hInst = GetModuleHandleA(nullptr);
        WNDCLASSA wc{};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = hInst;
        wc.lpszClassName = "ZDE_GPU_Rasterizer_Host";
        RegisterClassA(&wc);

        hwnd = CreateWindowExA(
            0, "ZDE_GPU_Rasterizer_Host", "ZDE_GPU_Host",
            WS_POPUP, 0, 0, 16, 16,
            nullptr, nullptr, hInst, nullptr);

        if (!hwnd)
        {
            return false;
        }

        hdc = GetDC(hwnd);
        if (!hdc)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        int format = ChoosePixelFormat(hdc, &pfd);
        if (format == 0 || !SetPixelFormat(hdc, format, &pfd))
        {
            ReleaseDC(hwnd, hdc);
            DestroyWindow(hwnd);
            hdc = nullptr;
            hwnd = nullptr;
            return false;
        }

        hglrc = wglCreateContext(hdc);
        if (!hglrc)
        {
            ReleaseDC(hwnd, hdc);
            DestroyWindow(hwnd);
            hdc = nullptr;
            hwnd = nullptr;
            return false;
        }

        return wglMakeCurrent(hdc, hglrc) == TRUE;
    }

    void make_current()
    {
        if (hdc && hglrc)
        {
            wglMakeCurrent(hdc, hglrc);
        }
    }

    void destroy()
    {
        if (hglrc)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
            hglrc = nullptr;
        }
        if (hwnd && hdc)
        {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }
};
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <GL/glx.h>

struct GpuShaderRasterizer::ContextImpl
{
    Display* display = nullptr;
    GLXContext glx_context = nullptr;
    GLXDrawable drawable = 0;
    bool own_display = false;

    ~ContextImpl()
    {
        destroy();
    }

    bool ensure_context()
    {
        if (display != nullptr && glx_context != nullptr && drawable != 0)
        {
            return glXMakeCurrent(display, drawable, glx_context) == True;
        }

        display = XOpenDisplay(nullptr);
        if (!display)
        {
            return false;
        }
        own_display = true;

        int screen = DefaultScreen(display);
        int attribs[] = {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_DEPTH_SIZE, 24,
            None
        };

        XVisualInfo* vi = glXChooseVisual(display, screen, attribs);
        if (!vi)
        {
            return false;
        }

        glx_context = glXCreateContext(display, vi, nullptr, GL_TRUE);
        XFree(vi);

        if (!glx_context)
        {
            return false;
        }

        XSetWindowAttributes swa{};
        swa.event_mask = 0;
        Window root = RootWindow(display, screen);
        drawable = XCreateWindow(
            display, root, 0, 0, 16, 16, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            0, &swa);

        if (!drawable)
        {
            return false;
        }

        return glXMakeCurrent(display, drawable, glx_context) == True;
    }

    void make_current()
    {
        if (display && glx_context && drawable)
        {
            glXMakeCurrent(display, drawable, glx_context);
        }
    }

    void destroy()
    {
        if (display && glx_context)
        {
            glXMakeCurrent(display, None, nullptr);
            glXDestroyContext(display, glx_context);
            glx_context = nullptr;
        }
        if (display && drawable)
        {
            XDestroyWindow(display, drawable);
            drawable = 0;
        }
        if (display && own_display)
        {
            XCloseDisplay(display);
            display = nullptr;
        }
    }
};
#elif defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

struct GpuShaderRasterizer::ContextImpl
{
    CGLContextObj cgl_context = nullptr;

    ~ContextImpl()
    {
        destroy();
    }

    bool ensure_context()
    {
        if (cgl_context != nullptr)
        {
            return CGLSetCurrentContext(cgl_context) == kCGLNoError;
        }

        CGLPixelFormatAttribute attribs[] = {
            kCGLPFAAccelerated,
            kCGLPFAColorSize, static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAAlphaSize, static_cast<CGLPixelFormatAttribute>(8),
            kCGLPFADepthSize, static_cast<CGLPixelFormatAttribute>(24),
            static_cast<CGLPixelFormatAttribute>(0)
        };

        CGLPixelFormatObj pixel_format = nullptr;
        GLint num_formats = 0;
        CGLError err = CGLChoosePixelFormat(attribs, &pixel_format, &num_formats);
        if (err != kCGLNoError || pixel_format == nullptr)
        {
            CGLPixelFormatAttribute fallback_attribs[] = {
                kCGLPFAColorSize, static_cast<CGLPixelFormatAttribute>(24),
                kCGLPFAAlphaSize, static_cast<CGLPixelFormatAttribute>(8),
                kCGLPFADepthSize, static_cast<CGLPixelFormatAttribute>(24),
                static_cast<CGLPixelFormatAttribute>(0)
            };
            err = CGLChoosePixelFormat(fallback_attribs, &pixel_format, &num_formats);
            if (err != kCGLNoError || pixel_format == nullptr)
            {
                return false;
            }
        }

        err = CGLCreateContext(pixel_format, nullptr, &cgl_context);
        CGLDestroyPixelFormat(pixel_format);

        if (err != kCGLNoError || cgl_context == nullptr)
        {
            return false;
        }

        return CGLSetCurrentContext(cgl_context) == kCGLNoError;
    }

    void make_current()
    {
        if (cgl_context != nullptr)
        {
            CGLSetCurrentContext(cgl_context);
        }
    }

    void destroy()
    {
        if (cgl_context != nullptr)
        {
            if (CGLGetCurrentContext() == cgl_context)
            {
                CGLSetCurrentContext(nullptr);
            }
            CGLDestroyContext(cgl_context);
            cgl_context = nullptr;
        }
    }
};
#else
struct GpuShaderRasterizer::ContextImpl
{
    bool ensure_context() { return false; }
    void make_current() {}
    void destroy() {}
};
#endif

namespace
{

constexpr const char* VertexShaderSource = R"(
#version 120
varying vec2 v_uv;
void main() {
    gl_Position = gl_Vertex;
    v_uv = (gl_Vertex.xy + 1.0) * 0.5;
}
)";

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif

} // namespace

GpuShaderRasterizer::GpuShaderRasterizer()
    : m_context(std::make_unique<ContextImpl>())
{
}

GpuShaderRasterizer::~GpuShaderRasterizer()
{
    destroy();
}

GpuShaderRasterizer::GpuShaderRasterizer(GpuShaderRasterizer&& other) noexcept
    : m_shader(std::move(other.m_shader))
    , m_active_code(std::move(other.m_active_code))
    , m_last_error(std::move(other.m_last_error))
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_fbo_width(other.m_fbo_width)
    , m_fbo_height(other.m_fbo_height)
    , m_fbo(other.m_fbo)
    , m_color_texture(other.m_color_texture)
    , m_depth_rbo(other.m_depth_rbo)
    , m_channel_textures(other.m_channel_textures)
    , m_loaded_channel_kinds(other.m_loaded_channel_kinds)
    , m_loaded_channel_sizes(other.m_loaded_channel_sizes)
    , m_pixel_buffer(std::move(other.m_pixel_buffer))
    , m_initialized(other.m_initialized)
    , m_context_valid(other.m_context_valid)
    , m_context(std::move(other.m_context))
{
    other.m_fbo = 0;
    other.m_color_texture = 0;
    other.m_depth_rbo = 0;
    other.m_channel_textures.fill(0);
    other.m_loaded_channel_kinds.fill(ChannelTextureKind::Empty);
    other.m_loaded_channel_sizes.fill(0);
    other.m_fbo_width = 0;
    other.m_fbo_height = 0;
    other.m_initialized = false;
    other.m_context_valid = false;
}

GpuShaderRasterizer& GpuShaderRasterizer::operator=(GpuShaderRasterizer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_shader = std::move(other.m_shader);
        m_active_code = std::move(other.m_active_code);
        m_last_error = std::move(other.m_last_error);
        m_width = other.m_width;
        m_height = other.m_height;
        m_fbo_width = other.m_fbo_width;
        m_fbo_height = other.m_fbo_height;
        m_fbo = other.m_fbo;
        m_color_texture = other.m_color_texture;
        m_depth_rbo = other.m_depth_rbo;
        m_channel_textures = other.m_channel_textures;
        m_loaded_channel_kinds = other.m_loaded_channel_kinds;
        m_loaded_channel_sizes = other.m_loaded_channel_sizes;
        m_pixel_buffer = std::move(other.m_pixel_buffer);
        m_initialized = other.m_initialized;
        m_context_valid = other.m_context_valid;
        m_context = std::move(other.m_context);

        other.m_fbo = 0;
        other.m_color_texture = 0;
        other.m_depth_rbo = 0;
        other.m_channel_textures.fill(0);
        other.m_loaded_channel_kinds.fill(ChannelTextureKind::Empty);
        other.m_loaded_channel_sizes.fill(0);
        other.m_fbo_width = 0;
        other.m_fbo_height = 0;
        other.m_initialized = false;
        other.m_context_valid = false;
    }
    return *this;
}

bool GpuShaderRasterizer::is_supported() const noexcept
{
    return Graphics::is_opengl_shader_supported();
}

bool GpuShaderRasterizer::is_valid() const noexcept
{
    return m_shader.is_valid() && m_initialized;
}

bool GpuShaderRasterizer::initialize(int width, int height)
{
    m_width = std::max(width, 16);
    m_height = std::max(height, 16);
    m_pixel_buffer.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), 0xFF000000);

    if (!m_context)
    {
        m_context = std::make_unique<ContextImpl>();
    }

    if (!m_context->ensure_context())
    {
        m_last_error = "Failed to initialize OpenGL hardware rendering context";
        return false;
    }

    if (!Graphics::initialize_opengl_functions())
    {
        m_last_error = "OpenGL function pointers could not be loaded";
        return false;
    }

    m_initialized = true;
    m_context_valid = true;
    ensure_fbo(m_width, m_height);
    return true;
}

void GpuShaderRasterizer::resize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (m_width == width && m_height == height)
    {
        return;
    }

    m_width = width;
    m_height = height;
    m_pixel_buffer.resize(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height), 0xFF000000);

    if (m_initialized && m_context)
    {
        m_context->make_current();
        ensure_fbo(m_width, m_height);
    }
}

std::string GpuShaderRasterizer::build_full_fragment_shader(std::string_view user_code)
{
    std::string code(user_code);

    // Strip any user-supplied #version directives to prevent conflict
    std::size_t vpos = code.find("#version");
    while (vpos != std::string::npos)
    {
        std::size_t eol = code.find('\n', vpos);
        if (eol != std::string::npos)
        {
            code.erase(vpos, eol - vpos + 1);
        }
        else
        {
            code.erase(vpos);
        }
        vpos = code.find("#version");
    }

    // Common GLSL 1.20 header with compatibility macros
    std::string header = R"(#version 120
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform vec4 iMouse;
uniform vec4 iDate;
uniform sampler2D iChannel0;
uniform sampler2D iChannel1;
uniform sampler2D iChannel2;
uniform sampler2D iChannel3;

varying vec2 v_uv;

#define texture(s, uv) texture2D(s, uv)

)";

    // Strip duplicate uniform declarations from user code
    const std::array<std::string_view, 10> common_uniforms = {
        "uniform vec3 iResolution",
        "uniform float iTime",
        "uniform float iTimeDelta",
        "uniform int iFrame",
        "uniform vec4 iMouse",
        "uniform vec4 iDate",
        "uniform sampler2D iChannel0",
        "uniform sampler2D iChannel1",
        "uniform sampler2D iChannel2",
        "uniform sampler2D iChannel3"
    };

    for (const auto& u : common_uniforms)
    {
        std::size_t pos = code.find(u);
        while (pos != std::string::npos)
        {
            std::size_t semi = code.find(';', pos);
            if (semi != std::string::npos)
            {
                code.erase(pos, semi - pos + 1);
            }
            else
            {
                break;
            }
            pos = code.find(u);
        }
    }

    // If code has Shadertoy mainImage:
    if (code.find("mainImage") != std::string::npos)
    {
        if (code.find("void main()") == std::string::npos &&
            code.find("void main ()") == std::string::npos &&
            code.find("void main(") == std::string::npos)
        {
            return header + code + R"(

void main() {
    mainImage(gl_FragColor, gl_FragCoord.xy);
}
)";
        }
        return header + code;
    }

    // If code has standard main:
    if (code.find("void main") != std::string::npos)
    {
        std::string modified_code = code;
        std::size_t out_decl = modified_code.find("out vec4 ");
        if (out_decl != std::string::npos)
        {
            std::size_t semi = modified_code.find(';', out_decl);
            if (semi != std::string::npos)
            {
                std::string var_name = modified_code.substr(out_decl + 9, semi - (out_decl + 9));
                var_name.erase(0, var_name.find_first_not_of(" \t\r\n"));
                var_name.erase(var_name.find_last_not_of(" \t\r\n") + 1);
                modified_code.erase(out_decl, semi - out_decl + 1);
                header += "#define " + var_name + " gl_FragColor\n";
            }
        }
        std::size_t in_decl = modified_code.find("in vec2 v_uv;");
        if (in_decl != std::string::npos)
        {
            modified_code.erase(in_decl, 13);
        }

        return header + modified_code;
    }

    // Default fallback wrapper
    return header + R"(
void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}
)";
}

bool GpuShaderRasterizer::compile(std::string_view glsl_code, std::string& out_error)
{
    m_active_code = glsl_code;
    out_error.clear();

    if (!m_initialized || !m_context)
    {
        if (!initialize(m_width, m_height))
        {
            out_error = m_last_error.empty() ? "OpenGL context is not available" : m_last_error;
            return false;
        }
    }

    m_context->make_current();

    if (!Graphics::initialize_opengl_functions())
    {
        out_error = "OpenGL drivers or function pointers are not available";
        m_last_error = out_error;
        return false;
    }

    const std::string full_fragment = build_full_fragment_shader(glsl_code);
    const bool success = m_shader.compile_from_source(VertexShaderSource, full_fragment);
    if (!success)
    {
        out_error = m_shader.get_info_log();
        m_last_error = out_error;
        m_shader.destroy();
        return false;
    }

    m_last_error.clear();
    return true;
}

void GpuShaderRasterizer::ensure_fbo(int width, int height)
{
    if (m_context)
    {
        m_context->make_current();
    }

    if (!Graphics::has_active_gl_context())
    {
        return;
    }

    auto& gl = Graphics::get_gl_api();
    if (gl.GenFramebuffers == nullptr || gl.BindFramebuffer == nullptr)
    {
        return;
    }

    if (m_fbo == 0)
    {
        gl.GenFramebuffers(1, &m_fbo);
    }
    if (m_color_texture == 0)
    {
        glGenTextures(1, &m_color_texture);
    }

    if (m_fbo_width != width || m_fbo_height != height)
    {
        m_fbo_width = width;
        m_fbo_height = height;

        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glBindTexture(GL_TEXTURE_2D, m_color_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (gl.FramebufferTexture2D != nullptr)
        {
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_texture, 0);
        }

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void GpuShaderRasterizer::destroy_fbo()
{
    if (m_context)
    {
        m_context->make_current();
    }

    if (!Graphics::has_active_gl_context())
    {
        m_color_texture = 0;
        m_fbo = 0;
        m_channel_textures.fill(0);
        m_loaded_channel_kinds.fill(ChannelTextureKind::Empty);
        m_loaded_channel_sizes.fill(0);
        m_fbo_width = 0;
        m_fbo_height = 0;
        return;
    }

    auto& gl = Graphics::get_gl_api();
    if (m_color_texture != 0)
    {
        glDeleteTextures(1, &m_color_texture);
        m_color_texture = 0;
    }
    if (m_fbo != 0)
    {
        if (gl.DeleteFramebuffers != nullptr)
        {
            gl.DeleteFramebuffers(1, &m_fbo);
        }
        m_fbo = 0;
    }
    for (auto& tex : m_channel_textures)
    {
        if (tex != 0)
        {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
    }
    m_loaded_channel_kinds.fill(ChannelTextureKind::Empty);
    m_loaded_channel_sizes.fill(0);
    m_fbo_width = 0;
    m_fbo_height = 0;
}

void GpuShaderRasterizer::update_channel_textures(const std::array<ShaderChannel, 4>& channels)
{
    if (!Graphics::has_active_gl_context())
    {
        return;
    }
    auto& gl = Graphics::get_gl_api();
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& ch = channels[i];
        if (ch.kind == ChannelTextureKind::Empty || ch.pixels.empty())
        {
            continue;
        }

        if (m_channel_textures[i] == 0)
        {
            glGenTextures(1, &m_channel_textures[i]);
            m_loaded_channel_kinds[i] = ChannelTextureKind::Empty;
            m_loaded_channel_sizes[i] = 0;
        }

        if (m_loaded_channel_kinds[i] != ch.kind || m_loaded_channel_sizes[i] != ch.pixels.size())
        {
            if (gl.ActiveTexture != nullptr)
            {
                gl.ActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(i));
            }
            glBindTexture(GL_TEXTURE_2D, m_channel_textures[i]);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA, ch.width, ch.height, 0,
                GL_BGRA_EXT, GL_UNSIGNED_BYTE, ch.pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            m_loaded_channel_kinds[i] = ch.kind;
            m_loaded_channel_sizes[i] = ch.pixels.size();
        }
    }
}

bool GpuShaderRasterizer::render(
    const ShaderUniforms& uniforms,
    const std::array<ShaderChannel, 4>& channels)
{
    if (!m_shader.is_valid())
    {
        return false;
    }

    if (!m_context)
    {
        return false;
    }
    m_context->make_current();

    if (!Graphics::has_active_gl_context())
    {
        return false;
    }

    auto& gl = Graphics::get_gl_api();
    if (gl.BindFramebuffer == nullptr)
    {
        return false;
    }

    ensure_fbo(m_width, m_height);

    gl.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    m_shader.bind();

    // Set standard uniforms
    m_shader.set_uniform_3f("iResolution", static_cast<float>(m_width), static_cast<float>(m_height), 1.0F);
    m_shader.set_uniform_2f("iResolution", static_cast<float>(m_width), static_cast<float>(m_height));
    m_shader.set_uniform_1f("iTime", uniforms.time);
    m_shader.set_uniform_1f("iTimeDelta", uniforms.time_delta);
    m_shader.set_uniform_1i("iFrame", uniforms.frame);
    m_shader.set_uniform_4f("iMouse", uniforms.mouse[0], uniforms.mouse[1], uniforms.mouse[2], uniforms.mouse[3]);
    m_shader.set_uniform_4f("iDate", uniforms.date[0], uniforms.date[1], uniforms.date[2], uniforms.date[3]);

    update_channel_textures(channels);
    for (int i = 0; i < 4; ++i)
    {
        const std::string name = "iChannel" + std::to_string(i);
        m_shader.set_uniform_1i(name, i);
    }

    // Render normalized full screen quad
    glBegin(GL_QUADS);
    glVertex2f(-1.0F, -1.0F);
    glVertex2f( 1.0F, -1.0F);
    glVertex2f( 1.0F,  1.0F);
    glVertex2f(-1.0F,  1.0F);
    glEnd();

    m_shader.unbind();

    // Read back rendered pixels into BGRA buffer and flip vertically to match top-down UI bitmap layout
    if (m_pixel_buffer.size() == static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height))
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, m_width, m_height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_pixel_buffer.data());

        const std::size_t stride = static_cast<std::size_t>(m_width);
        for (int y = 0; y < m_height / 2; ++y)
        {
            auto* top = m_pixel_buffer.data() + static_cast<std::size_t>(y) * stride;
            auto* bottom = m_pixel_buffer.data() + static_cast<std::size_t>(m_height - 1 - y) * stride;
            std::swap_ranges(top, top + stride, bottom);
        }
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void GpuShaderRasterizer::destroy()
{
    if (m_context)
    {
        m_context->make_current();
    }
    destroy_fbo();
    m_shader.destroy();
    m_pixel_buffer.clear();
    m_initialized = false;
    m_context_valid = false;
    if (m_context)
    {
        m_context->destroy();
    }
}

} // namespace Zenvra::Services::Shader
