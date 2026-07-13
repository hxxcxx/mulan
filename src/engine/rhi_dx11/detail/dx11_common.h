/**
 * @file dx11_common.h
 * @brief D3D11 后端公共头文件，统一 include 与工具类型
 * @author zmb
 * @date 2026-04-19
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <string>

namespace mulan::engine {

using Microsoft::WRL::ComPtr;

inline std::string dx11SystemErrorMessage(HRESULT hr) {
    char message[512] = {};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                        static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message,
                                        static_cast<DWORD>(sizeof(message)), nullptr);
    return length > 0 ? std::string(message, length) : std::string();
}

[[noreturn]] inline void throwDX11Failure(HRESULT hr, const char* file, int line) {
    char prefix[256] = {};
    std::snprintf(prefix, sizeof(prefix), "[DX11 ERROR] HRESULT=0x%08X at %s:%d", static_cast<unsigned>(hr), file,
                  line);
    const std::string systemMessage = dx11SystemErrorMessage(hr);
    throw std::runtime_error(systemMessage.empty() ? std::string(prefix) : std::string(prefix) + " " + systemMessage);
}

inline void logDX11Failure(HRESULT hr, const char* operation) {
    const std::string systemMessage = dx11SystemErrorMessage(hr);
    std::fprintf(stderr, "[DX11] %s failed (HRESULT=0x%08X)%s%s\n", operation, static_cast<unsigned>(hr),
                 systemMessage.empty() ? "" : ": ", systemMessage.c_str());
}

}  // namespace mulan::engine

// 创建资源失败必须传递到 Device 工厂，不能只在 Debug 输出后继续使用空句柄。
#define DX11_CHECK(expression)                                               \
    do {                                                                     \
        const HRESULT _dx11_hr = (expression);                               \
        if (FAILED(_dx11_hr)) {                                              \
            ::mulan::engine::throwDX11Failure(_dx11_hr, __FILE__, __LINE__); \
        }                                                                    \
    } while (0)
