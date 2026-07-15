#include "detail/vk_pipeline_state.h"
#include "detail/vk_device.h"
#include "detail/vk_shader.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<VKPipelineState>> VKPipelineState::create(const GraphicsPipelineDesc& desc,
                                                                       vk::Device device) {
    auto obj = std::unique_ptr<VKPipelineState>(new VKPipelineState(desc, device));

    if (auto e = obj->createRootSignature(); e.code != 0)
        return std::unexpected(e);

    if (auto e = obj->build(); e.code != 0)
        return std::unexpected(e);

    obj->desc_.discardShaderReferences();
    return obj;
}

VKPipelineState::~VKPipelineState() {
    waitForLastUseBeforeDestruction();
    if (pipeline_)
        device_.destroyPipeline(pipeline_);
    if (layout_)
        device_.destroyPipelineLayout(layout_);
    if (descriptor_set_layout_)
        device_.destroyDescriptorSetLayout(descriptor_set_layout_);
}

core::Error VKPipelineState::createRootSignature() {
    // --- Descriptor Set Layout ---
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    for (uint8_t i = 0; i < desc_.descriptorBindingCount; ++i) {
        const auto& b = desc_.descriptorBindings[i];
        vk::DescriptorType vkType;
        switch (b.type) {
        case DescriptorType::UniformBuffer:
            vkType = b.mode == BindingMode::Dynamic ? vk::DescriptorType::eUniformBufferDynamic
                                                    : vk::DescriptorType::eUniformBuffer;
            break;
        case DescriptorType::TextureSRV: vkType = vk::DescriptorType::eSampledImage; break;
        case DescriptorType::Sampler: vkType = vk::DescriptorType::eSampler; break;
        }
        bindings.push_back({ b.binding, vkType, b.count, static_cast<vk::ShaderStageFlags>(b.stages) });
    }

    vk::DescriptorSetLayoutCreateInfo dslCI;
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings = bindings.data();

    // --- Pipeline Layout ---
    vk::PipelineLayoutCreateInfo plCI;
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &descriptor_set_layout_;

    try {
        descriptor_set_layout_ = device_.createDescriptorSetLayout(dslCI);
        layout_ = device_.createPipelineLayout(plCI);
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::PipelineCreateFailed, std::string("createRootSignature failed: ") + e.what());
    }
    return {};
}

