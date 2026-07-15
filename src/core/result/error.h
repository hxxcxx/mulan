/**
 * @file error.h
 * @brief Error 类型 + Result<T> 别名，配合 std::expected<T, Error> 使用
 *
 * 使用边界:
 *   适合: 初始化/加载、提交/等待、上传/回读等具备明确失败边界的操作
 *   不适合: draw/bind 等高频命令录制、内部不变量检查
 *
 * 判断标准: 调用方能否根据错误做不同决策？
 *   能  → 用 Result<T>（即 std::expected<T, Error>）
 *   不能 → 用 null / assert / fprintf
 */

#pragma once

#include "../core_export.h"

#include <expected>
#include <source_location>
#include <string>
#include <string_view>

namespace mulan {

// ============================================================
// ErrorCode — 通用错误码（0~999），各模块从 1000 起自定义
// ============================================================

enum class ErrorCode : int32_t {
    Generic = 0,
    NotFound = 1,
    InvalidArg = 2,
    Io = 3,
    OutOfMemory = 4,
    NotSupported = 5,
    Internal = 6,
};

// ============================================================
// Error — 携带错误码、消息、源码位置的值类型
// ============================================================

struct CORE_API Error {
    int32_t code = static_cast<int32_t>(ErrorCode::Generic);
    std::string message;
    const char* file = nullptr;
    uint32_t line = 0;

    static Error make(std::string_view msg, std::source_location loc = std::source_location::current());

    static Error make(ErrorCode code, std::string_view msg, std::source_location loc = std::source_location::current());

    static Error make(int32_t code, std::string_view msg, std::source_location loc = std::source_location::current());
};

// ============================================================
// Result<T> — std::expected<T, Error> 的模块内统一缩写
// 用 Result<T> 替代冗长的 std::expected<T, Error>；无返回值用 ResultVoid。
// ============================================================

template <typename T>
using Result = std::expected<T, Error>;

using ResultVoid = Result<void>;

}  // namespace mulan
