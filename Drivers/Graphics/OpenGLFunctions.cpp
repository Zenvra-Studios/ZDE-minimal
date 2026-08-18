#include "Drivers/Graphics/OpenGLFunctions.h"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>

namespace {
void ensure_win32_bootstrap_context() {
    if (wglGetCurrentContext() != nullptr) {
        return;
    }
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = hInst;
    wc.lpszClassName = "ZDE_GL_Bootstrap_Host";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, "ZDE_GL_Bootstrap_Host", "ZDE_GL_Bootstrap",
        WS_POPUP, 0, 0, 16, 16,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        return;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        DestroyWindow(hwnd);
        return;
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
    if (format != 0 && SetPixelFormat(hdc, format, &pfd)) {
        HGLRC hglrc = wglCreateContext(hdc);
        if (hglrc) {
            wglMakeCurrent(hdc, hglrc);
        }
    }
}
} // namespace
#elif defined(__linux__)
#include <GL/glx.h>
#include <dlfcn.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace Zenvra::Graphics {

namespace {

void* get_gl_proc_address(const char* name) {
#if defined(_WIN32)
    ensure_win32_bootstrap_context();
    void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (proc == nullptr || proc == reinterpret_cast<void*>(0x1) ||
        proc == reinterpret_cast<void*>(0x2) || proc == reinterpret_cast<void*>(0x3) ||
        proc == reinterpret_cast<void*>(-1)) {
        HMODULE module = GetModuleHandleA("opengl32.dll");
        if (module != nullptr) {
            proc = reinterpret_cast<void*>(GetProcAddress(module, name));
        }
    }
    return proc;
#elif defined(__linux__)
    void* proc = reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
    if (proc == nullptr) {
        void* libgl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        if (libgl != nullptr) {
            proc = dlsym(libgl, name);
        }
    }
    return proc;
#elif defined(__APPLE__)
    void* libgl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_GLOBAL);
    if (libgl != nullptr) {
        return dlsym(libgl, name);
    }
    return nullptr;
#else
    return nullptr;
#endif
}

template <typename T>
void load_gl_func(T& func_ptr, const char* name) {
    func_ptr = reinterpret_cast<T>(get_gl_proc_address(name));
}

OpenGLApi g_gl_api{};
bool g_initialized = false;

} // namespace

OpenGLApi& get_gl_api() noexcept {
    return g_gl_api;
}

bool initialize_opengl_functions() noexcept {
    if (g_initialized) {
        return true;
    }

    load_gl_func(g_gl_api.CreateShader, "glCreateShader");
    load_gl_func(g_gl_api.ShaderSource, "glShaderSource");
    load_gl_func(g_gl_api.CompileShader, "glCompileShader");
    load_gl_func(g_gl_api.GetShaderiv, "glGetShaderiv");
    load_gl_func(g_gl_api.GetShaderInfoLog, "glGetShaderInfoLog");
    load_gl_func(g_gl_api.DeleteShader, "glDeleteShader");
    load_gl_func(g_gl_api.CreateProgram, "glCreateProgram");
    load_gl_func(g_gl_api.AttachShader, "glAttachShader");
    load_gl_func(g_gl_api.LinkProgram, "glLinkProgram");
    load_gl_func(g_gl_api.GetProgramiv, "glGetProgramiv");
    load_gl_func(g_gl_api.GetProgramInfoLog, "glGetProgramInfoLog");
    load_gl_func(g_gl_api.UseProgram, "glUseProgram");
    load_gl_func(g_gl_api.DeleteProgram, "glDeleteProgram");
    load_gl_func(g_gl_api.GetUniformLocation, "glGetUniformLocation");
    load_gl_func(g_gl_api.Uniform1i, "glUniform1i");
    load_gl_func(g_gl_api.Uniform1f, "glUniform1f");
    load_gl_func(g_gl_api.Uniform2f, "glUniform2f");
    load_gl_func(g_gl_api.Uniform3f, "glUniform3f");
    load_gl_func(g_gl_api.Uniform4f, "glUniform4f");
    load_gl_func(g_gl_api.ActiveTexture, "glActiveTexture");
    load_gl_func(g_gl_api.GenFramebuffers, "glGenFramebuffers");
    load_gl_func(g_gl_api.BindFramebuffer, "glBindFramebuffer");
    load_gl_func(g_gl_api.FramebufferTexture2D, "glFramebufferTexture2D");
    load_gl_func(g_gl_api.CheckFramebufferStatus, "glCheckFramebufferStatus");
    load_gl_func(g_gl_api.DeleteFramebuffers, "glDeleteFramebuffers");
    load_gl_func(g_gl_api.GenBuffers, "glGenBuffers");
    load_gl_func(g_gl_api.BindBuffer, "glBindBuffer");
    load_gl_func(g_gl_api.BufferData, "glBufferData");
    load_gl_func(g_gl_api.DeleteBuffers, "glDeleteBuffers");
    load_gl_func(g_gl_api.EnableVertexAttribArray, "glEnableVertexAttribArray");
    load_gl_func(g_gl_api.DisableVertexAttribArray, "glDisableVertexAttribArray");
    load_gl_func(g_gl_api.VertexAttribPointer, "glVertexAttribPointer");

    g_initialized = (g_gl_api.CreateShader != nullptr &&
                     g_gl_api.CompileShader != nullptr &&
                     g_gl_api.CreateProgram != nullptr &&
                     g_gl_api.LinkProgram != nullptr &&
                     g_gl_api.UseProgram != nullptr);

    return g_initialized;
}

bool is_opengl_shader_supported() noexcept {
    if (!g_initialized) {
        initialize_opengl_functions();
    }
    return g_initialized;
}

} // namespace Zenvra::Graphics
