#include "vk_shader.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <cstdio>
#include <string>
#include <vector>

namespace mulan::engine {

core::Result<std::unique_ptr<VKShader>> VKShader::create(const ShaderDesc& desc, vk::Device device) {
    // 收集 SPIR-V 字节码：优先 byteCode，否则从 filePath 读取
    std::vector<uint32_t> codeOwned;
    const uint32_t* codePtr = nullptr;
    uint32_t codeSize = 0;

    if (desc.byteCode && desc.byteCodeSize > 0) {
        codeSize = desc.byteCodeSize;
        codePtr = reinterpret_cast<const uint32_t*>(desc.byteCode);
    } else if (!desc.filePath.empty()) {
        FILE* f = nullptr;
        if (fopen_s(&f, std::string(desc.filePath).c_str(), "rb") != 0 || !f) {
            return std::unexpected(makeError(EngineErrorCode::ShaderFileNotFound,
                                             "Failed to open SPIR-V file: " + std::string(desc.filePath)));
        }
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fileSize <= 0 || (fileSize % 4) != 0) {
            fclose(f);
            return std::unexpected(makeError(EngineErrorCode::ShaderCompileFailed,
                                             "Invalid SPIR-V file size: " + std::string(desc.filePath)));
        }
        codeOwned.resize(fileSize / 4);
        fread(codeOwned.data(), 1, fileSize, f);
        fclose(f);
        codeSize = static_cast<uint32_t>(fileSize);
        codePtr = codeOwned.data();
    } else {
        // 既无 byteCode 也无 filePath：当前静默行为升级为错误
        return std::unexpected(
                makeError(EngineErrorCode::ShaderCompileFailed, "ShaderDesc has neither byteCode nor filePath"));
    }

    auto obj = std::unique_ptr<VKShader>(new VKShader(desc, device));

    vk::ShaderModuleCreateInfo ci;
    ci.codeSize = codeSize;
    ci.pCode = codePtr;
    try {
        obj->module_ = device.createShaderModule(ci);
    } catch (const vk::Error& e) {
        return std::unexpected(
                makeError(EngineErrorCode::ShaderCompileFailed, std::string("createShaderModule failed: ") + e.what()));
    }

    return obj;
}

VKShader::~VKShader() {
    if (module_) {
        device_.destroyShaderModule(module_);
    }
}

vk::PipelineShaderStageCreateInfo VKShader::stageCreateInfo() const {
    vk::PipelineShaderStageCreateInfo ci;
    ci.stage = toVkShaderStage(desc_.type);
    ci.module = module_;
    ci.pName = desc_.entryPoint.empty() ? "main" : desc_.entryPoint.data();
    return ci;
}

vk::ShaderStageFlagBits VKShader::toVkShaderStage(ShaderType type) {
    switch (type) {
    case ShaderType::Vertex: return vk::ShaderStageFlagBits::eVertex;
    case ShaderType::Pixel: return vk::ShaderStageFlagBits::eFragment;
    case ShaderType::Geometry: return vk::ShaderStageFlagBits::eGeometry;
    case ShaderType::Compute: return vk::ShaderStageFlagBits::eCompute;
    case ShaderType::TessControl: return vk::ShaderStageFlagBits::eTessellationControl;
    case ShaderType::TessEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
    default: return vk::ShaderStageFlagBits::eVertex;
    }
}

}  // namespace mulan::engine
