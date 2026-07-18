#include "detail/vk_shader.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace mulan::engine {

Result<std::unique_ptr<VKShader>> VKShader::create(const ShaderDesc& desc, vk::Device device) {
    // 收集 SPIR-V 字节码：优先 byteCode，否则从 filePath 读取
    std::vector<uint32_t> codeOwned;
    const uint32_t* codePtr = nullptr;
    uint32_t codeSize = 0;

    if (desc.byteCode && desc.byteCodeSize > 0) {
        codeSize = desc.byteCodeSize;
        codePtr = reinterpret_cast<const uint32_t*>(desc.byteCode);
    } else if (!desc.filePath.empty()) {
        const std::string filePath(desc.filePath);
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::unexpected(
                    makeError(EngineErrorCode::ShaderFileNotFound, "Failed to open SPIR-V file: " + filePath));
        }

        const std::streamoff fileSize = file.tellg();
        if (fileSize <= 0 || (fileSize % sizeof(uint32_t)) != 0 ||
            fileSize > static_cast<std::streamoff>(std::numeric_limits<uint32_t>::max())) {
            return std::unexpected(
                    makeError(EngineErrorCode::ShaderCompileFailed, "Invalid SPIR-V file size: " + filePath));
        }

        file.seekg(0, std::ios::beg);
        codeOwned.resize(static_cast<size_t>(fileSize) / sizeof(uint32_t));
        if (!file.read(reinterpret_cast<char*>(codeOwned.data()), fileSize)) {
            return std::unexpected(
                    makeError(EngineErrorCode::ShaderCompileFailed, "Failed to read SPIR-V file: " + filePath));
        }
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

    obj->desc_.discardCreationData();
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
