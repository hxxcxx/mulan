/**
 * @file VKPipelineState.h
 * @brief Vulkan管线状态实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../PipelineState.h"
#include "VkConvert.h"

#include <unordered_map>

namespace MulanGeo::engine {

class VKDevice;

class VKPipelineState : public PipelineState {
public:
    VKPipelineState(const GraphicsPipelineDesc& desc,
                    vk::Device device, VKDevice* ownerDevice);
    ~VKPipelineState();

    const GraphicsPipelineDesc& desc() const override { return m_desc; }

    vk::Pipeline pipeline() const { return m_pipeline; }
    vk::PipelineLayout layout() const { return m_layout; }
    vk::DescriptorSetLayout descriptorSetLayout() const { return m_descriptorSetLayout; }

private:
    void build(vk::RenderPass renderPass);
    void createRootSignature();
    vk::PipelineVertexInputStateCreateInfo buildVertexInputState();

    GraphicsPipelineDesc m_desc;
    vk::Device           m_device;
    vk::Pipeline         m_pipeline;
    vk::PipelineLayout   m_layout;
    vk::DescriptorSetLayout m_descriptorSetLayout;

    std::vector<vk::VertexInputBindingDescription>   m_bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> m_attributeDescriptions;
};

} // namespace MulanGeo::Engine
