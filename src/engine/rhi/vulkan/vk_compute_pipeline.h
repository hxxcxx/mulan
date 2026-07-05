/**
 * @file vk_compute_pipeline.h
 * @brief VK Compute Pipeline 实现
 * @author hxxcxx
 */
#pragma once

#include "../pipeline_state.h"
#include "vk_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <vector>

namespace mulan::engine {

class VKComputePipelineState : public ComputePipelineState {
public:
    static core::Result<std::unique_ptr<VKComputePipelineState>> create(const ComputePipelineDesc& desc,
                                                                        vk::Device device);
    ~VKComputePipelineState();

    const ComputePipelineDesc& desc() const override { return desc_; }

    vk::Pipeline pipeline() const { return pipeline_; }
    vk::PipelineLayout layout() const { return layout_; }
    vk::DescriptorSetLayout descriptorSetLayout() const { return descriptor_set_layout_; }

private:
    VKComputePipelineState(const ComputePipelineDesc& desc, vk::Device device) : desc_(desc), device_(device) {}

    core::Error build();

    ComputePipelineDesc desc_;
    vk::Device device_;
    vk::Pipeline pipeline_;
    vk::PipelineLayout layout_;
    vk::DescriptorSetLayout descriptor_set_layout_;
};

}  // namespace mulan::engine
