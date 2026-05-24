/**
 * @file DX12Common.h
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

// D3D12 调试层检查
#ifdef _DEBUG
#define DX12_CHECK(hr)                                                                  \
    do {                                                                                \
        HRESULT _dx12_hr = (hr);                                                        \
        if (FAILED(_dx12_hr)) {                                                         \
            char _dx12_msg[512] = {};                                                   \
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,  \
                           nullptr, static_cast<DWORD>(_dx12_hr),                       \
                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),                   \
                           _dx12_msg, static_cast<DWORD>(sizeof(_dx12_msg)), nullptr);  \
            fprintf(stderr, "[DX12 ERROR] HRESULT=0x%08X at %s:%d %s\n",              \
                    static_cast<unsigned>(_dx12_hr), __FILE__, __LINE__, _dx12_msg);    \
        }                                                                               \
    } while (0)
#define DX12_LOG(...)                                                                   \
    do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define DX12_CHECK(hr) (void)(hr)
#define DX12_LOG(...) do {} while (0)
#endif

namespace mulan::engine {

using Microsoft::WRL::ComPtr;

/// 安全释放 COM 指针
template<typename T>
inline void safeRelease(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

/// 安全释放 ComPtr（reset 即可）
template<typename T>
inline void safeRelease(ComPtr<T>& ptr) {
    ptr.Reset();
}

} // namespace mulan::Engine
