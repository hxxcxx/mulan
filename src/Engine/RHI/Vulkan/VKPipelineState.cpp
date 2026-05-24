#include "VKPipelineState.h"
#include "VKDevice.h"
#include "VKShader.h"

namespace MulanGeo::engine {

VKPipelineState::VKPipelineState(const GraphicsPipelineDesc& desc,
                                 vk::Device device, VKDevice* ownerDevice)
    : m_desc(desc), m_device(device)
{
    createRootSignature();

    // 从 desc 获取 RT 格式 → device RenderPass Cache → build
    std::array<TextureFormat, GraphicsPipelineDesc::kMaxRenderTargets> colorFmts;
    for (uint8_t i = 0; i < m_desc.colorTargetCount; ++i) {
        colorFmts[i] = m_desc.colorFormats[i];
    }
    vk::RenderPass renderPass = ownerDevice->getOrCreateRenderPass(
        {colorFmts.data(), m_desc.colorTargetCount},
        m_desc.depthStencilFormat,
        m_desc.depthEnable);

    build(renderPass);
}

VKPipelineState::~VKPipelineState() {
    if (m_pipeline) m_device.destroyPipeline(m_pipeline);
    if (m_layout)   m_device.destroyPipelineLayout(m_layout);
    if (m_descriptorSetLayout) m_device.destroyDescriptorSetLayout(m_descriptorSetLayout);
}

void VKPipelineState::createRootSignature() {
    // --- Descriptor Set Layout ---
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    if (m_desc.descriptorBindingCount > 0) {
        for (uint8_t i = 0; i < m_desc.descriptorBindingCount; ++i) {
            const auto& b = m_desc.descriptorBindings[i];
            vk::DescriptorType vkType;
            switch (b.type) {
            case DescriptorType::UniformBuffer: vkType = vk::DescriptorType::eUniformBuffer; break;
            case DescriptorType::TextureSRV:    vkType = vk::DescriptorType::eSampledImage; break;
            case DescriptorType::Sampler:       vkType = vk::DescriptorType::eSampler; break;
            }
            bindings.push_back({b.binding, vkType, b.count,
                                static_cast<vk::ShaderStageFlags>(b.stages)});
        }
    } else {
        // 向后兼容：无 descriptor 描述时使用默认 3 个 UBO
        bindings.push_back({0, vk::DescriptorType::eUniformBuffer,
                            1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment});
        bindings.push_back({1, vk::DescriptorType::eUniformBuffer,
                            1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment});
        bindings.push_back({2, vk::DescriptorType::eUniformBuffer,
                            1, vk::ShaderStageFlagBits::eFragment});
    }

    vk::DescriptorSetLayoutCreateInfo dslCI;
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings    = bindings.data();
    m_descriptorSetLayout = m_device.createDescriptorSetLayout(dslCI);

    // --- Pipeline Layout ---
    vk::PipelineLayoutCreateInfo plCI;
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts    = &m_descriptorSetLayout;
    m_layout = m_device.createPipelineLayout(plCI);
}

void VKPipelineState::build(vk::RenderPass renderPass) {
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    auto* vkVs = static_cast<VKShader*>(m_desc.vs);
    auto* vkPs = static_cast<VKShader*>(m_desc.ps);
    if (vkVs) stages.push_back(vkVs->stageCreateInfo());
    if (vkPs) stages.push_back(vkPs->stageCreateInfo());

    // --- Vertex Input ---
    auto vertexInputState = buildVertexInputState();

    // --- Input Assembly ---
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = toVkTopology(m_desc.topology);
    inputAssembly.primitiveRestartEnable = false;

    // --- Rasterization ---
    vk::PipelineRasterizationStateCreateInfo raster;
    raster.cullMode              = toVkCullMode(m_desc.cullMode);
    raster.frontFace             = toVkFrontFace(m_desc.frontFace);
    raster.polygonMode           = toVkPolygonMode(m_desc.fillMode);
    raster.lineWidth             = 1.0f;
    raster.depthClampEnable      = false;
    raster.rasterizerDiscardEnable = false;
    raster.depthBiasEnable       = (m_desc.depthStencil.depthBias != 0.0f ||
                                     m_desc.depthStencil.slopeScaledDepthBias != 0.0f);
    raster.depthBiasClamp        = m_desc.depthStencil.depthBiasClamp;
    raster.depthBiasConstantFactor = m_desc.depthStencil.depthBias;
    raster.depthBiasSlopeFactor   = m_desc.depthStencil.slopeScaledDepthBias;

    // --- Multisample ---
    vk::PipelineMultisampleStateCreateInfo multisample;
    multisample.sampleShadingEnable = false;
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // --- Depth/Stencil ---
    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable   = m_desc.depthStencil.depthEnable;
    depthStencil.depthWriteEnable  = m_desc.depthStencil.depthWrite;
    depthStencil.depthCompareOp    = toVkCompareOp(m_desc.depthStencil.depthFunc);
    depthStencil.stencilTestEnable = m_desc.depthStencil.stencilEnable;
    if (m_desc.depthStencil.stencilEnable) {
        depthStencil.front.failOp      = toVkStencilOp(m_desc.depthStencil.frontFace.failOp);
        depthStencil.front.passOp      = toVkStencilOp(m_desc.depthStencil.frontFace.passOp);
        depthStencil.front.depthFailOp = toVkStencilOp(m_desc.depthStencil.frontFace.depthFailOp);
        depthStencil.front.compareOp   = toVkCompareOp(m_desc.depthStencil.frontFace.func);
        depthStencil.back  = depthStencil.front;
    }

    // --- Blend ---
    vk::PipelineColorBlendAttachmentState colorBlend;
    const auto& rt0 = m_desc.blend.renderTargets[0];
    colorBlend.blendEnable    = rt0.blendEnable;
    colorBlend.srcColorBlendFactor  = toVkBlendFactor(rt0.srcBlend);
    colorBlend.dstColorBlendFactor  = toVkBlendFactor(rt0.dstBlend);
    colorBlend.colorBlendOp         = toVkBlendOp(rt0.blendOp);
    colorBlend.srcAlphaBlendFactor  = toVkBlendFactor(rt0.srcBlendAlpha);
    colorBlend.dstAlphaBlendFactor  = toVkBlendFactor(rt0.dstBlendAlpha);
    colorBlend.alphaBlendOp         = toVkBlendOp(rt0.blendOpAlpha);
    colorBlend.colorWriteMask       = vk::ColorComponentFlagBits(rt0.writeMask);

    vk::PipelineColorBlendStateCreateInfo blendCI;
    blendCI.attachmentCount = 1;
    blendCI.pAttachments    = &colorBlend;

    // --- Dynamic State ---
    vk::DynamicState dynamicStates[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamicCI;
    dynamicCI.dynamicStateCount = 2;
    dynamicCI.pDynamicStates    = dynamicStates;

    // --- Viewport / Scissor (dynamic) ---
    vk::PipelineViewportStateCreateInfo viewport;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    // --- Graphics Pipeline ---
    vk::GraphicsPipelineCreateInfo gpCI;
    gpCI.stageCount          = static_cast<uint32_t>(stages.size());
    gpCI.pStages             = stages.data();
    gpCI.pVertexInputState   = &vertexInputState;
    gpCI.pInputAssemblyState = &inputAssembly;
    gpCI.pViewportState      = &viewport;
    gpCI.pRasterizationState = &raster;
    gpCI.pMultisampleState   = &multisample;
    gpCI.pDepthStencilState  = &depthStencil;
    gpCI.pColorBlendState    = &blendCI;
    gpCI.pDynamicState       = &dynamicCI;
    gpCI.layout              = m_layout;
    gpCI.renderPass          = renderPass;
    gpCI.subpass             = 0;

    m_pipeline = m_device.createGraphicsPipeline(nullptr, gpCI).value;
}

vk::PipelineVertexInputStateCreateInfo VKPipelineState::buildVertexInputState() {
    m_bindingDescriptions.clear();
    m_attributeDescriptions.clear();

    const auto& layout = m_desc.vertexLayout;
    if (layout.empty()) {
        return {};
    }

    std::unordered_map<uint32_t, uint16_t> slotStrides;
    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        slotStrides[attr.bufferSlot] = layout.stride();
    }

    for (auto& [slot, stride] : slotStrides) {
        m_bindingDescriptions.push_back({slot, stride, vk::VertexInputRate::eVertex});
    }

    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        m_attributeDescriptions.push_back({
            i, attr.bufferSlot, vertexFormatToVk(attr.format), attr.offset
        });
    }

    vk::PipelineVertexInputStateCreateInfo vi;
    vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(m_bindingDescriptions.size());
    vi.pVertexBindingDescriptions      = m_bindingDescriptions.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_attributeDescriptions.size());
    vi.pVertexAttributeDescriptions    = m_attributeDescriptions.data();
    return vi;
}

} // namespace MulanGeo::Engine
