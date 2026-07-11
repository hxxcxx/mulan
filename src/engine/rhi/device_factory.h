/**
 * @file device_factory.h
 * @brief RHI 后端注册表 —— 后端编译时自注册，抽象层零后端依赖。
 * @author hxxcxx
 * @date 2026-07-11
 *
 * 各后端(vulkan/d3d12)通过 AutoRegisterDeviceBackend 在静态初始化期向
 * DeviceFactory 注册 create 函数。RHIDevice::create() 查注册表分派。
 * 这使得 rhi 抽象层不包含任何后端头文件，后端可按平台/option 选择性编译。
 */
#pragma once

#include "device.h"

#include <functional>

namespace mulan::engine {

/// 设备创建函数签名。
using DeviceCreator = std::unique_ptr<RHIDevice> (*)(const DeviceCreateInfo&);

/// 后端注册表（单例）。后端通过 AutoRegisterDeviceBackend 自注册。
class DeviceFactory {
public:
    static DeviceFactory& instance();

    /// 注册或替换一个后端的创建函数。
    void registerBackend(GraphicsBackend backend, DeviceCreator creator);

    /// 查找后端创建函数；未注册时返回 nullptr。
    DeviceCreator find(GraphicsBackend backend) const;

private:
    DeviceFactory() = default;
    struct BackendEntry {
        GraphicsBackend backend;
        DeviceCreator creator;
    };
    std::vector<BackendEntry> entries_;
};

/// RAII 自动注册。后端库内部用全局静态实例触发注册。
struct AutoRegisterDeviceBackend {
    explicit AutoRegisterDeviceBackend(GraphicsBackend backend, DeviceCreator creator) {
        DeviceFactory::instance().registerBackend(backend, creator);
    }
};

}  // namespace mulan::engine
