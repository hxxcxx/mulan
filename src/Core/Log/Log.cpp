#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>

#include <memory>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace mulan::core::log {
namespace {

std::once_flag g_initFlag;
bool           g_initialized = false;

// ----------------------------------------------------------
// MSVC Debug Output sink
// ----------------------------------------------------------

class MsvcSink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        buf.push_back('\0');
        OutputDebugStringA(buf.data());
    }

    void flush() override {}
    void set_pattern(const std::string& pattern) override {
        formatter_ = std::make_unique<spdlog::pattern_formatter>(pattern);
    }
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
        formatter_ = std::move(formatter);
    }

private:
    std::unique_ptr<spdlog::formatter> formatter_ =
        std::make_unique<spdlog::pattern_formatter>("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
};

} // anonymous namespace

// ============================================================
// 生命周期
// ============================================================

void init(const Config& cfg) {
    std::call_once(g_initFlag, [&]() {
        std::vector<spdlog::sink_ptr> sinks;

        if (cfg.enableConsole) {
            auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
            sinks.push_back(console);
        }

        if (cfg.enableFile) {
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                std::string(cfg.logDir) + "/mulan.log",
                static_cast<size_t>(cfg.maxFileSize),
                static_cast<size_t>(cfg.maxFiles));
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file);
        }

        if (cfg.enableMSVC) {
            auto msvc = std::make_shared<MsvcSink>();
            msvc->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            sinks.push_back(msvc);
        }

        if (sinks.empty()) return;

        std::shared_ptr<spdlog::logger> logger;

        if (cfg.asyncMode) {
            spdlog::init_thread_pool(8192, 1);
            logger = std::make_shared<spdlog::async_logger>(
                std::string(cfg.loggerName),
                sinks.begin(), sinks.end(),
                spdlog::thread_pool(),
                spdlog::async_overflow_policy::block);
        } else {
            logger = std::make_shared<spdlog::logger>(
                std::string(cfg.loggerName),
                sinks.begin(), sinks.end());
        }

        logger->set_level(toSpdlogLevel(cfg.flushOnCritical ? Level::Trace : Level::Info));
        logger->set_error_handler([](const std::string& err) {
            ::OutputDebugStringA(("spdlog error: " + err + "\n").c_str());
        });

        spdlog::set_default_logger(logger);
        g_initialized = true;
    });
}

void shutdown() {
    spdlog::shutdown();
    g_initialized = false;
}

bool isInitialized() {
    return g_initialized;
}

// ============================================================
// 运行时控制
// ============================================================

void setLevel(Level lvl) {
    if (auto lgr = spdlog::default_logger_raw()) {
        lgr->set_level(toSpdlogLevel(lvl));
    }
}

void setFlushLevel(Level lvl) {
    if (auto lgr = spdlog::default_logger_raw()) {
        lgr->flush_on(toSpdlogLevel(lvl));
    }
}

// ============================================================
// 日志输出
// ============================================================

void log(Level lvl, std::string_view msg) {
    if (auto lgr = spdlog::default_logger_raw()) {
        lgr->log(toSpdlogLevel(lvl), msg);
    }
}

} // namespace mulan::core::Log
