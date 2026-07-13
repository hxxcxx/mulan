#pragma once

#include "../core_export.h"

#include <cstdint>
#include <format>
#include <source_location>
#include <string_view>

namespace mulan::core::log {

// ============================================================
// 级别
// ============================================================

enum class Level : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

// ============================================================
// 配置
// ============================================================

struct CORE_API Config {
    std::string_view loggerName = "mulan";
    std::string_view logDir = "logs";
    int32_t maxFileSize = 10 * 1024 * 1024;  // 10 MB
    int32_t maxFiles = 5;
    bool enableConsole = true;
    bool enableFile = true;
    bool enableMSVC = true;          // VS OutputDebugString
    bool asyncMode = true;
    Level logLevel = Level::Info;    // 默认输出级别
    Level flushLevel = Level::Warn;  // 达到该级别立即落盘
};

// ============================================================
// 生命周期
// ============================================================

CORE_API void init(const Config& cfg = Config{});
CORE_API void shutdown();
CORE_API bool isInitialized();

// ============================================================
// 运行时控制
// ============================================================

CORE_API void setLevel(Level lvl);
CORE_API void setFlushLevel(Level lvl);

// ============================================================
// 日志输出
// ============================================================

/// 纯文本日志（不做格式化）
CORE_API void log(Level lvl, std::string_view msg);

namespace detail {

/// 类型擦除的提交入口。
/// spdlog 类型对外完全不可见：格式化在本函数内完成后，只把 std::string 交给后端。
/// @note args 是调用方栈上参数的只读视图，本函数同步消费，不跨线程、不存储，
///       因此不会出现悬垂引用。
CORE_API void submit(Level lvl, std::source_location loc, std::string_view fmt, std::format_args args);

/// 带位置的格式化入口（source_location 在 format_string 之前，避免参数包末尾
/// 默认参数的推导歧义 —— fmt/spdlog/std::format 均采用此排列）。
template <typename... Args>
void log_with_loc(Level lvl, std::source_location loc, std::format_string<Args...> fmt, Args&&... args) {
    submit(lvl, loc, fmt.get(), std::make_format_args(args...));
}

}  // namespace detail

}  // namespace mulan::core::log

// ============================================================
// 便捷宏 —— source_location 由宏在调用点捕获后显式传入。
// ============================================================

#define LOG_TRACE(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Trace, std::source_location::current(), \
                                             __VA_ARGS__)
#define LOG_DEBUG(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Debug, std::source_location::current(), \
                                             __VA_ARGS__)
#define LOG_INFO(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Info, std::source_location::current(), \
                                             __VA_ARGS__)
#define LOG_WARN(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Warn, std::source_location::current(), \
                                             __VA_ARGS__)
#define LOG_ERROR(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Error, std::source_location::current(), \
                                             __VA_ARGS__)
#define LOG_CRITICAL(...)                                                                                          \
    ::mulan::core::log::detail::log_with_loc(::mulan::core::log::Level::Critical, std::source_location::current(), \
                                             __VA_ARGS__)
