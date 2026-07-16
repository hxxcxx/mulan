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
    combine(std::hash<uint8_t>{}(static_cast<uint8_t>(key.rasterVariant)));
    return value;
}

PipelineState* DevicePipelineLibrary::acquire(const DevicePipelineKey& key) {
    if (const auto known = entries_.find(key); known != entries_.end()) {
        return known->second.pipeline.get();
    }
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
    desc.cullMode = key.rasterVariant == RasterVariant::DoubleSided ? CullMode::None : technique.cullMode;
    desc.frontFace = key.rasterVariant == RasterVariant::Mirrored ? FrontFace::Clockwise : FrontFace::CounterClockwise;
    desc.fillMode = FillMode::Solid;
    desc.depthStencil.depthEnable = technique.depthTest;
    desc.depthStencil.depthWrite = technique.depthWrite;
    desc.depthStencil.depthFunc = technique.depthFunc;
    desc.blend = technique.blend;

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
    if (technique.sampleTextures) {
        for (uint8_t binding = 3; binding <= 7; ++binding) {
            desc.descriptorBindings[bindingCount++] = {
                .binding = binding, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
            };
        }
        desc.descriptorBindings[bindingCount++] = {
            .binding = 8, .count = 1, .type = DescriptorType::Sampler, .stages = PB::kStageFragment
        };
        for (uint8_t binding = 9; binding <= 11; ++binding) {
            desc.descriptorBindings[bindingCount++] = {
                .binding = binding, .count = 1, .type = DescriptorType::TextureSRV, .stages = PB::kStageFragment
            };
        }
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
