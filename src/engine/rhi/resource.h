/**
 * @file resource.h
 * @brief RHIResourceKind / RHITrackedResource —— GPU 资源类型枚举与资源追踪基类。
 * @author hxxcxx
 * @date 2026-07-06
 */
#pragma once

#include "submission.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mulan::engine {

class RHIDevice;

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

    /// 由 CommandList 在成功提交后写入；每条 queue 只保留最大的已提交序号。
    void markUsed(SubmissionToken token) noexcept;
    SubmissionToken lastUseToken(QueueType queue = QueueType::Graphics) const noexcept;

    RHITrackedResource(const RHITrackedResource&) = delete;
    RHITrackedResource& operator=(const RHITrackedResource&) = delete;

protected:
    RHITrackedResource() = default;
    RHIDevice* trackingDevice() const noexcept { return tracking_device_; }

private:
    friend class RHIDevice;

    void detachFromDevice(const RHIDevice& device);

    RHIDevice* tracking_device_ = nullptr;
    RHIResourceKind tracking_kind_ = RHIResourceKind::Buffer;
    std::string tracking_name_;

    static constexpr std::size_t kQueueCount = 3;
    std::array<std::atomic<uint64_t>, kQueueCount> last_use_generations_{};
    std::array<std::atomic<uint64_t>, kQueueCount> last_use_values_{};
};

}  // namespace mulan::engine
