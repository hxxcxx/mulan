#include "resource.h"

#include "device.h"

#include <mulan/core/log/log.h>

namespace mulan::engine {

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
    lifetime_state_ = std::make_shared<RHIResourceLifetimeState>();
    tracking_device_ = &device;
    tracking_kind_ = kind;
    tracking_name_ = std::string(name);
    tracking_device_->registerLiveResource(this, tracking_kind_, tracking_name_);
}

void RHITrackedResource::untrackResource() {
    if (lifetime_state_)
        lifetime_state_->alive.store(false, std::memory_order_release);
    if (!tracking_device_) {
        lifetime_state_.reset();
        return;
    }
    tracking_device_->unregisterLiveResource(this);
    tracking_device_ = nullptr;
    tracking_name_.clear();
    lifetime_state_.reset();
}

void RHITrackedResource::waitForLastUseBeforeDestruction() noexcept {
    if (auto result = waitForLastUse(); !result)
        LOG_ERROR("[RHI] GPU resource destruction wait failed: {}", result.error().message);
}

ResultVoid RHITrackedResource::waitForLastUse() {
    if (!tracking_device_ || !lifetime_state_)
        return {};
    const uint64_t value = lifetime_state_->lastSubmissionValue.load(std::memory_order_acquire);
    if (value == 0)
        return {};
    const SubmissionToken token{ tracking_device_->deviceGeneration(), value };
    if (tracking_device_->isSubmissionComplete(token))
        return {};
    return tracking_device_->waitForSubmission(token);
}

void RHITrackedResource::detachFromDevice(const RHIDevice& device) {
    if (tracking_device_ != &device) {
        return;
    }
    tracking_device_ = nullptr;
    tracking_name_.clear();
    if (lifetime_state_)
        lifetime_state_->alive.store(false, std::memory_order_release);
    lifetime_state_.reset();
}

}  // namespace mulan::engine
