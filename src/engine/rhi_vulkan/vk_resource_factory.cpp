#include "detail/vk_resource_factory.h"
#include "../rhi/pipeline_validation.h"

#include "detail/vk_bind_group.h"
#include "detail/vk_buffer.h"
#include "detail/vk_compute_pipeline.h"
#include "detail/vk_debug_name.h"
#include "detail/vk_fence.h"
#include "detail/vk_pipeline_state.h"
#include "detail/vk_render_target.h"
#include "detail/vk_sampler.h"
#include "detail/vk_shader.h"
#include "detail/vk_texture.h"
#include "detail/vk_upload_context.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

VKResourceFactory::VKResourceFactory(RHIDevice& owner, vk::Device device, VmaAllocator allocator,
                                     VKUploadContext& uploadContext)
    : owner_(owner), device_(device), allocator_(allocator), upload_context_(uploadContext) {
}

core::Result<std::unique_ptr<Buffer>> VKResourceFactory::createBuffer(const BufferDesc& desc) {
    auto result = VKBuffer::create(desc, allocator_);
    if (!result)
        return std::unexpected(result.error());
    auto& buf = *result;
    setDebugName(device_, vk::ObjectType::eBuffer, reinterpret_cast<uint64_t>(VkBuffer(buf->vkBuffer())),
                 desc.name.empty() ? "Buffer" : desc.name);
    if (buf->needsUpload()) {
        if (auto uploadResult = upload_context_.uploadBufferInit(buf.get()); !uploadResult)
            return std::unexpected(uploadResult.error());
    }
    buf->trackResource(owner_, RHIResourceKind::Buffer, desc.name);
    return result;
}

core::Result<std::unique_ptr<Texture>> VKResourceFactory::createTexture(const TextureDesc& desc) {
    auto result = VKTexture::create(desc, device_, allocator_);
    if (!result)
        return std::unexpected(result.error());
    auto& tex = *result;
    setDebugName(device_, vk::ObjectType::eImage, reinterpret_cast<uint64_t>(VkImage(tex->image())),
                 desc.name.empty() ? "Texture" : desc.name);
    setDebugName(device_, vk::ObjectType::eImageView, reinterpret_cast<uint64_t>(VkImageView(tex->view())),
                 desc.name.empty() ? "TextureView" : (std::string(desc.name) + "/view").c_str());
    tex->trackResource(owner_, RHIResourceKind::Texture, desc.name);
    return result;
}

core::Result<std::unique_ptr<Shader>> VKResourceFactory::createShader(const ShaderDesc& desc) {
    auto result = VKShader::create(desc, device_);
    if (!result)
        return std::unexpected(result.error());
    auto& sh = *result;
    setDebugName(device_, vk::ObjectType::eShaderModule, reinterpret_cast<uint64_t>(VkShaderModule(sh->module())),
                 desc.name.empty() ? "Shader" : desc.name);
    sh->trackResource(owner_, RHIResourceKind::Shader, desc.name);
    return result;
}

core::Result<std::unique_ptr<PipelineState>> VKResourceFactory::createPipelineState(const GraphicsPipelineDesc& desc) {
    if (auto validation = validateGraphicsPipelineDesc(desc, owner_, owner_.capabilities()); !validation)
        return std::unexpected(validation.error());
    auto result = VKPipelineState::create(desc, device_);
    if (!result)
        return std::unexpected(result.error());
    auto& pso = *result;
    setDebugName(device_, vk::ObjectType::ePipeline, reinterpret_cast<uint64_t>(VkPipeline(pso->pipeline())),
                 desc.name.empty() ? "Pipeline" : desc.name);
    pso->trackResource(owner_, RHIResourceKind::PipelineState, desc.name);
    return result;
}

core::Result<std::unique_ptr<ComputePipelineState>> VKResourceFactory::createComputePipelineState(
        const ComputePipelineDesc& desc) {
    if (auto validation = validateComputePipelineDesc(desc, owner_, owner_.capabilities()); !validation)
        return std::unexpected(validation.error());
    auto result = VKComputePipelineState::create(desc, device_);
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(owner_, RHIResourceKind::ComputePipelineState, desc.name);
    return result;
}

core::Result<std::unique_ptr<RenderTarget>> VKResourceFactory::createRenderTarget(const RenderTargetDesc& desc) {
    auto result = VKRenderTarget::create(desc, device_, allocator_);
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(owner_, RHIResourceKind::RenderTarget, "RenderTarget");
    return result;
}

core::Result<std::unique_ptr<Sampler>> VKResourceFactory::createSampler(const SamplerDesc& desc) {
    auto result = VKSampler::create(desc, device_);
    if (!result)
        return std::unexpected(result.error());
    auto& s = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Sampler@%p", s.get());
    setDebugName(device_, vk::ObjectType::eSampler, reinterpret_cast<uint64_t>(VkSampler(s->handle())), nm);
    s->trackResource(owner_, RHIResourceKind::Sampler, nm);
    return result;
}

core::Result<std::unique_ptr<Fence>> VKResourceFactory::createFence(uint64_t initialValue) {
    auto result = VKFence::create(device_, initialValue);
    if (!result)
        return std::unexpected(result.error());
    auto& f = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Fence@%p", f.get());
    setDebugName(device_, vk::ObjectType::eSemaphore, reinterpret_cast<uint64_t>(VkSemaphore(f->semaphore())), nm);
    f->trackResource(owner_, RHIResourceKind::Fence, nm);
    return result;
}

core::Result<std::unique_ptr<BindGroup>> VKResourceFactory::createBindGroup(const BindGroupLayout& layout,
                                                                            const BindGroupDesc& desc) {
    auto bindGroup = std::unique_ptr<BindGroup>(std::make_unique<VKBindGroup>(layout, desc.entries, desc.count));
    bindGroup->trackResource(owner_, RHIResourceKind::BindGroup, "BindGroup");
    return bindGroup;
}

}  // namespace mulan::engine
