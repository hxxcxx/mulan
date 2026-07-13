/**
 * @file gl_common.h
 * @brief OpenGL 公共头文件，统一 include 与工具宏
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// GLAD must be included before any OpenGL headers.
#include <glad/glad.h>
#include <mulan/core/log/log.h>

// WGL_ARB_create_context constants (not provided by vcpkg glad)
#if defined(_WIN32)
#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_FLAGS_ARB            0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_DEBUG_BIT_ARB        0x00000001
#endif
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
#endif

#include <cstdlib>
#include <cstring>

namespace mulan::engine {

inline bool glExtensionSupported(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && std::strcmp(extension, name) == 0)
            return true;
    }
    return false;
}

}  // namespace mulan::engine
