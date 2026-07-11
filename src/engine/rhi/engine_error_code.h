/**
 * @file engine_error_code.h
 * @brief 引擎层错误码 + Error 工厂函数
 *
 * 各模块自定义错误码范围:
 *   core:     0 ~ 999   (ErrorCode)
 *   engine:   1000 ~ 1999 (EngineErrorCode)
 *   io:       2000 ~ 2999
 *   document: 3000 ~ 3999
 * @author hxxcxx
 */

#pragma once

#include <mulan/core/result/error.h>

#include <source_location>
#include <string_view>

namespace mulan::engine {

enum class EngineErrorCode : int32_t {
    // --- 通用设备/后端（1000~1006）---
    DeviceLost = 1000,
    OutOfDeviceMemory = 1001,
    ShaderCompileFailed = 1002,
    PipelineCreateFailed = 1003,
    BackendNotSupported = 1004,
    AdapterNotFound = 1005,
    ResourceCreateFailed = 1006,

    // --- 资源创建细分（1007~1013）---
    BufferCreateFailed = 1007,
    TextureCreateFailed = 1008,
    CommandListCreateFailed = 1009,
    SwapChainCreateFailed = 1010,
    RenderTargetCreateFailed = 1011,
    SamplerCreateFailed = 1012,
    FenceCreateFailed = 1013,

    // --- 失败原因细分（1014~1016）---
    ShaderFileNotFound = 1014,   // 着色器文件读取失败（区别于编译/链接失败）
    SurfaceNotSupported = 1015,  // 平台 surface / 呈现格式后端不支持
    FormatNotSupported = 1016,   // 纹理/渲染目标格式后端不支持
};

inline core::Error makeError(EngineErrorCode code, std::string_view msg,
                             std::source_location loc = std::source_location::current()) {
    return core::Error::make(static_cast<int32_t>(code), msg, loc);
}

}  // namespace mulan::engine
