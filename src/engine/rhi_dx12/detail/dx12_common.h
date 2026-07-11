/**
 * @file dx12_common.h
 * @brief D3D12 后端公共头文件，统一 include 与工具类型
 * @author hxxcxx
 * @date 2026-04-18
 */

#pragma once

// Windows 头文件最小化
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <cassert>
#include <stdexcept>

// D3D12 检查：失败时抛异常，Debug 版附带系统错误消息
#ifdef _DEBUG
#define DX12_CHECK(hr)                                                                                               \
    do {                                                                                                             \
        HRESULT _dx12_hr = (hr);                                                                                     \
        if (FAILED(_dx12_hr)) {                                                                                      \
            char _msg[512] = {};                                                                                     \
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,                      \
                           static_cast<DWORD>(_dx12_hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), _msg,            \
                           static_cast<DWORD>(sizeof(_msg)), nullptr);                                               \
            char _buf[640];                                                                                          \
            snprintf(_buf, sizeof(_buf), "[DX12 ERROR] HRESULT=0x%08X at %s:%d %s", static_cast<unsigned>(_dx12_hr), \
                     __FILE__, __LINE__, _msg);                                                                      \
            throw std::runtime_error(_buf);                                                                          \
        }                                                                                                            \
    } while (0)
#else
#define DX12_CHECK(hr)                                                                                    \
    do {                                                                                                  \
        HRESULT _dx12_hr = (hr);                                                                          \
        if (FAILED(_dx12_hr)) {                                                                           \
            char _buf[128];                                                                               \
            snprintf(_buf, sizeof(_buf), "[DX12 ERROR] HRESULT=0x%08X", static_cast<unsigned>(_dx12_hr)); \
            throw std::runtime_error(_buf);                                                               \
        }                                                                                                 \
    } while (0)
#endif

#define DX12_LOG(...)                 \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

namespace mulan::engine {

using Microsoft::WRL::ComPtr;

/// 安全释放 COM 指针
template <typename T>
inline void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

/// 安全释放 ComPtr（reset 即可）
template <typename T>
inline void safeRelease(ComPtr<T>& ptr) {
    ptr.Reset();
}

}  // namespace mulan::engine
