/**
 * @file resource.h
 * @brief RHIResourceKind / RHITrackedResource —— GPU 资源类型枚举与资源追踪基类。
 * @author hxxcxx
 * @date 2026-07-06
 */
#pragma once

#include "submission.h"

#include <mulan/core/result/error.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mulan::engine {

class RHIDevice;
class CommandList;

struct RHIResourceLifetimeState {
    std::atomic<bool> alive{ true };
    std::atomic<uint64_t> lastSubmissionValue{ 0 };
};

enum class RHIResourceKind : uint8_t {
    Buffer,
    Texture,
    Shader,
    PipelineState,
    ComputePipelineState,
    CommandList,
    SwapChain,
    RenderTarget,
    Sampler,
    Fence,
    BindGroup,
};

std::string_view toString(RHIResourceKind kind);

class RHITrackedResource {
public:
    virtual ~RHITrackedResource();

    void trackResource(RHIDevice& device, RHIResourceKind kind, std::string_view name = {});
    void untrackResource();
    bool isTracked() const noexcept { return tracking_device_ != nullptr; }
    bool belongsTo(const RHIDevice& device) const noexcept { return tracking_device_ == &device; }

    RHITrackedResource(const RHITrackedResource&) = delete;
    RHITrackedResource& operator=(const RHITrackedResource&) = delete;

protected:
    RHITrackedResource() = default;
    RHIDevice* trackingDevice() const noexcept { return tracking_device_; }

    /// 后端资源析构函数必须在释放原生句柄前调用，确保 GPU 已结束最后一次使用。
    void waitForLastUseBeforeDestruction() noexcept;
    /// 可变资源写入前调用，避免覆盖仍被 GPU 读取的存储。
    ResultVoid waitForLastUse();

private:
    friend class RHIDevice;
    friend class CommandList;

    void detachFromDevice(const RHIDevice& device);
    const std::shared_ptr<RHIResourceLifetimeState>& lifetimeState() const noexcept { return lifetime_state_; }

    RHIDevice* tracking_device_ = nullptr;
    RHIResourceKind tracking_kind_ = RHIResourceKind::Buffer;
    std::string tracking_name_;
    std::shared_ptr<RHIResourceLifetimeState> lifetime_state_;
};

}  // namespace mulan::engine
