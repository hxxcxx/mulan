#include "vk_shader.h"

#include <stdexcept>

namespace mulan::engine {

VKShader::VKShader(const ShaderDesc& desc, vk::Device device)
    : desc_(desc), device_(device)
{
    if (desc.byteCode && desc.byteCodeSize > 0) {
        createFromSPIRV(desc.byteCode, desc.byteCodeSize);
    } else if (!desc.filePath.empty()) {
        loadSPIRVFromFile(desc.filePath);
    }
}

VKShader::~VKShader() {
    if (module_) {
        device_.destroyShaderModule(module_);
    }
}

vk::PipelineShaderStageCreateInfo VKShader::stageCreateInfo() const {
    vk::PipelineShaderStageCreateInfo ci;
    ci.stage  = toVkShaderStage(desc_.type);
    ci.module = module_;
    ci.pName  = desc_.entryPoint.empty() ? "main" : desc_.entryPoint.data();
    return ci;
}

void VKShader::createFromSPIRV(const uint8_t* code, uint32_t size) {
    vk::ShaderModuleCreateInfo ci;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code);
    module_ = device_.createShaderModule(ci);
}

void VKShader::loadSPIRVFromFile(std::string_view path) {
    FILE* f = nullptr;
    if (fopen_s(&f, std::string(path).c_str(), "rb") != 0 || !f)
        throw std::runtime_error("Failed to open SPIR-V file: " + std::string(path));

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || (fileSize % 4) != 0) {
        fclose(f);
        throw std::runtime_error("Invalid SPIR-V file size: " + std::string(path));
    }

    std::vector<uint32_t> code(fileSize / 4);
    fread(code.data(), 1, fileSize, f);
    fclose(f);

    vk::ShaderModuleCreateInfo ci;
    ci.codeSize = fileSize;
    ci.pCode    = code.data();
    module_ = device_.createShaderModule(ci);
}

vk::ShaderStageFlagBits VKShader::toVkShaderStage(ShaderType type) {
    switch (type) {
        case ShaderType::Vertex:         return vk::ShaderStageFlagBits::eVertex;
        case ShaderType::Pixel:          return vk::ShaderStageFlagBits::eFragment;
        case ShaderType::Geometry:       return vk::ShaderStageFlagBits::eGeometry;
        case ShaderType::Compute:        return vk::ShaderStageFlagBits::eCompute;
        case ShaderType::TessControl:    return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderType::TessEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
        default:                         return vk::ShaderStageFlagBits::eVertex;
    }
}

} // namespace mulan::engine
