#include "runtime.h"

#include <cstdlib>
#include <array>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <mulan/core/log/log.h>
#include <mulan/modeling/core/shape_ops.h>

#ifdef _WIN32
#include <windows.h>

#include <softpub.h>
#include <wintrust.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

#ifndef MULAN_DEFAULT_SHAPE_OPS_BACKEND
#define MULAN_DEFAULT_SHAPE_OPS_BACKEND "occt"
#endif

namespace mulan::runtime {

namespace {

/// 后端动态库导出的固定 C 符号签名（见各后端 backend_entry.cpp）。
using LoadBackendFn = void (*)();

#ifdef _WIN32
constexpr auto allowedBackendLibraries() {
    std::array<std::wstring_view, MULAN_ENABLE_OCCT_BACKEND + MULAN_ENABLE_TRUCK_BACKEND> names{};
    size_t index = 0;
#if MULAN_ENABLE_OCCT_BACKEND
    names[index++] = L"mulan_modeling_occt.dll";
#endif
#if MULAN_ENABLE_TRUCK_BACKEND
    names[index++] = L"mulan_modeling_truck.dll";
#endif
    return names;
}

bool isAllowedBackendLibrary(const std::filesystem::path& path) {
    std::wstring filename = path.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    const auto allowed = allowedBackendLibraries();
    return std::find(allowed.begin(), allowed.end(), filename) != allowed.end();
}
#else
constexpr auto allowedBackendLibraries() {
    std::array<std::string_view, MULAN_ENABLE_OCCT_BACKEND + MULAN_ENABLE_TRUCK_BACKEND> names{};
    size_t index = 0;
#if MULAN_ENABLE_OCCT_BACKEND
    names[index++] = "libmulan_modeling_occt.so";
#endif
#if MULAN_ENABLE_TRUCK_BACKEND
    names[index++] = "libmulan_modeling_truck.so";
#endif
    return names;
}

bool isAllowedBackendLibrary(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    const auto allowed = allowedBackendLibraries();
    return std::find(allowed.begin(), allowed.end(), filename) != allowed.end();
}
#endif

constexpr std::string_view backendLibraryExtension() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";
#endif
}

#ifdef _WIN32
bool hasTrustedAuthenticodeSignature(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    const std::wstring nativePath = path.wstring();
    fileInfo.pcwszFilePath = nativePath.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    // Keep startup deterministic and offline-safe; the OS still validates the embedded signature and trust chain.
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trustData);
    return status == ERROR_SUCCESS;
}
#endif

/// 加载单个后端动态库，查找 mulan_load_backend 并调用。
/// 本函数对任何具体后端一无所知，只认固定的 C 符号契约。
bool loadBackendLibrary(const std::filesystem::path& libraryPath) {
#ifdef _WIN32
    // Dependencies are intentionally resolved only from the application directory and System32, never beside an
    // independently writable plugin. Release packaging keeps the backend's third-party dependencies with the app.
    HMODULE handle = LoadLibraryExW(libraryPath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!handle) {
        LOG_ERROR("[ModelingRuntime] Backend DLL load failed: path={}, win32Error={}", libraryPath.string(),
                  static_cast<unsigned long>(GetLastError()));
        return false;
    }

    auto fn = reinterpret_cast<LoadBackendFn>(GetProcAddress(handle, "mulan_load_backend"));
    if (!fn) {
        LOG_ERROR("[ModelingRuntime] Backend entry point is missing: path={}, symbol=mulan_load_backend",
                  libraryPath.string());
        FreeLibrary(handle);
        return false;
    }

    fn();  // 后端内部把自己注册进 modeling_core 的注册表
    // 注册表持有后端实现对象，其虚表位于插件中；成功加载后必须常驻到进程退出。
    LOG_INFO("[ModelingRuntime] Backend plugin loaded: {}", libraryPath.string());
    return true;
#elif defined(__linux__)
    void* handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* error = dlerror();
        LOG_ERROR("[ModelingRuntime] Backend shared library load failed: path={}, error={}", libraryPath.string(),
                  error ? error : "unknown dlopen error");
        return false;
    }

    dlerror();
    auto fn = reinterpret_cast<LoadBackendFn>(dlsym(handle, "mulan_load_backend"));
    const char* symbolError = dlerror();
    if (symbolError || !fn) {
        LOG_ERROR("[ModelingRuntime] Backend entry point is missing: path={}, symbol=mulan_load_backend, error={}",
                  libraryPath.string(), symbolError ? symbolError : "unknown dlsym error");
        dlclose(handle);
        return false;
    }

    fn();
    // 与 Windows 分支相同，成功加载后不卸载：注册表中的对象依赖插件代码。
    LOG_INFO("[ModelingRuntime] Backend plugin loaded: {}", libraryPath.string());
    return true;
