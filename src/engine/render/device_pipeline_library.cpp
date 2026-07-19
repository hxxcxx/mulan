/**
 * @file device_pipeline_library.cpp
 * @brief DevicePipelineLibrary 的 shader 与完整 pipeline key 缓存实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "device_pipeline_library.h"

#include "shader/shader_loader.h"
#include "../rhi/pipeline_state.h"
#include "../rhi/device.h"

#include <mulan/core/profiling/profile.h>

namespace mulan::engine {

DevicePipelineLibrary::~DevicePipelineLibrary() = default;

size_t DevicePipelineKeyHash::operator()(const DevicePipelineKey& key) const noexcept {
    size_t value = std::hash<uint8_t>{}(static_cast<uint8_t>(key.technique));
    const auto combine = [&value](size_t part) {
        value ^= part + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
    };
    combine(std::hash<uint16_t>{}(static_cast<uint16_t>(key.colorFormat)));
    combine(std::hash<uint16_t>{}(static_cast<uint16_t>(key.depthFormat)));
    combine(std::hash<uint32_t>{}(key.sampleCount));
    combine(std::hash<bool>{}(key.hasDepth));
    combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.objectBindingMode)));
    combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.alphaMode)));
    combine(std::hash<bool>{}(key.doubleSided));
    combine(std::hash<bool>{}(key.reverseWinding));
    return value;
}

PipelineState* DevicePipelineLibrary::acquire(const DevicePipelineKey& key) {
    if (const auto known = entries_.find(key); known != entries_.end()) {
        return known->second.pipeline.get();
    }
    MULAN_PROFILE_ZONE_N("RHI/CreatePipeline");
    const TechniqueDesc& technique = TechniqueRegistry::builtin(key.technique);
    const char* vertexShaderName = technique.shader.vertex;
    if (key.objectBindingMode == ObjectBindingMode::InstancedBatch) {
        if (!technique.instancedVertexShader || technique.instancedVertexShader[0] == '\0') {
            return nullptr;
        }
        vertexShaderName = technique.instancedVertexShader;
    }
    auto vertexShader = loadShader(device_, ShaderType::Vertex, vertexShaderName);
    auto pixelShader = loadShader(device_, ShaderType::Pixel, technique.shader.pixel);
    if (!vertexShader || !pixelShader) {
        return nullptr;
    }

    GraphicsPipelineDesc desc{};
    desc.name = technique.debugName;
    if (key.objectBindingMode == ObjectBindingMode::InstancedBatch) {
        desc.name += "InstancedBatch";
    }
    desc.vs = vertexShader->get();
    desc.ps = pixelShader->get();
    desc.vertexLayout = technique.vertexLayout;
    desc.topology = technique.topology;
    const bool materialSurface = technique.materialBindings != MaterialBindingProfile::None;
    desc.cullMode = materialSurface ? (key.doubleSided ? CullMode::None : CullMode::Back) : technique.cullMode;
    desc.frontFace = key.reverseWinding ? FrontFace::Clockwise : FrontFace::CounterClockwise;
    desc.fillMode = FillMode::Solid;
    desc.depthStencil.depthEnable = technique.depthTest;
    desc.depthStencil.depthWrite =
            technique.depthWrite && (!materialSurface || key.alphaMode != graphics::AlphaMode::Blend);
    desc.depthStencil.depthFunc = technique.depthFunc;
    desc.blend = technique.blend;
    if (materialSurface && key.alphaMode == graphics::AlphaMode::Blend) {
        auto& target = desc.blend.renderTargets[0];
        target.blendEnable = true;
        target.srcBlend = BlendFactor::SrcAlpha;
        target.dstBlend = BlendFactor::InvSrcAlpha;
        target.blendOp = BlendOp::Add;
        target.srcBlendAlpha = BlendFactor::One;
        target.dstBlendAlpha = BlendFactor::InvSrcAlpha;
        target.blendOpAlpha = BlendOp::Add;
    }

    using PB = PipelineBinding;
    desc.descriptorBindings[0] = { .binding = 0,
                                   .count = 1,
                                   .type = DescriptorType::UniformBuffer,
                                   .stages = PB::kStageVertex | PB::kStageFragment,
                                   .mode = BindingMode::Dynamic };
    desc.descriptorBindings[1] = { .binding = 1,
                                   .count = 1,
                                   .type = DescriptorType::UniformBuffer,
                                   .stages = PB::kStageVertex | PB::kStageFragment,
                                   .mode = BindingMode::Dynamic };
    desc.descriptorBindings[2] = { .binding = 2,
                                   .count = 1,
                                   .type = DescriptorType::UniformBuffer,
                                   .stages = PB::kStageFragment,
                                   .mode = BindingMode::Dynamic };
    uint8_t bindingCount = 3;
    const auto addTextureBinding = [&](uint8_t binding) {
        desc.descriptorBindings[bindingCount++] = {
            .binding = binding, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
        };
    };
    const auto addSamplerBinding = [&] {
        desc.descriptorBindings[bindingCount++] = {
            .binding = 8, .count = 1, .type = DescriptorType::Sampler, .stages = PB::kStageFragment
        };
    };
    switch (technique.materialBindings) {
    case MaterialBindingProfile::None: break;
    case MaterialBindingProfile::Unlit:
        addTextureBinding(3);
        addSamplerBinding();
        addTextureBinding(13);
        break;
    case MaterialBindingProfile::Legacy:
        for (uint8_t binding = 3; binding <= 7; ++binding)
            addTextureBinding(binding);
        addSamplerBinding();
        addTextureBinding(12);
        addTextureBinding(13);
        break;
    case MaterialBindingProfile::PBR:
        for (uint8_t binding = 3; binding <= 7; ++binding) {
            desc.descriptorBindings[bindingCount++] = {
                .binding = binding, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
            };
        }
        addSamplerBinding();
        for (uint8_t binding = 9; binding <= 11; ++binding)
            addTextureBinding(binding);
        addTextureBinding(13);
        break;
    }
    desc.descriptorBindingCount = bindingCount;
    desc.colorFormats[0] = key.colorFormat;
    desc.colorTargetCount = 1;
    desc.depthStencilFormat = key.hasDepth ? key.depthFormat : TextureFormat::Unknown;
    desc.sampleCount = key.sampleCount;

    auto pipeline = device_.createPipelineState(desc);
    if (!pipeline) {
        return nullptr;
    }
    Entry entry{ std::move(*vertexShader), std::move(*pixelShader), std::move(*pipeline) };
    auto [inserted, added] = entries_.emplace(key, std::move(entry));
    return added ? inserted->second.pipeline.get() : nullptr;
}

}  // namespace mulan::engine
