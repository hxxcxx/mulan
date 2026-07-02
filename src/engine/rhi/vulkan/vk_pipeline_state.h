/**
 * @file vk_pipeline_state.h
 * @brief Vulkan管线状态实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../pipeline_state.h"
#include "vk_convert.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <unordered_map>

namespace mulan::engine {

class VKDevice;

class VKPipelineState : public PipelineState {
public:
    /// 创建 VKPipelineState。失败返回 PipelineCreateFailed。
    static std::expected<std::unique_ptr<VKPipelineState>, core::Error>
        create(const GraphicsPipelineDesc& desc,
               vk::Device device, VKDevice* ownerDevice);
    ~VKPipelineState();

    const GraphicsPipelineDesc& desc() const override { return desc_; }

    vk::Pipeline pipeline() const { return pipeline_; }
    vk::PipelineLayout layout() const { return layout_; }
    vk::DescriptorSetLayout descriptorSetLayout() const { return descriptor_set_layout_; }

private:
    VKPipelineState(const GraphicsPipelineDesc& desc, vk::Device device)
        : desc_(desc), device_(device) {}

    core::Error build(vk::RenderPass renderPass);
    core::Error createRootSignature();
    vk::PipelineVertexInputStateCreateInfo buildVertexInputState();

    GraphicsPipelineDesc desc_;
    vk::Device           device_;
    vk::Pipeline         pipeline_;
    vk::PipelineLayout   layout_;
    vk::DescriptorSetLayout descriptor_set_layout_;

    std::vector<vk::VertexInputBindingDescription>   binding_descriptions_;
    std::vector<vk::VertexInputAttributeDescription> attribute_descriptions_;
};

} // namespace mulan::engine
