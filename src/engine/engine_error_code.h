/**
 * @file engine_error_code.h
 * @brief 引擎层错误码 + Error 工厂函数
 *
 * 各模块自定义错误码范围:
 *   core:     0 ~ 999   (ErrorCode)
 *   engine:   1000 ~ 1999 (EngineErrorCode)
 *   io:       2000 ~ 2999
 *   document: 3000 ~ 3999
 */

#pragma once

#include <mulan/core/result/error.h>

#include <source_location>
#include <string_view>

namespace mulan::engine {

enum class EngineErrorCode : int32_t {
    DeviceLost           = 1000,
    OutOfDeviceMemory    = 1001,
    ShaderCompileFailed  = 1002,
    PipelineCreateFailed = 1003,
    BackendNotSupported  = 1004,
    AdapterNotFound      = 1005,
    ResourceCreateFailed = 1006,
};

inline core::Error makeError(EngineErrorCode code, std::string_view msg,
                             std::source_location loc = std::source_location::current()) {
    return core::Error::make(static_cast<int32_t>(code), msg, loc);
}

} // namespace mulan::engine