#else
    LOG_WARN("[ModelingRuntime] Dynamic backend loading is unavailable on this platform: {}", libraryPath.string());
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

    // 仅从可执行文件同目录的 backends/ 加载本构建启用的后端；不接受任意动态库。
    namespace fs = std::filesystem;
    fs::path executablePath;
    fs::path backendsDir;

#ifdef _WIN32
    std::array<wchar_t, 32768> exePath{};
    const DWORD exePathLength = GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
    if (exePathLength > 0 && exePathLength < exePath.size()) {
        executablePath = fs::path(std::wstring_view(exePath.data(), exePathLength));
        backendsDir = executablePath.parent_path() / "backends";
    }
#elif defined(__linux__)
    std::error_code executablePathError;
    executablePath = fs::read_symlink("/proc/self/exe", executablePathError);
    if (!executablePathError)
        backendsDir = executablePath.parent_path() / "backends";
    else
        LOG_ERROR("[ModelingRuntime] Executable path resolution failed: error={}", executablePathError.message());
#endif

    if (backendsDir.empty() || !fs::exists(backendsDir)) {
        LOG_WARN("[ModelingRuntime] Backend directory not found: {}",
                 backendsDir.empty() ? std::string("<unresolved>") : backendsDir.string());
        return;
    }

    std::error_code ec;
    backendsDir = fs::weakly_canonical(backendsDir, ec);
    if (ec) {
        LOG_ERROR("[ModelingRuntime] Backend directory canonicalization failed: error={}", ec.message());
        return;
    }

    size_t loadedCount = 0;
#ifdef _WIN32
    // Signed production hosts only accept plugins with an OS-trusted Authenticode chain. Unsigned developer
    // builds remain usable, while still receiving the allow-list, canonical-path and restricted-search defenses.
    const bool requireTrustedSignature = hasTrustedAuthenticodeSignature(executablePath);
    if (!requireTrustedSignature)
        LOG_WARN("[ModelingRuntime] Host executable is unsigned; backend signature enforcement is disabled");
#endif
    for (auto& entry : fs::directory_iterator(backendsDir, ec)) {
        std::error_code entryError;
        const fs::path canonicalPath = fs::weakly_canonical(entry.path(), entryError);
        if (entryError || !entry.is_regular_file(entryError) || entry.is_symlink(entryError) ||
            canonicalPath.parent_path() != backendsDir || !isAllowedBackendLibrary(canonicalPath)) {
            if (entry.path().extension().string() == backendLibraryExtension())
                LOG_WARN("[ModelingRuntime] Ignoring untrusted or unknown backend library: {}", entry.path().string());
            continue;
        }
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(entry.path().wstring().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            LOG_WARN("[ModelingRuntime] Ignoring backend DLL through a reparse point: {}", entry.path().string());
            continue;
        }
        if (requireTrustedSignature && !hasTrustedAuthenticodeSignature(canonicalPath)) {
            LOG_ERROR("[ModelingRuntime] Rejecting backend DLL with an untrusted signature: {}",
                      canonicalPath.string());
            continue;
        }
#endif
        if (loadBackendLibrary(canonicalPath))
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
