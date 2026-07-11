/**
 * @file device_factory.cpp
 * @brief DeviceFactory 注册表实现 + RHIDevice::create 分派。
 * @author hxxcxx
 * @date 2026-07-11
 *
 * 本文件不含任何后端头文件。后端各自通过 AutoRegisterDeviceBackend 注册。
 */
#include "device_factory.h"
#include "engine_error_code.h"

#include <algorithm>

namespace mulan::engine {

DeviceFactory& DeviceFactory::instance() {
    static DeviceFactory factory;
    return factory;
}

void DeviceFactory::registerBackend(GraphicsBackend backend, DeviceCreator creator) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [backend](const BackendEntry& e) { return e.backend == backend; });
    if (it != entries_.end()) {
        it->creator = creator;
    } else {
        entries_.push_back({ backend, creator });
    }
}

DeviceCreator DeviceFactory::find(GraphicsBackend backend) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [backend](const BackendEntry& e) { return e.backend == backend; });
    return it != entries_.end() ? it->creator : nullptr;
}

core::Result<std::unique_ptr<RHIDevice>> RHIDevice::create(const DeviceCreateInfo& ci) {
    auto creator = DeviceFactory::instance().find(ci.backend);
    if (!creator) {
        return std::unexpected(makeError(EngineErrorCode::BackendNotSupported, "Graphics backend not registered"));
    }
    try {
        return creator(ci);
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, e.what()));
    }
}

}  // namespace mulan::engine
