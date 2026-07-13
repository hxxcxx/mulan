#include "log.h"

#include <spdlog/async.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cassert>
#include <cstdio>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace mulan::core::log {
namespace {

std::once_flag g_initFlag;
std::shared_mutex g_loggerMutex;
std::shared_ptr<spdlog::logger> g_logger;
std::shared_ptr<spdlog::details::thread_pool> g_threadPool;
bool g_initialized = false;  // 仅在 g_loggerMutex 保护下访问

// Level → spdlog 级别的映射，仅在本 .cpp 内可见。
constexpr spdlog::level::level_enum kLevelMap[] = { spdlog::level::trace, spdlog::level::debug, spdlog::level::info,
                                                    spdlog::level::warn,  spdlog::level::err,   spdlog::level::critical,
                                                    spdlog::level::off };

inline spdlog::level::level_enum toSpdlogLevel(Level lvl) {
    const auto idx = static_cast<size_t>(lvl);
    assert(idx < std::size(kLevelMap));
    return kLevelMap[idx];
}

void reportInitError(const char* message) noexcept {
    constexpr const char* prefix = "mulan log init skipped: ";
    ::OutputDebugStringA(prefix);
    ::OutputDebugStringA(message);
    ::OutputDebugStringA("\n");
    std::fputs(prefix, stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
}

bool isValidConfig(const Config& cfg) noexcept {
    if (!cfg.enableConsole && !cfg.enableFile && !cfg.enableMSVC) {
        reportInitError("at least one log sink must be enabled");
        return false;
    }
    if (cfg.enableFile && cfg.logDir.empty()) {
        reportInitError("log directory must not be empty when file logging is enabled");
        return false;
    }
    if (cfg.enableFile && cfg.maxFileSize <= 0) {
        reportInitError("log file size must be positive");
        return false;
    }
    if (cfg.enableFile && cfg.maxFiles <= 0) {
        reportInitError("log file count must be positive");
        return false;
    }
    return true;
}

// 同步 logger 会从多个调用线程进入每个 sink；自定义 sink 自己必须加锁。
class MsvcSink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg& msg) override {
        std::lock_guard lock(mutex_);
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        buf.push_back('\0');
        OutputDebugStringA(buf.data());
    }

    void flush() override {}

    void set_pattern(const std::string& pattern) override {
        std::lock_guard lock(mutex_);
        formatter_ = std::make_unique<spdlog::pattern_formatter>(pattern);
    }

    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
        std::lock_guard lock(mutex_);
        formatter_ = std::move(formatter);
    }

private:
    std::mutex mutex_;
    std::unique_ptr<spdlog::formatter> formatter_ =
            std::make_unique<spdlog::pattern_formatter>("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
};

}  // namespace

void detail::submit(Level lvl, std::source_location loc, std::string_view fmt, std::format_args args) {
    // shared_lock 覆盖整个 log 调用：shutdown 取得 unique_lock 后才能销毁 logger/thread pool。
    std::shared_lock lock(g_loggerMutex);
    const auto logger = g_logger;
    if (!logger)
        return;

    const auto level = toSpdlogLevel(lvl);
    if (level < logger->level())
        return;

    const std::string formatted = std::vformat(fmt, args);
    const spdlog::source_loc source{ loc.file_name(), static_cast<int>(loc.line()), loc.function_name() };
    logger->log(source, level, spdlog::string_view_t(formatted.data(), formatted.size()));
}

void init(const Config& cfg) {
    // 校验放在 call_once 前：传错配置不会抛异常，也不会消耗初始化机会。
    if (!isValidConfig(cfg))
        return;

    try {
        std::call_once(g_initFlag, [&]() {
            std::unique_lock lock(g_loggerMutex);
            std::vector<spdlog::sink_ptr> sinks;

            if (cfg.enableConsole) {
                auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v");
                sinks.push_back(console);
            }

            if (cfg.enableFile) {
                auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        std::string(cfg.logDir) + "/mulan.log", static_cast<size_t>(cfg.maxFileSize),
                        static_cast<size_t>(cfg.maxFiles));
                file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
                sinks.push_back(file);
            }

            if (cfg.enableMSVC) {
                auto msvc = std::make_shared<MsvcSink>();
                msvc->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
                sinks.push_back(msvc);
            }

            std::shared_ptr<spdlog::logger> logger;
            if (cfg.asyncMode) {
                // 线程池只属于本模块，不占用也不关闭 spdlog 的全局线程池。
                g_threadPool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
                logger = std::make_shared<spdlog::async_logger>(std::string(cfg.loggerName), sinks.begin(), sinks.end(),
                                                                g_threadPool, spdlog::async_overflow_policy::block);
            } else {
                logger = std::make_shared<spdlog::logger>(std::string(cfg.loggerName), sinks.begin(), sinks.end());
            }

            logger->set_level(toSpdlogLevel(cfg.logLevel));
            logger->flush_on(toSpdlogLevel(cfg.flushLevel));
            logger->set_error_handler(
                    [](const std::string& err) { ::OutputDebugStringA(("spdlog error: " + err + "\n").c_str()); });

            // 不注册为 spdlog 的 default logger，避免与项目中其他 spdlog 使用者互相覆盖。
            g_logger = std::move(logger);
            g_initialized = true;
        });
    } catch (const spdlog::spdlog_ex& error) {
        // 例如目录不可写。保持未初始化状态，后续可修正配置并重试。
        std::unique_lock lock(g_loggerMutex);
        g_logger.reset();
        g_threadPool.reset();
        reportInitError(error.what());
    }
}

void shutdown() {
    // 生命周期约定：init 一次，shutdown 仅用于进程退出阶段；调用前先停止业务工作线程。
    std::unique_lock lock(g_loggerMutex);
    if (g_logger)
        g_logger->flush();
    g_logger.reset();
    g_threadPool.reset();
    g_initialized = false;
}

bool isInitialized() {
    std::shared_lock lock(g_loggerMutex);
    return g_initialized;
}

void setLevel(Level lvl) {
    std::shared_lock lock(g_loggerMutex);
    if (g_logger)
        g_logger->set_level(toSpdlogLevel(lvl));
}

void setFlushLevel(Level lvl) {
    std::shared_lock lock(g_loggerMutex);
    if (g_logger)
        g_logger->flush_on(toSpdlogLevel(lvl));
}

void log(Level lvl, std::string_view msg) {
    std::shared_lock lock(g_loggerMutex);
    if (g_logger)
        g_logger->log(toSpdlogLevel(lvl), msg);
}

}  // namespace mulan::core::log
