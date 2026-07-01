/**
 * @file dx12_shader.h
 * @brief D3D12 着色器实现（DXIL 字节码）
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../shader.h"
#include "dx12_common.h"

#include <vector>

namespace mulan::engine {

class DX12Shader final : public Shader {
public:
    DX12Shader(const ShaderDesc& desc);
    ~DX12Shader() = default;

    const ShaderDesc& desc() const override { return desc_; }

    D3D12_SHADER_BYTECODE byteCode() const {
        return D3D12_SHADER_BYTECODE{
            byte_code_.data(),
            byte_code_.size()
        };
    }

private:
    ShaderDesc              desc_;
    std::vector<uint8_t>    byte_code_;

    void loadFromFile(std::string_view path);
};

} // namespace mulan::engine
