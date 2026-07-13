/**
 * @file dx11_shader.h
 * @brief D3D11 着色器实现（DXBC/DXIL 字节码）
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/shader.h"
#include "dx11_common.h"

#include <vector>

namespace mulan::engine {

class DX11Shader final : public Shader {
public:
    DX11Shader(const ShaderDesc& desc, ID3D11Device* device);
    ~DX11Shader() = default;

    const ShaderDesc& desc() const override { return m_desc; }

    ID3D11VertexShader* vsShader() const { return m_vs.Get(); }
    ID3D11PixelShader* psShader() const { return m_ps.Get(); }
    ID3D11GeometryShader* gsShader() const { return m_gs.Get(); }
    bool isValid() const { return m_vs || m_ps || m_gs; }

    const void* byteCodeData() const { return m_byteCode.data(); }
    size_t byteCodeSize() const { return m_byteCode.size(); }

private:
    ShaderDesc m_desc;
    std::vector<uint8_t> m_byteCode;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11GeometryShader> m_gs;
};

}  // namespace mulan::engine
