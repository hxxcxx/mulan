/**
 * @file vk_shader.h
 * @brief Vulkan着色器实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../shader.h"
#include "vk_convert.h"

namespace mulan::engine {

class VKShader : public Shader {
public:
    VKShader(const ShaderDesc& desc, vk::Device device);
    ~VKShader();

    const ShaderDesc& desc() const override { return desc_; }

    vk::ShaderModule module() const { return module_; }
    vk::PipelineShaderStageCreateInfo stageCreateInfo() const;

private:
    void createFromSPIRV(const uint8_t* code, uint32_t size);
    void loadSPIRVFromFile(std::string_view path);
    static vk::ShaderStageFlagBits toVkShaderStage(ShaderType type);

    ShaderDesc     desc_;
    vk::Device     device_;
    vk::ShaderModule module_;
};

} // namespace mulan::engine
