#include "vk_compute_pipeline.h"
#include "vk_shader.h"

#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"

#include <string>
#include <vector>

namespace mulan::engine {

core::Result<std::unique_ptr<VKComputePipelineState>> VKComputePipelineState::create(const ComputePipelineDesc& desc,
                                                                                     vk::Device device) {
    auto obj = std::unique_ptr<VKComputePipelineState>(new VKComputePipelineState(desc, device));
    if (auto e = obj->build(); e.code != 0)
        return std::unexpected(e);
    return obj;
}

VKComputePipelineState::~VKComputePipelineState() {
    if (pipeline_)
        device_.destroyPipeline(pipeline_);
    if (layout_)
        device_.destroyPipelineLayout(layout_);
    if (descriptor_set_layout_)
        device_.destroyDescriptorSetLayout(descriptor_set_layout_);
}

core::Error VKComputePipelineState::build() {
    // Descriptor set layout
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    for (uint8_t i = 0; i < desc_.descriptorBindingCount; ++i) {
        const auto& b = desc_.descriptorBindings[i];
        vk::DescriptorType vkType;
        switch (b.type) {
        case DescriptorType::UniformBuffer: vkType = vk::DescriptorType::eUniformBuffer; break;
        case DescriptorType::TextureSRV: vkType = vk::DescriptorType::eSampledImage; break;
        case DescriptorType::Sampler: vkType = vk::DescriptorType::eSampler; break;
        }
        bindings.push_back({ b.binding, vkType, b.count, static_cast<vk::ShaderStageFlags>(b.stages) });
    }

    vk::DescriptorSetLayoutCreateInfo dslCI;
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings = bindings.data();

    try {
        descriptor_set_layout_ = device_.createDescriptorSetLayout(dslCI);
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::PipelineCreateFailed,
                         std::string("Compute descriptorSetLayout failed: ") + e.what());
    }

    // Pipeline layout
    vk::PipelineLayoutCreateInfo plCI;
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &descriptor_set_layout_;

    // Push constants
    vk::PushConstantRange pcRange;
    if (desc_.pushConstantSize > 0) {
        pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pcRange.offset = 0;
        pcRange.size = desc_.pushConstantSize;
        plCI.pushConstantRangeCount = 1;
        plCI.pPushConstantRanges = &pcRange;
    }

    try {
        layout_ = device_.createPipelineLayout(plCI);
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::PipelineCreateFailed,
                         std::string("Compute pipelineLayout failed: ") + e.what());
    }

    // Shader stage
    auto* vkCs = static_cast<VKShader*>(desc_.cs);
    auto stageCI = vkCs->stageCreateInfo();

    // Compute pipeline
    vk::ComputePipelineCreateInfo cpCI;
    cpCI.stage = stageCI;
    cpCI.layout = layout_;

    try {
        pipeline_ = device_.createComputePipeline(nullptr, cpCI).value;
    } catch (const vk::Error& e) {
        return makeError(EngineErrorCode::PipelineCreateFailed,
                         std::string("createComputePipeline failed: ") + e.what());
    }

    return {};
}

}  // namespace mulan::engine