core::Error VKPipelineState::build() {
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    auto* vkVs = static_cast<VKShader*>(desc_.vs);
    auto* vkPs = static_cast<VKShader*>(desc_.ps);
    if (vkVs)
        stages.push_back(vkVs->stageCreateInfo());
    if (vkPs)
        stages.push_back(vkPs->stageCreateInfo());

    // --- Dynamic Rendering: 通过 pNext 声明 RT 格式（替代 RenderPass）---
    vk::PipelineRenderingCreateInfo renderingCI;
    renderingCI.colorAttachmentCount = desc_.colorTargetCount;
    std::array<vk::Format, GraphicsPipelineDesc::kMaxRenderTargets> colorFmts;
    for (uint8_t i = 0; i < desc_.colorTargetCount; ++i)
        colorFmts[i] = toVkFormat(desc_.colorFormats[i]);
    renderingCI.pColorAttachmentFormats = colorFmts.data();
    renderingCI.depthAttachmentFormat = desc_.depthStencilFormat != TextureFormat::Unknown
                                                ? toVkFormat(desc_.depthStencilFormat)
                                                : vk::Format::eUndefined;
    renderingCI.stencilAttachmentFormat =
            desc_.depthStencilFormat == TextureFormat::D24_UNorm_S8_UInt ||
                            desc_.depthStencilFormat == TextureFormat::D32_Float_S8X24_UInt
                    ? toVkFormat(desc_.depthStencilFormat)
                    : vk::Format::eUndefined;

    // --- Vertex Input ---
    auto vertexInputState = buildVertexInputState();

    // --- Input Assembly ---
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = toVkTopology(desc_.topology);
    inputAssembly.primitiveRestartEnable = false;

    // --- Rasterization ---
    vk::PipelineRasterizationStateCreateInfo raster;
    raster.cullMode = toVkCullMode(desc_.cullMode);
    raster.frontFace = toVkFrontFace(desc_.frontFace);
    raster.polygonMode = toVkPolygonMode(desc_.fillMode);
    raster.lineWidth = 1.0f;
    raster.depthClampEnable = false;
    raster.rasterizerDiscardEnable = false;
    raster.depthBiasEnable = (desc_.depthStencil.depthBias != 0.0f || desc_.depthStencil.slopeScaledDepthBias != 0.0f);
    raster.depthBiasClamp = desc_.depthStencil.depthBiasClamp;
    raster.depthBiasConstantFactor = desc_.depthStencil.depthBias;
    raster.depthBiasSlopeFactor = desc_.depthStencil.slopeScaledDepthBias;

    // --- Multisample ---
    vk::PipelineMultisampleStateCreateInfo multisample;
    multisample.sampleShadingEnable = false;
    multisample.rasterizationSamples = toVkSampleCount(desc_.sampleCount);

    // --- Depth/Stencil ---
    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable = desc_.depthStencil.depthEnable;
    depthStencil.depthWriteEnable = desc_.depthStencil.depthWrite;
    depthStencil.depthCompareOp = toVkCompareOp(desc_.depthStencil.depthFunc);
    depthStencil.stencilTestEnable = desc_.depthStencil.stencilEnable;
    if (desc_.depthStencil.stencilEnable) {
        depthStencil.front.failOp = toVkStencilOp(desc_.depthStencil.frontFace.failOp);
        depthStencil.front.passOp = toVkStencilOp(desc_.depthStencil.frontFace.passOp);
        depthStencil.front.depthFailOp = toVkStencilOp(desc_.depthStencil.frontFace.depthFailOp);
        depthStencil.front.compareOp = toVkCompareOp(desc_.depthStencil.frontFace.func);
        depthStencil.back = depthStencil.front;
    }

    // --- Blend ---
    std::array<vk::PipelineColorBlendAttachmentState, GraphicsPipelineDesc::kMaxRenderTargets> colorBlends{};
    for (uint8_t i = 0; i < desc_.colorTargetCount; ++i) {
        const auto& source = desc_.blend.renderTargets[desc_.blend.independentBlend ? i : 0];
        auto& target = colorBlends[i];
        target.blendEnable = source.blendEnable;
        target.srcColorBlendFactor = toVkBlendFactor(source.srcBlend);
        target.dstColorBlendFactor = toVkBlendFactor(source.dstBlend);
        target.colorBlendOp = toVkBlendOp(source.blendOp);
        target.srcAlphaBlendFactor = toVkBlendFactor(source.srcBlendAlpha);
        target.dstAlphaBlendFactor = toVkBlendFactor(source.dstBlendAlpha);
        target.alphaBlendOp = toVkBlendOp(source.blendOpAlpha);
        target.colorWriteMask = vk::ColorComponentFlagBits(source.writeMask);
    }

    vk::PipelineColorBlendStateCreateInfo blendCI;
    blendCI.attachmentCount = desc_.colorTargetCount;
    blendCI.pAttachments = desc_.colorTargetCount > 0 ? colorBlends.data() : nullptr;

    // --- Dynamic State ---
    vk::DynamicState dynamicStates[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamicCI;
    dynamicCI.dynamicStateCount = 2;
    dynamicCI.pDynamicStates = dynamicStates;

    // --- Viewport / Scissor (dynamic) ---
    vk::PipelineViewportStateCreateInfo viewport;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // --- Graphics Pipeline ---
    vk::GraphicsPipelineCreateInfo gpCI;
    gpCI.stageCount = static_cast<uint32_t>(stages.size());
    gpCI.pStages = stages.data();
    gpCI.pVertexInputState = &vertexInputState;
    gpCI.pInputAssemblyState = &inputAssembly;
    gpCI.pViewportState = &viewport;
    gpCI.pRasterizationState = &raster;
    gpCI.pMultisampleState = &multisample;
    gpCI.pDepthStencilState = &depthStencil;
    gpCI.pColorBlendState = &blendCI;
    gpCI.pDynamicState = &dynamicCI;
    gpCI.layout = layout_;
    gpCI.renderPass = nullptr;  // dynamic rendering
    gpCI.subpass = 0;
    gpCI.pNext = &renderingCI;

    try {
        pipeline_ = device_.createGraphicsPipeline(nullptr, gpCI).value;
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::PipelineCreateFailed,
                         std::string("createGraphicsPipeline failed: ") + e.what());
    }
    return {};
}

vk::PipelineVertexInputStateCreateInfo VKPipelineState::buildVertexInputState() {
    binding_descriptions_.clear();
    attribute_descriptions_.clear();

    const auto& layout = desc_.vertexLayout;
    if (layout.empty()) {
        return {};
    }

    std::unordered_map<uint32_t, uint16_t> slotStrides;
    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        slotStrides[attr.bufferSlot] = layout.stride();
    }

    for (auto& [slot, stride] : slotStrides) {
        binding_descriptions_.push_back({ slot, stride, vk::VertexInputRate::eVertex });
    }

    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        attribute_descriptions_.push_back({ i, attr.bufferSlot, vertexFormatToVk(attr.format), attr.offset });
    }

    vk::PipelineVertexInputStateCreateInfo vi;
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(binding_descriptions_.size());
    vi.pVertexBindingDescriptions = binding_descriptions_.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions_.size());
    vi.pVertexAttributeDescriptions = attribute_descriptions_.data();
    return vi;
}

}  // namespace mulan::engine
