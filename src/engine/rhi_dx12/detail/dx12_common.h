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

#include <mulan/core/log/log.h>
#include <mulan/core/result/error.h>
#include "../../rhi/engine_error_code.h"

#include <cstdint>
#include <cassert>
#include <string>

namespace mulan::engine {

inline std::string dx12SystemErrorMessage(HRESULT hr) {
    char message[512] = {};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                        static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message,
                                        static_cast<DWORD>(sizeof(message)), nullptr);
    return length > 0 ? std::string(message, length) : std::string();
}

[[nodiscard]] inline Result<void> checkDX12(HRESULT hr, std::string_view operation,
                                            EngineErrorCode errorCode = EngineErrorCode::ResourceCreateFailed,
                                            std::source_location where = std::source_location::current()) {
    if (SUCCEEDED(hr))
        return {};

    const std::string systemMessage = dx12SystemErrorMessage(hr);
    const std::string message = std::string("[DX12] ") + std::string(operation) + " failed (HRESULT=0x" +
                                std::format("{:08X}", static_cast<unsigned>(hr)) + ")" +
                                (systemMessage.empty() ? "" : ": " + systemMessage);
    LOG_ERROR("{}", message);
    return std::unexpected(makeError(errorCode, message, where));
}

}  // namespace mulan::engine

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
