/**
 * @file profile.h
 * @brief 轻量级调用树性能统计。
 *
 * 按线程聚合作用域的调用次数、包含耗时和自身耗时。不采集时间线，
 * 不启动后台线程或网络服务。RelWithDebInfo 中默认启用埋点。
 *
 * @author hxxcxx
 * @date 2026-07-17
 */

#pragma once

#include <mulan/core/core_export.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::profiling {

struct ProfileNode {
    std::string name;
    std::uint64_t callCount = 0;
    std::uint64_t inclusiveNanoseconds = 0;
    std::uint64_t selfNanoseconds = 0;
    std::vector<ProfileNode> children;
};

struct ThreadProfile {
    std::string name;
    std::uint64_t id = 0;
    std::vector<ProfileNode> roots;
};

struct ProfileSnapshot {
    std::uint64_t frameCount = 0;
    std::vector<ThreadProfile> threads;
};

class CORE_API ScopedZone final {
public:
    explicit ScopedZone(const char* name) noexcept;
    ~ScopedZone();

    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

private:
    void* threadState_ = nullptr;
};

CORE_API void setThreadName(std::string_view name);
CORE_API void markFrame() noexcept;
CORE_API void startCapture();
CORE_API ProfileSnapshot stopCapture();
CORE_API bool isCapturing() noexcept;
CORE_API void reset();
CORE_API ProfileSnapshot snapshot();
CORE_API std::string formatTree(const ProfileSnapshot& value);
CORE_API std::string formatHtml(const ProfileSnapshot& value);
CORE_API bool writeTextReport(std::string_view path);
CORE_API bool writeHtmlReport(std::string_view path);
CORE_API bool writeHtmlReport(const ProfileSnapshot& value, std::string_view path);
/// 在 rootPath 下创建唯一会话目录并写入 report.html；失败返回空字符串。
CORE_API std::string writeHtmlReportToDirectory(std::string_view rootPath);
CORE_API std::string writeHtmlReportToDirectory(const ProfileSnapshot& value, std::string_view rootPath);

}  // namespace mulan::profiling

#if defined(MULAN_ENABLE_PROFILING) && MULAN_ENABLE_PROFILING && defined(MULAN_PROFILER_BACKEND_TRACY)

#include <tracy/Tracy.hpp>

#define MULAN_PROFILE_ZONE()       ZoneScoped
#define MULAN_PROFILE_ZONE_N(name) ZoneScopedN(name)
#define MULAN_PROFILE_FRAME()      FrameMark
#define MULAN_PROFILE_THREAD(name) tracy::SetThreadName(name)

#elif defined(MULAN_ENABLE_PROFILING) && MULAN_ENABLE_PROFILING && defined(MULAN_PROFILER_BACKEND_BUILTIN)

#define MULAN_PROFILE_DETAIL_JOIN_INNER(a, b) a##b
#define MULAN_PROFILE_DETAIL_JOIN(a, b)       MULAN_PROFILE_DETAIL_JOIN_INNER(a, b)
#define MULAN_PROFILE_ZONE() \
    ::mulan::profiling::ScopedZone MULAN_PROFILE_DETAIL_JOIN(mulanProfileZone_, __COUNTER__)(__FUNCTION__)
#define MULAN_PROFILE_ZONE_N(name) \
    ::mulan::profiling::ScopedZone MULAN_PROFILE_DETAIL_JOIN(mulanProfileZone_, __COUNTER__)(name)
#define MULAN_PROFILE_FRAME()      ::mulan::profiling::markFrame()
#define MULAN_PROFILE_THREAD(name) ::mulan::profiling::setThreadName(name)

#else

#define MULAN_PROFILE_ZONE()       ((void) 0)
#define MULAN_PROFILE_ZONE_N(name) ((void) 0)
#define MULAN_PROFILE_FRAME()      ((void) 0)
#define MULAN_PROFILE_THREAD(name) ((void) 0)

#endif
