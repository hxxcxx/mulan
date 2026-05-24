/**
 * @file Result.h
 * @brief Result<T> — 基于 tl::expected 的错误传播机制
 *
 * 使用边界:
 *   适合: 文件导入/导出、用户触发的 IO 操作、需要向 UI 层报告失败原因的接口
 *   不适合: GPU 资源创建（失败即致命，用 null）、内部断言（用 assert）、热路径渲染循环
 *
 * 判断标准: 调用方能否根据错误做不同决策？
 *   能  → 用 Result<T>（弹对话框、重试、换格式）
 *   不能 → 用 null / assert / fprintf
 */

#pragma once

#include "../CoreExport.h"

#include <source_location>
#include <string>
#include <string_view>

#include <tl/expected.hpp>

namespace MulanGeo::core {

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

// ============================================================
// Result<T> — tl::expected 别名
// ============================================================

template<typename T>
using Result = tl::expected<T, Error>;

// ============================================================
// 工厂函数
// ============================================================

template<typename T>
Result<std::remove_cvref_t<T>> Ok(T&& value) {
    return Result<std::remove_cvref_t<T>>(std::forward<T>(value));
}

inline Result<void> Ok() {
    return Result<void>();
}

template<typename T>
Result<T> Err(Error err) {
    return tl::make_unexpected(std::move(err));
}

} // namespace MulanGeo::Core

// ============================================================
// 便捷宏
// ============================================================

#define CORE_ERR(...)    ::MulanGeo::Core::Error::make(__VA_ARGS__)
#define CORE_ERR_CODE(c, ...) ::MulanGeo::Core::Error::make((c), __VA_ARGS__)

/// 传播错误：失败时直接 return，成功时绑定值
#define PROPAGATE(var_name, expr)                                      \
    auto var_name = (expr);                                            \
    if (!var_name) [[unlikely]]                                        \
        return ::tl::make_unexpected(std::move(var_name.error()))

/// 传播错误（不绑定值）：用于 Result<void> 或不需要值的场景
#define TRY(expr)                                                      \
    do {                                                               \
        auto&& _r_ = (expr);                                           \
        if (!_r_) [[unlikely]]                                         \
            return ::tl::make_unexpected(std::move(_r_.error()));      \
    } while (0)
