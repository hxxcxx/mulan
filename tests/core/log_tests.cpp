#include <mulan/core/log/log.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ============================================================
// 测试策略说明
// ------------------------------------------------------------
// 对外日志接口 = LOG_* 宏 + log(Level, string_view) + 生命周期/级别控制。
// 本测试只走这些公共 API(尤其 LOG_* 宏——这是调用方真正使用的入口),
// 不触碰 detail::submit / logf 等内部实现细节。
//
// log.cpp 用 std::call_once 保证 sink 装配只发生一次,shutdown() 不重置
// 该标志。因此本可执行文件在 main() 中通过 testing::Environment 做一次
// 全局 init(同步、文件 sink、Trace 级别、立即 flush,日志写到独立临时目录)。
// 各 TEST 通过"消息唯一标记 + setLevel 动态切换"来隔离,不依赖重新装配 sink。
// ============================================================

namespace mulan::core::log {
namespace {

/// 由全局 Environment 填充,供各 TEST 读取日志文件内容。
std::filesystem::path g_logDir;

/// 读取日志文件全部文本。文件不存在返回空串。
std::string readLogFile() {
    std::ifstream s(g_logDir / "mulan.log", std::ios::binary);
    if (!s)
        return {};
    return { std::istreambuf_iterator<char>(s), {} };
}

/// 等待日志文件中出现指定子串,最长等 dur 秒。
/// spdlog 同步 sink 一般写后立即可读,但 flush 时机偶有调度延迟。
bool waitForContent(std::string_view needle, int durSeconds = 2) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(durSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (readLogFile().find(needle) != std::string::npos)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

/// 确认某子串不存在:先等够时间排除"尚未落盘"的可能,再断言缺失。
bool confirmAbsent(std::string_view needle, int waitMs = 200) {
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
    return readLogFile().find(needle) == std::string::npos;
}

}  // namespace

// ============================================================
// 进程级环境:首次(也是唯一一次)装配项目 logger。
// ============================================================
class LogEnvironment : public testing::Environment {
public:
    void SetUp() override {
        // 用相对路径(纯 ASCII),避免 std::filesystem::path::string() 在中文
        // Windows 上按 ANSI 代码页(GBK)编码后,被 spdlog 当 UTF-8 处理产生乱码。
        // 相对路径直接交给 spdlog 的窄字符 file sink,不经 filesystem 编码转换。
        g_logDir = "mulan_log_tests";
        std::filesystem::remove_all(g_logDir);
        std::filesystem::create_directories(g_logDir);

        Config cfg;
        cfg.loggerName = "mulan_test";
        cfg.logDir = "mulan_log_tests";  // 纯 ASCII 相对路径
        cfg.enableConsole = false;       // 测试不需要控制台噪音
        cfg.enableMSVC = false;          // 关闭 OutputDebugString
        cfg.asyncMode = false;           // 同步:写后立即可读
        cfg.logLevel = Level::Trace;
        cfg.flushLevel = Level::Trace;   // 每条立即落盘
        init(cfg);
    }
    void TearDown() override { shutdown(); }
};

// ============================================================
// 生命周期与状态查询
// ============================================================

TEST(LogTests, IsInitializedTrueAfterInit) {
    EXPECT_TRUE(isInitialized());
}

TEST(LogTests, RepeatedInitIsIdempotent) {
    // call_once 已消费,重复 init 不重装 sink、不抛异常,状态保持 true。
    EXPECT_NO_FATAL_FAILURE({ init(Config{}); });
    EXPECT_TRUE(isInitialized());
}

TEST(LogTests, InvalidConfigDoesNotThrowOrDisableLogger) {
    Config invalid;
    invalid.enableConsole = false;
    invalid.enableFile = true;
    invalid.enableMSVC = false;
    invalid.maxFiles = 0;

    EXPECT_NO_THROW(init(invalid));
    EXPECT_TRUE(isInitialized());
}

// ============================================================
// LOG_* 宏 —— 基本格式化
// ============================================================

TEST(LogTests, LogInfoMacroFormatsArgs) {
    setLevel(Level::Trace);
    const int x = 42;
    const std::string s = "world";
    LOG_INFO("fmt-int={} fmt-str={}", x, s);
    EXPECT_TRUE(waitForContent("fmt-int=42 fmt-str=world"));
}

TEST(LogTests, LogMacroFormatsFloatingPoint) {
    setLevel(Level::Trace);
    const double pi = 3.14159;
    LOG_INFO("pi={:.2f}", pi);
    EXPECT_TRUE(waitForContent("pi=3.14"));
}

TEST(LogTests, LogMacroHandlesVariousTypes) {
    setLevel(Level::Trace);
    const unsigned long long big = 18446744073709551500ULL;
    // std::format 对 bool 默认输出 "true"/"false"(非 printf 的 1/0),
    // 对 char 输出字符本身,对 unsigned long long 输出十进制整数。
    LOG_INFO("types-bool={} types-char={} types-ull={}", true, 'Z', big);
    EXPECT_TRUE(waitForContent("types-bool=true types-char=Z types-ull=18446744073709551500"));
}

TEST(LogTests, LogMacroWithNoArgs) {
    setLevel(Level::Trace);
    LOG_INFO("static-message-no-args");
    EXPECT_TRUE(waitForContent("static-message-no-args"));
}

// ============================================================
// LOG_* 宏 —— 六个级别映射正确
// ============================================================

TEST(LogTests, AllSixMacrosEmitAtTraceLevel) {
    setLevel(Level::Trace);
    LOG_TRACE("macro-trace-{}", 101);
    LOG_DEBUG("macro-debug-{}", 102);
    LOG_INFO("macro-info-{}", 103);
    LOG_WARN("macro-warn-{}", 104);
    LOG_ERROR("macro-error-{}", 105);
    LOG_CRITICAL("macro-critical-{}", 106);

    for (auto tag : { "macro-trace-101", "macro-debug-102", "macro-info-103", "macro-warn-104", "macro-error-105",
                      "macro-critical-106" }) {
        EXPECT_TRUE(waitForContent(tag)) << "missing: " << tag;
    }
}

// ============================================================
// 级别过滤 —— 通过 setLevel 控制宏输出
// ============================================================

TEST(LogTests, SetLevelWarnFiltersTraceDebugInfo) {
    setLevel(Level::Warn);  // 仅 Warn/Error/Critical 通过
    LOG_TRACE("filtered-trace-{}", 201);
    LOG_DEBUG("filtered-debug-{}", 202);
    LOG_INFO("filtered-info-{}", 203);
    LOG_WARN("passed-warn-{}", 204);
    LOG_ERROR("passed-error-{}", 205);

    EXPECT_TRUE(waitForContent("passed-warn-204"));
    EXPECT_TRUE(waitForContent("passed-error-205"));
    EXPECT_TRUE(confirmAbsent("filtered-trace-201"));
    EXPECT_TRUE(confirmAbsent("filtered-debug-202"));
    EXPECT_TRUE(confirmAbsent("filtered-info-203"));
}

TEST(LogTests, SetLevelErrorKeepsOnlyErrorAndCritical) {
    setLevel(Level::Error);
    LOG_WARN("filtered-warn-{}", 301);
    LOG_ERROR("passed-error-{}", 302);
    LOG_CRITICAL("passed-critical-{}", 303);

    EXPECT_TRUE(waitForContent("passed-error-302"));
    EXPECT_TRUE(waitForContent("passed-critical-303"));
    EXPECT_TRUE(confirmAbsent("filtered-warn-301"));
}

TEST(LogTests, OffLevelSuppressesEverything) {
    setLevel(Level::Off);
    LOG_CRITICAL("off-suppressed-{}", 401);
    EXPECT_TRUE(confirmAbsent("off-suppressed-401"));
    setLevel(Level::Trace);  // 恢复,避免污染后续用例
}

// ============================================================
// 纯文本输出 log(Level, string_view)
// ============================================================

TEST(LogTests, PlainLogWritesVerbatimMessage) {
    setLevel(Level::Trace);
    log(Level::Info, "verbatim-marker-no-formatting-501");
    EXPECT_TRUE(waitForContent("verbatim-marker-no-formatting-501"));
}

TEST(LogTests, PlainLogRespectsLevelFilter) {
    setLevel(Level::Error);
    log(Level::Debug, "plain-dropped-601");
    log(Level::Error, "plain-passed-602");
    setLevel(Level::Trace);  // 恢复

    EXPECT_TRUE(waitForContent("plain-passed-602"));
    EXPECT_TRUE(confirmAbsent("plain-dropped-601"));
}

// ============================================================
// 文件 sink 落盘
// ============================================================

TEST(LogTests, FileSinkCreatesLogFile) {
    setLevel(Level::Trace);
    LOG_INFO("file-existence-marker-701");
    EXPECT_TRUE(waitForContent("file-existence-marker-701"));
    EXPECT_TRUE(std::filesystem::exists(g_logDir / "mulan.log"));
}

TEST(LogTests, MultipleLogsAccumulateInFile) {
    setLevel(Level::Trace);
    LOG_INFO("accum-a-{}", 801);
    LOG_INFO("accum-b-{}", 802);
    LOG_INFO("accum-c-{}", 803);
    EXPECT_TRUE(waitForContent("accum-c-803"));
    const auto content = readLogFile();
    EXPECT_NE(content.find("accum-a-801"), std::string::npos);
    EXPECT_NE(content.find("accum-b-802"), std::string::npos);
    EXPECT_NE(content.find("accum-c-803"), std::string::npos);
}

// ============================================================
// 多线程写入：同步 logger 也必须安全地共享同一个 wrapper/logger。
// ============================================================

TEST(LogTests, ConcurrentWritersAccumulateInFile) {
    setLevel(Level::Trace);

    constexpr int threadCount = 8;
    constexpr int messagesPerThread = 100;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int thread = 0; thread < threadCount; ++thread) {
        workers.emplace_back([thread] {
            for (int message = 0; message < messagesPerThread; ++message)
                LOG_INFO("concurrent-thread-{}-message-{}", thread, message);
        });
    }

    for (auto& worker : workers)
        worker.join();

    for (int thread = 0; thread < threadCount; ++thread) {
        EXPECT_TRUE(waitForContent(std::string("concurrent-thread-") + std::to_string(thread) + "-message-99"));
    }
}

// ============================================================
// 动态级别切换在运行时即时生效
// ============================================================

TEST(LogTests, LevelSwitchTakesEffectImmediately) {
    // 先关闭,发一条(应被过滤)
    setLevel(Level::Error);
    LOG_INFO("before-switch-{}", 901);
    EXPECT_TRUE(confirmAbsent("before-switch-901"));

    // 运行时切回 Trace,同一条宏路径立即放行
    setLevel(Level::Trace);
    LOG_INFO("after-switch-{}", 902);
    EXPECT_TRUE(waitForContent("after-switch-902"));
}

}  // namespace mulan::core::log

// ============================================================
// 自定义 main:注册全局 Environment 后交还 gtest。
// (因此本测试不链接 GTest::gtest_main。)
// ============================================================
int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new mulan::core::log::LogEnvironment);
    return RUN_ALL_TESTS();
}
