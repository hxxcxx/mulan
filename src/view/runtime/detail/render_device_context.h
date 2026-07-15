/**
 * @file render_device_context.h
 * @brief RenderDeviceContext 管理可复用的 RHI Device，并串行化其访问。
 * @author hxxcxx
 * @date 2026-07-12
 *
 * Device 归属于后端和渲染配置，而非某一个 ViewContext。窗口表面与截图表面
 * 作为此上下文的客户端，可在 Device 存活期间独立创建和销毁。
 */
#pragma once

#include <mulan/core/result/error.h>
#include <mulan/rhi/device.h>
#include <mulan/render/device_resource_service.h>
#include <mulan/view/core/view_config.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace mulan::view::detail {

class RenderDeviceContext {
public:
    static Result<std::shared_ptr<RenderDeviceContext>> acquire(const ViewConfig& config);

    RenderDeviceContext(const RenderDeviceContext&) = delete;
    RenderDeviceContext& operator=(const RenderDeviceContext&) = delete;

    engine::RHIDevice& device() { return *device_; }
    const engine::RHIDevice& device() const { return *device_; }
    engine::DeviceResourceService& resources() { return *resource_service_; }
    const engine::DeviceResourceService& resources() const { return *resource_service_; }

    /// 录制/提交命令及修改此 Device 的 RHI 资源时，必须持有此锁。
    std::unique_lock<std::mutex> lock() { return std::unique_lock(device_mutex_); }

    engine::GraphicsBackend backend() const { return backend_; }

    bool isHealthy() const { return healthy_.load(); }
    /// GPU 提交域失败后禁止其他视图继续复用同一 Device。
    void markFailed();

private:
    RenderDeviceContext(std::unique_ptr<engine::RHIDevice> device, engine::GraphicsBackend backend)
        : device_(std::move(device)),
          resource_service_(std::make_unique<engine::DeviceResourceService>(*device_)),
          backend_(backend) {}

    static bool canShare(engine::GraphicsBackend backend);
    static bool matches(const RenderDeviceContext& context, const ViewConfig& config);

    std::unique_ptr<engine::RHIDevice> device_;
    // 声明在 Device 之后，析构时先释放所有共享 GPU 资源，再销毁 Device。
    std::unique_ptr<engine::DeviceResourceService> resource_service_;
    engine::GraphicsBackend backend_ = engine::GraphicsBackend::Vulkan;
    engine::RenderConfig render_config_;
    bool validation_enabled_ = true;
    std::atomic<bool> healthy_ = true;
    std::mutex device_mutex_;
};

}  // namespace mulan::view::detail
