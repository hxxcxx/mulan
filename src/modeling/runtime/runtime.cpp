#include "runtime.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <mulan/core/log/log.h>
#include <mulan/modeling/core/shape_ops.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef MULAN_DEFAULT_SHAPE_OPS_BACKEND
#define MULAN_DEFAULT_SHAPE_OPS_BACKEND "occt"
#endif

namespace mulan::runtime {

namespace {

/// 后端 DLL 导出的固定 C 符号签名（见各后端 backend_entry.cpp）。
using LoadBackendFn = void (*)();

/// 加载单个后端 DLL：LoadLibrary + GetProcAddress(mulan_load_backend) + 调用。
/// 本函数对任何具体后端一无所知，只认固定的 C 符号契约。
bool loadBackendDll(const std::filesystem::path& dllPath) {
#ifdef _WIN32
    HMODULE handle = LoadLibraryW(dllPath.wstring().c_str());
    if (!handle) {
        LOG_ERROR("[ModelingRuntime] Backend DLL load failed: path={}, win32Error={}", dllPath.string(),
                  static_cast<unsigned long>(GetLastError()));
        return false;
    }

    auto fn = reinterpret_cast<LoadBackendFn>(GetProcAddress(handle, "mulan_load_backend"));
    if (!fn) {
        LOG_ERROR("[ModelingRuntime] Backend entry point is missing: path={}, symbol=mulan_load_backend",
                  dllPath.string());
        FreeLibrary(handle);
        return false;
    }

    fn();  // 后端内部把自己注册进 modeling_core 的注册表
    LOG_INFO("[ModelingRuntime] Backend plugin loaded: {}", dllPath.string());
    return true;
#else
    LOG_WARN("[ModelingRuntime] Dynamic backend loading is unavailable on this platform: {}", dllPath.string());
    return false;
#endif
}

/// ShapeOps 使用独立配置。新名称明确限定能力，旧名称仅用于兼容已有启动脚本。
std::string configuredShapeOpsBackend() {
    if (const char* backend = std::getenv("MULAN_SHAPE_OPS_BACKEND"); backend && *backend)
        return backend;
    if (const char* backend = std::getenv("MULAN_MODELING_BACKEND"); backend && *backend)
        return backend;
    return MULAN_DEFAULT_SHAPE_OPS_BACKEND;
}

}  // namespace

void init() {
    auto& registry = modeling::ShapeOpsRegistry::instance();
    registry.selectBackend(configuredShapeOpsBackend());
    LOG_INFO("[ModelingRuntime] Initializing: selectedBackend={}", registry.selectedBackend());

    // 扫描可执行文件同目录的 backends/ 子目录，加载所有后端插件。
    // runtime 不点名任何后端，插件可热插拔。
    namespace fs = std::filesystem;
    fs::path backendsDir;

#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        backendsDir = fs::path(exePath).parent_path() / "backends";
#endif

    if (backendsDir.empty() || !fs::exists(backendsDir)) {
        LOG_WARN("[ModelingRuntime] Backend directory not found: {}",
                 backendsDir.empty() ? std::string("<unresolved>") : backendsDir.string());
        return;
    }

    std::error_code ec;
    size_t loadedCount = 0;
    for (auto& entry : fs::directory_iterator(backendsDir, ec)) {
        if (entry.path().extension() == ".dll" && loadBackendDll(entry.path()))
            ++loadedCount;
    }
    if (ec) {
        LOG_ERROR("[ModelingRuntime] Backend directory scan failed: path={}, error={}", backendsDir.string(),
                  ec.message());
    }

    const auto available = registry.availableBackends();
    LOG_INFO("[ModelingRuntime] Initialization completed: pluginsLoaded={}, registeredBackends={}, selectedBackend={}",
             loadedCount, available.size(), registry.selectedBackend());
    for (const auto& backend : available) {
        LOG_DEBUG("[ModelingRuntime] Registered backend: {}", backend);
    }
    if (!registry.ops()) {
        LOG_WARN("[ModelingRuntime] Selected backend is unavailable: {}", registry.selectedBackend());
    }
}

}  // namespace mulan::runtime
