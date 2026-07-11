/**
 * @file vk_resource_factory.h
 * @brief Vulkan backend resource factory: 创建 Vulkan 资源对象（Buffer/Texture/Shader/PipelineState 等）。
 * @author hxxcxx
 * @date 2026-04-15
 */
#pragma once

#include "vk_common.h"
#include "../rhi/device.h"

#include <memory>

namespace mulan::engine {

class VKUploadContext;

class VKResourceFactory {
public:
    VKResourceFactory(RHIDevice& owner, vk::Device device, VmaAllocator allocator, VKUploadContext& uploadContext);

    core::Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc);
    core::Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc);
    core::Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc);
    core::Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc);
    core::Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(const ComputePipelineDesc& desc);
    core::Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc& desc);
    core::Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc& desc);
    core::Result<std::unique_ptr<Fence>> createFence(uint64_t initialValue);
    core::Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc);

private:
    RHIDevice& owner_;
    vk::Device device_;
    VmaAllocator allocator_ = nullptr;
    VKUploadContext& upload_context_;
};

}  // namespace mulan::engine
