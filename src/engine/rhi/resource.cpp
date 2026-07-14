#include "resource.h"

#include "device.h"

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

void RHITrackedResource::detachFromDevice(const RHIDevice& device) {
    if (tracking_device_ != &device) {
        return;
    }
    tracking_device_ = nullptr;
    tracking_name_.clear();
}

}  // namespace mulan::engine
