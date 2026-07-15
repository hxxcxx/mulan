/**
 * @file dx12_shader.h
 * @brief D3D12 着色器实现（DXIL 字节码）
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/shader.h"
#include "dx12_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <vector>

namespace mulan::engine {

class DX12Shader final : public Shader {
public:
    /// 创建 DX12Shader。文件读失败 → ShaderFileNotFound；无字节码 → ShaderCompileFailed。
    static Result<std::unique_ptr<DX12Shader>> create(const ShaderDesc& desc);
    ~DX12Shader() = default;

    const ShaderDesc& desc() const override { return desc_; }

    D3D12_SHADER_BYTECODE byteCode() const { return D3D12_SHADER_BYTECODE{ byte_code_.data(), byte_code_.size() }; }

private:
    DX12Shader(const ShaderDesc& desc) : desc_(desc) {}

    bool loadFromFile(std::string_view path);

    ShaderDesc desc_;
    std::vector<uint8_t> byte_code_;
};

}  // namespace mulan::engine
