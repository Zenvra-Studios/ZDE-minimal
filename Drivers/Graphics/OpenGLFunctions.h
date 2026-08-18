#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace Zenvra::Graphics {

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_ACTIVE_UNIFORMS
#define GL_ACTIVE_UNIFORMS 0x8B86
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

// Function pointer signatures for modern OpenGL features
using PFNGLCREATESHADERPROC = unsigned int (*)(unsigned int type);
using PFNGLSHADERSOURCEPROC = void (*)(unsigned int shader, int count, const char* const* string, const int* length);
using PFNGLCOMPILESHADERPROC = void (*)(unsigned int shader);
using PFNGLGETSHADERIVPROC = void (*)(unsigned int shader, unsigned int pname, int* params);
using PFNGLGETSHADERINFOLOGPROC = void (*)(unsigned int shader, int bufSize, int* length, char* infoLog);
using PFNGLDELETESHADERPROC = void (*)(unsigned int shader);
using PFNGLCREATEPROGRAMPROC = unsigned int (*)();
using PFNGLATTACHSHADERPROC = void (*)(unsigned int program, unsigned int shader);
using PFNGLLINKPROGRAMPROC = void (*)(unsigned int program);
using PFNGLGETPROGRAMIVPROC = void (*)(unsigned int program, unsigned int pname, int* params);
using PFNGLGETPROGRAMINFOLOGPROC = void (*)(unsigned int program, int bufSize, int* length, char* infoLog);
using PFNGLUSEPROGRAMPROC = void (*)(unsigned int program);
using PFNGLDELETEPROGRAMPROC = void (*)(unsigned int program);
using PFNGLGETUNIFORMLOCATIONPROC = int (*)(unsigned int program, const char* name);
using PFNGLUNIFORM1IPROC = void (*)(int location, int v0);
using PFNGLUNIFORM1FPROC = void (*)(int location, float v0);
using PFNGLUNIFORM2FPROC = void (*)(int location, float v0, float v1);
using PFNGLUNIFORM3FPROC = void (*)(int location, float v0, float v1, float v2);
using PFNGLUNIFORM4FPROC = void (*)(int location, float v0, float v1, float v2, float v3);
using PFNGLACTIVETEXTUREPROC = void (*)(unsigned int texture);
using PFNGLGENFRAMEBUFFERSPROC = void (*)(int n, unsigned int* framebuffers);
using PFNGLBINDFRAMEBUFFERPROC = void (*)(unsigned int target, unsigned int framebuffer);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void (*)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = unsigned int (*)(unsigned int target);
using PFNGLDELETEFRAMEBUFFERSPROC = void (*)(int n, const unsigned int* framebuffers);
using PFNGLGENBUFFERSPROC = void (*)(int n, unsigned int* buffers);
using PFNGLBINDBUFFERPROC = void (*)(unsigned int target, unsigned int buffer);
using PFNGLBUFFERDATAPROC = void (*)(unsigned int target, std::ptrdiff_t size, const void* data, unsigned int usage);
using PFNGLDELETEBUFFERSPROC = void (*)(int n, const unsigned int* buffers);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (*)(unsigned int index);
using PFNGLDISABLEVERTEXATTRIBARRAYPROC = void (*)(unsigned int index);
using PFNGLVERTEXATTRIBPOINTERPROC = void (*)(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void* pointer);

struct OpenGLApi {
    PFNGLCREATESHADERPROC CreateShader = nullptr;
    PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC CompileShader = nullptr;
    PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
    PFNGLDELETESHADERPROC DeleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
    PFNGLATTACHSHADERPROC AttachShader = nullptr;
    PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
    PFNGLUSEPROGRAMPROC UseProgram = nullptr;
    PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC Uniform1i = nullptr;
    PFNGLUNIFORM1FPROC Uniform1f = nullptr;
    PFNGLUNIFORM2FPROC Uniform2f = nullptr;
    PFNGLUNIFORM3FPROC Uniform3f = nullptr;
    PFNGLUNIFORM4FPROC Uniform4f = nullptr;
    PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
    PFNGLGENBUFFERSPROC GenBuffers = nullptr;
    PFNGLBINDBUFFERPROC BindBuffer = nullptr;
    PFNGLBUFFERDATAPROC BufferData = nullptr;
    PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
};

/// Get the global OpenGL API function table.
OpenGLApi& get_gl_api() noexcept;

/// Initialize dynamic OpenGL function pointers for the active context.
bool initialize_opengl_functions() noexcept;

/// Checks whether the OpenGL shader pipeline is supported and loaded.
bool is_opengl_shader_supported() noexcept;

} // namespace Zenvra::Graphics
