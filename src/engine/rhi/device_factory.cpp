/**
 * @file device_factory.cpp
 * @brief DeviceFactory 显式注册与 RHIDevice::create 分派
 * @author hxxcxx
 * @date 2026-07-11
 *
 * 本文件不包含任何具体后端头文件。
 */
#include "device_factory.h"
#include "engine_error_code.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <string>

namespace mulan::engine {

DeviceFactory& DeviceFactory::instance() {
    static DeviceFactory factory;
    return factory;
}

ResultVoid DeviceFactory::registerModule(BackendModule module) {
    if (module.name.empty() || module.createDevice == nullptr)
        return std::unexpected(
                makeError(EngineErrorCode::InvalidBackendModule, "BackendModule requires a name and Device creator"));
    if (find(module.backend))
        return std::unexpected(
                makeError(EngineErrorCode::BackendAlreadyRegistered, "RHI backend is already registered"));

    modules_.push_back(module);
    return {};
}

const BackendModule* DeviceFactory::find(GraphicsBackend backend) const noexcept {
    const auto it = std::find_if(modules_.begin(), modules_.end(),
                                 [backend](const BackendModule& module) { return module.backend == backend; });
    return it != modules_.end() ? &*it : nullptr;
}

Result<std::unique_ptr<RHIDevice>> RHIDevice::create(const DeviceCreateInfo& ci) {
    MULAN_PROFILE_ZONE();

    const BackendModule* module = DeviceFactory::instance().find(ci.backend);
    if (!module) {
        return std::unexpected(makeError(EngineErrorCode::BackendNotSupported, "Graphics backend not registered"));
    }
    try {
        auto device = module->createDevice(ci);
        if (!device || !device->isInitialized()) {
            return std::unexpected(makeError(EngineErrorCode::DeviceLost,
                                             std::string(module->name) + " device initialization failed"));
        }
        return std::move(device);
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, e.what()));
    } catch (...) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "Graphics device creation failed"));
    }
}

}  // namespace mulan::engine
