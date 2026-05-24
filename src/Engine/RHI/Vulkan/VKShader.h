/**
 * @file VKShader.h
 * @brief Vulkan着色器实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../Shader.h"
#include "VkConvert.h"

namespace MulanGeo::engine {

class VKShader : public Shader {
public:
    VKShader(const ShaderDesc& desc, vk::Device device);
    ~VKShader();

    const ShaderDesc& desc() const override { return m_desc; }

    vk::ShaderModule module() const { return m_module; }
    vk::PipelineShaderStageCreateInfo stageCreateInfo() const;

private:
    void createFromSPIRV(const uint8_t* code, uint32_t size);
    void loadSPIRVFromFile(std::string_view path);
    static vk::ShaderStageFlagBits toVkShaderStage(ShaderType type);

    ShaderDesc     m_desc;
    vk::Device     m_device;
    vk::ShaderModule m_module;
};

} // namespace MulanGeo::Engine
