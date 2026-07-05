/**
 * @file vk_shader.h
 * @brief Vulkan着色器实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../shader.h"
#include "vk_convert.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class VKShader : public Shader {
public:
    /// 创建 VKShader。文件读失败 → ShaderFileNotFound；编译失败 → ShaderCompileFailed。
    static core::Result<std::unique_ptr<VKShader>>
        create(const ShaderDesc& desc, vk::Device device);
    ~VKShader();

    const ShaderDesc& desc() const override { return desc_; }

    vk::ShaderModule module() const { return module_; }
    vk::PipelineShaderStageCreateInfo stageCreateInfo() const;

private:
    VKShader(const ShaderDesc& desc, vk::Device device)
        : desc_(desc), device_(device) {}

    static vk::ShaderStageFlagBits toVkShaderStage(ShaderType type);

    ShaderDesc     desc_;
    vk::Device     device_;
    vk::ShaderModule module_;
};

} // namespace mulan::engine
