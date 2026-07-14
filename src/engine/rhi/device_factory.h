/**
 * @file device_factory.h
 * @brief RHI 后端模块的显式注册与设备创建入口
 * @author hxxcxx
 * @date 2026-07-11
 *
 * 各后端导出独立 BackendModule，由最终应用显式组装实际链接的后端。
 * rhi 抽象层不依赖具体后端，也不依赖静态初始化和 whole-archive 链接行为。
 */
#pragma once

#include "device.h"

#include <span>
#include <string_view>
#include <vector>

namespace mulan::engine {

/// 设备创建函数签名。
using DeviceCreator = core::Result<std::unique_ptr<RHIDevice>> (*)(const DeviceCreateInfo&);

struct BackendModule {
    GraphicsBackend backend = GraphicsBackend::Vulkan;
    std::string_view name;
    DeviceCreator createDevice = nullptr;
};

/// 应用显式组装的后端注册表。
class DeviceFactory {
public:
    static DeviceFactory& instance();

    core::Result<void> registerModule(BackendModule module);

    const BackendModule* find(GraphicsBackend backend) const noexcept;
    std::span<const BackendModule> modules() const noexcept { return modules_; }

private:
    DeviceFactory() = default;
    std::vector<BackendModule> modules_;
};

}  // namespace mulan::engine
