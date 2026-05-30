/**
 * @file GLCommon.h
 * @brief OpenGL 公共头文件，统一 include 与工具宏
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// GLAD must be included before any OpenGL headers
// Emscripten: 使用系统 GLES3 头文件，无需 GLAD
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

// WGL_ARB_create_context constants (not provided by vcpkg glad)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001
#define WGL_CONTEXT_DEBUG_BIT_ARB         0x00000001
#endif
typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
#endif

#include <cstdio>
#include <cstdlib>

namespace mulan::engine {

/// 检查 OpenGL 错误（Debug 模式下使用）
inline void glCheckError(const char* file, int line) {
#ifdef _DEBUG
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        const char* errStr = "UNKNOWN";
        switch (err) {
        case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
        case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
        case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
        case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        std::fprintf(stderr, "[GL ERROR] %s at %s:%d\n", errStr, file, line);
    }
#endif
}

#define GL_CHECK() ::mulan::engine::glCheckError(__FILE__, __LINE__)

} // namespace mulan::engine
