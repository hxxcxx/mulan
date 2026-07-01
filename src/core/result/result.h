/**
 * @file result.h
 * @brief Error 类型 + 便捷宏，配合 std::expected<T, Error> 使用
 *
 * 使用边界:
 *   适合: 文件导入/导出、用户触发的 IO 操作、需要向 UI 层报告失败原因的接口
 *   不适合: GPU 资源创建（失败即致命，用 null）、内部断言（用 assert）、热路径渲染循环
 *
 * 判断标准: 调用方能否根据错误做不同决策？
 *   能  → 用 std::expected<T, Error>
 *   不能 → 用 null / assert / fprintf
 */

#pragma once

#include "../core_export.h"

#include <expected>
#include <source_location>
#include <string>
#include <string_view>

namespace mulan::core {

// ============================================================
// Error — 携带错误码、消息、源码位置的值类型
// ============================================================

struct CORE_API Error {
    int32_t     code    = 0;
    std::string message;
    const char* file    = nullptr;
    uint32_t    line    = 0;

    static Error make(std::string_view msg,
                      std::source_location loc = std::source_location::current());

    static Error make(int32_t code, std::string_view msg,
                      std::source_location loc = std::source_location::current());
};

} // namespace mulan::core

// ============================================================
// 便捷宏
// ============================================================

#define CORE_ERR(...)       ::mulan::core::Error::make(__VA_ARGS__)
#define CORE_ERR_CODE(c, ...) ::mulan::core::Error::make((c), __VA_ARGS__)
