#pragma once

#include "../CoreExport.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace MulanGeo::core::log {

// ============================================================
// 配置
// ============================================================

struct CORE_API Config {
    std::string_view loggerName    = "mulan";
    std::string_view logDir        = "logs";
    int32_t          maxFileSize   = 10 * 1024 * 1024;  // 10 MB
    int32_t          maxFiles      = 5;
    bool             enableConsole = true;
    bool             enableFile    = true;
    bool             enableMSVC    = true;   // VS OutputDebugString
    bool             asyncMode     = true;
    bool             flushOnCritical = true;
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

enum class Level : uint8_t {
    Trace    = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

CORE_API void setLevel(Level lvl);
CORE_API void setFlushLevel(Level lvl);

// ============================================================
// 日志输出
// ============================================================

/// 纯文本日志
CORE_API void log(Level lvl, std::string_view msg);

constexpr spdlog::level::level_enum kLevelMap[] = {
    spdlog::level::trace,
    spdlog::level::debug,
    spdlog::level::info,
    spdlog::level::warn,
    spdlog::level::err,
    spdlog::level::critical,
    spdlog::level::off
};

inline spdlog::level::level_enum toSpdlogLevel(Level lvl) {
    auto idx = static_cast<size_t>(lvl);
    assert(idx < std::size(kLevelMap));
    return kLevelMap[idx];
}
/// 格式化日志（模板转发至 spdlog，编译期检查格式串，自动捕获调用位置）
template <typename... Args>
void logf(Level lvl, spdlog::format_string_t<Args...> fmt, Args&&... args,
          std::source_location loc = std::source_location::current()) {
    if (auto lgr = spdlog::default_logger_raw()) {
        spdlog::source_loc src{loc.file_name(), static_cast<int>(loc.line()),
                               loc.function_name()};
        lgr->log(src, toSpdlogLevel(lvl), fmt, std::forward<Args>(args)...);
    }
}

} // namespace MulanGeo::core::Log

// ============================================================
// 便捷宏
// ============================================================

#define LOG_TRACE(...)    ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Trace,    __VA_ARGS__)
#define LOG_DEBUG(...)    ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Debug,    __VA_ARGS__)
#define LOG_INFO(...)     ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Info,     __VA_ARGS__)
#define LOG_WARN(...)     ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Warn,     __VA_ARGS__)
#define LOG_ERROR(...)    ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Error,    __VA_ARGS__)
#define LOG_CRITICAL(...) ::MulanGeo::core::Log::logf(::MulanGeo::core::Log::Level::Critical, __VA_ARGS__)
