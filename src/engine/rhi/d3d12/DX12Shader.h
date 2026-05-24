/**
 * @file DX12Shader.h
 * @brief D3D12 着色器实现（DXIL 字节码）
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../Shader.h"
#include "DX12Common.h"

#include <vector>

namespace mulan::engine {

class DX12Shader final : public Shader {
public:
    DX12Shader(const ShaderDesc& desc);
    ~DX12Shader() = default;

    const ShaderDesc& desc() const override { return m_desc; }

    D3D12_SHADER_BYTECODE byteCode() const {
        return D3D12_SHADER_BYTECODE{
            m_byteCode.data(),
            m_byteCode.size()
        };
    }

private:
    ShaderDesc              m_desc;
    std::vector<uint8_t>    m_byteCode;
};

} // namespace mulan::Engine
