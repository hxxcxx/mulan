/**
 * @file render_device_context.h
 * @brief RenderDeviceContext 管理单个 GPU 执行域独占的 RHI Device 与设备级资源。
 * @author hxxcxx
 * @date 2026-07-12
 *
 * 由 GpuExecutionDomain 创建并持有。所有 RHI 调用均发生在所属执行线程，
 * 不在此层重复加锁或建立第二套共享注册表。
 */
#pragma once

#include <mulan/core/result/error.h>
#include <mulan/rhi/device.h>
#include <mulan/render/device_resource_service.h>
#include <mulan/view/core/view_config.h>

#include <atomic>
#include <memory>

namespace mulan::view::detail {

class RenderDeviceContext {
public:
    static Result<std::shared_ptr<RenderDeviceContext>> create(const ViewConfig& config);

    RenderDeviceContext(const RenderDeviceContext&) = delete;
    RenderDeviceContext& operator=(const RenderDeviceContext&) = delete;

    engine::RHIDevice& device() { return *device_; }
    const engine::RHIDevice& device() const { return *device_; }
    engine::DeviceResourceService& resources() { return *resource_service_; }
    const engine::DeviceResourceService& resources() const { return *resource_service_; }

    engine::GraphicsBackend backend() const { return backend_; }

    bool isHealthy() const { return healthy_.load(); }
    /// GPU 提交域失败后禁止其他视图继续复用同一 Device。
    void markFailed();

private:
    RenderDeviceContext(std::unique_ptr<engine::RHIDevice> device, engine::GraphicsBackend backend)
        : device_(std::move(device)),
          resource_service_(std::make_unique<engine::DeviceResourceService>(*device_)),
          backend_(backend) {}

    std::unique_ptr<engine::RHIDevice> device_;
    // 声明在 Device 之后，析构时先释放所有共享 GPU 资源，再销毁 Device。
    std::unique_ptr<engine::DeviceResourceService> resource_service_;
    engine::GraphicsBackend backend_ = engine::GraphicsBackend::Vulkan;
    std::atomic<bool> healthy_ = true;
};

}  // namespace mulan::view::detail
