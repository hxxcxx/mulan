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

#include <mulan/core/log/log.h>
#include <mulan/core/result/error.h>
#include "../../rhi/engine_error_code.h"

#include <cstdint>
#include <cassert>
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

[[nodiscard]] inline core::Result<void> checkDX11(HRESULT hr, std::string_view operation,
                                                  EngineErrorCode errorCode = EngineErrorCode::ResourceCreateFailed,
                                                  std::source_location where = std::source_location::current()) {
    if (SUCCEEDED(hr))
        return {};

    const std::string systemMessage = dx11SystemErrorMessage(hr);
    const std::string message = std::string("[DX11] ") + std::string(operation) + " failed (HRESULT=0x" +
                                std::format("{:08X}", static_cast<unsigned>(hr)) + ")" +
                                (systemMessage.empty() ? "" : ": " + systemMessage);
    LOG_ERROR("{}", message);
    return std::unexpected(makeError(errorCode, message, where));
}

inline void logDX11Failure(HRESULT hr, const char* operation) {
    const std::string systemMessage = dx11SystemErrorMessage(hr);
    LOG_ERROR("[DX11] {} failed (HRESULT=0x{:08X}){}{}", operation, static_cast<unsigned>(hr),
              systemMessage.empty() ? "" : ": ", systemMessage);
}

}  // namespace mulan::engine
