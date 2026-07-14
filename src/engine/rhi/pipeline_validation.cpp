#include "pipeline_validation.h"

#include "device.h"
#include "engine_error_code.h"

#include <array>

namespace mulan::engine {

namespace {

core::Result<void> validateBindings(const PipelineBinding* bindings, uint8_t count, uint8_t capacity,
                                    uint32_t allowedStages) {
    if (count > capacity)
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                         "Pipeline descriptor binding count exceeds its fixed capacity"));

    std::array<uint32_t, GraphicsPipelineDesc::kMaxDescriptorBindings> usedBindings{};
    uint8_t usedCount = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const PipelineBinding& binding = bindings[i];
        if (binding.count == 0)
            return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                             "Pipeline descriptor binding count must be greater than zero"));
        if (binding.stages == 0 || (binding.stages & ~allowedStages) != 0)
            return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                             "Pipeline descriptor binding contains unsupported shader stages"));
        if (binding.mode == BindingMode::Dynamic && binding.type != DescriptorType::UniformBuffer) {
            return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                             "Only uniform buffers may use dynamic binding mode"));
        }
        for (uint8_t j = 0; j < usedCount; ++j) {
            if (usedBindings[j] == binding.binding)
                return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                                 "Pipeline descriptor bindings must be unique"));
        }
        usedBindings[usedCount++] = binding.binding;
    }
    return {};
}

core::Result<void> validateShader(const Shader* shader, ShaderType expectedType, const RHIDevice& device,
                                  const char* missingMessage) {
    if (!shader)
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed, missingMessage));
    if (shader->type() != expectedType)
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed, "Pipeline shader stage is invalid"));
    if (!shader->isTracked() || !shader->belongsTo(device)) {
        return std::unexpected(
                makeError(EngineErrorCode::PipelineCreateFailed, "Pipeline shader belongs to a different device"));
    }
    return {};
}

core::Result<void> validatePushConstants(uint32_t size, bool supported) {
    if (size == 0)
        return {};
    if (!supported)
        return std::unexpected(
                makeError(EngineErrorCode::PipelineCreateFailed, "The active backend does not support push constants"));
    if (size > 128 || (size & 3U) != 0)
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                         "Push constant size must be 4-byte aligned and no greater than 128 bytes"));
    return {};
}

}  // namespace

core::Result<void> validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc, const RHIDevice& device,
                                                const GPUDeviceCapabilities& capabilities) {
    if (auto result = validateShader(desc.vs, ShaderType::Vertex, device, "Graphics pipeline requires a vertex shader");
        !result)
        return result;
    if (desc.ps) {
        if (auto result = validateShader(desc.ps, ShaderType::Pixel, device, "Graphics pipeline shader is missing");
            !result)
            return result;
    }
    if (desc.gs) {
        if (!capabilities.geometryShader)
            return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                             "The active backend does not support geometry shaders"));
        if (auto result = validateShader(desc.gs, ShaderType::Geometry, device, "Graphics pipeline shader is missing");
            !result)
            return result;
    }

    constexpr uint32_t graphicsStages =
            PipelineBinding::kStageVertex | PipelineBinding::kStageGeometry | PipelineBinding::kStageFragment;
    if (auto result = validateBindings(desc.descriptorBindings, desc.descriptorBindingCount,
                                       GraphicsPipelineDesc::kMaxDescriptorBindings, graphicsStages);
        !result)
        return result;
    if (auto result = validatePushConstants(desc.pushConstantSize, capabilities.pushConstants); !result)
        return result;

    if (desc.colorTargetCount > GraphicsPipelineDesc::kMaxRenderTargets)
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                         "Graphics pipeline color target count exceeds its fixed capacity"));
    for (uint8_t i = 0; i < desc.colorTargetCount; ++i) {
        if (desc.colorFormats[i] == TextureFormat::Unknown)
            return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                             "Graphics pipeline color target format must be specified"));
    }
    if ((desc.depthStencil.depthEnable || desc.depthStencil.stencilEnable) &&
        desc.depthStencilFormat == TextureFormat::Unknown) {
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                         "Depth or stencil testing requires a depth-stencil target format"));
    }
    if (desc.sampleCount == 0 || (desc.sampleCount & (desc.sampleCount - 1)) != 0 ||
        desc.sampleCount > capabilities.maxSampleCount) {
        return std::unexpected(
                makeError(EngineErrorCode::PipelineCreateFailed, "Graphics pipeline sample count is unsupported"));
    }
    return {};
}

core::Result<void> validateComputePipelineDesc(const ComputePipelineDesc& desc, const RHIDevice& device,
                                               const GPUDeviceCapabilities& capabilities) {
    if (!capabilities.computeShader)
        return std::unexpected(makeError(EngineErrorCode::BackendNotSupported,
                                         "The active backend does not support compute pipelines"));
    if (auto result =
                validateShader(desc.cs, ShaderType::Compute, device, "Compute pipeline requires a compute shader");
        !result)
        return result;
    if (auto result = validateBindings(desc.descriptorBindings, desc.descriptorBindingCount,
                                       ComputePipelineDesc::kMaxDescriptorBindings, PipelineBinding::kStageCompute);
        !result)
        return result;
    return validatePushConstants(desc.pushConstantSize, capabilities.pushConstants);
}

}  // namespace mulan::engine
