#include "runtime.h"

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
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
    if (!handle)
        return false;

    auto fn = reinterpret_cast<LoadBackendFn>(GetProcAddress(handle, "mulan_load_backend"));
    if (!fn)
        return false;

    fn();  // 后端内部把自己注册进 modeling_core 的注册表
    return true;
#else
    return false;
#endif
}

}  // namespace

void init() {
    // 扫描可执行文件同目录的 backends/ 子目录，加载所有后端插件。
    // runtime 不点名任何后端，插件可热插拔。
    namespace fs = std::filesystem;
    fs::path backendsDir;

#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        backendsDir = fs::path(exePath).parent_path() / "backends";
#endif

    if (backendsDir.empty() || !fs::exists(backendsDir))
        return;

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(backendsDir, ec)) {
        if (entry.path().extension() == ".dll")
            loadBackendDll(entry.path());
    }
}

}  // namespace mulan::runtime
