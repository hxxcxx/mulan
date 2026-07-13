#include "resource.h"

#include "device.h"

namespace mulan::engine {

namespace {
constexpr std::size_t queueIndex(QueueType queue) {
    return static_cast<std::size_t>(queue);
}
}  // namespace

std::string_view toString(RHIResourceKind kind) {
    switch (kind) {
    case RHIResourceKind::Buffer: return "Buffer";
    case RHIResourceKind::Texture: return "Texture";
    case RHIResourceKind::Shader: return "Shader";
    case RHIResourceKind::PipelineState: return "PipelineState";
    case RHIResourceKind::ComputePipelineState: return "ComputePipelineState";
    case RHIResourceKind::CommandList: return "CommandList";
    case RHIResourceKind::SwapChain: return "SwapChain";
    case RHIResourceKind::RenderTarget: return "RenderTarget";
    case RHIResourceKind::Sampler: return "Sampler";
    case RHIResourceKind::Fence: return "Fence";
    case RHIResourceKind::BindGroup: return "BindGroup";
    }
    return "Unknown";
}

RHITrackedResource::~RHITrackedResource() {
    untrackResource();
}

void RHITrackedResource::trackResource(RHIDevice& device, RHIResourceKind kind, std::string_view name) {
    untrackResource();
    tracking_device_ = &device;
    tracking_kind_ = kind;
    tracking_name_ = std::string(name);
    tracking_device_->registerLiveResource(this, tracking_kind_, tracking_name_);
}

void RHITrackedResource::untrackResource() {
    if (!tracking_device_) {
        return;
    }
    tracking_device_->unregisterLiveResource(this);
    tracking_device_ = nullptr;
    tracking_name_.clear();
}

void RHITrackedResource::markUsed(SubmissionToken token) noexcept {
    if (!token)
        return;
    const std::size_t index = queueIndex(token.queue);
    if (index >= kQueueCount)
        return;

    last_use_generations_[index].store(token.deviceGeneration, std::memory_order_relaxed);
    uint64_t current = last_use_values_[index].load(std::memory_order_relaxed);
    while (current < token.value &&
           !last_use_values_[index].compare_exchange_weak(current, token.value, std::memory_order_release,
                                                          std::memory_order_relaxed)) {}
}

SubmissionToken RHITrackedResource::lastUseToken(QueueType queue) const noexcept {
    const std::size_t index = queueIndex(queue);
    if (index >= kQueueCount)
        return {};
    const uint64_t value = last_use_values_[index].load(std::memory_order_acquire);
    if (value == 0)
        return {};
    const uint64_t generation = last_use_generations_[index].load(std::memory_order_relaxed);
    return SubmissionToken{ generation, queue, value };
}

void RHITrackedResource::detachFromDevice(const RHIDevice& device) {
    if (tracking_device_ != &device) {
        return;
    }
    tracking_device_ = nullptr;
    tracking_name_.clear();
}

}  // namespace mulan::engine
