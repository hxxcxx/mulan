#include "detail/dx11_shader.h"

#include <stdexcept>

namespace mulan::engine {

DX11Shader::DX11Shader(const ShaderDesc& desc, ID3D11Device* device) : m_desc(desc) {
    if (!device)
        throw std::invalid_argument("DX11Shader requires a valid device");
    if (desc.language != ShaderSourceLanguage::DXBC)
        throw std::invalid_argument("D3D11 requires precompiled DXBC shader bytecode");

    if (desc.byteCode && desc.byteCodeSize > 0) {
        m_byteCode.assign(desc.byteCode, desc.byteCode + desc.byteCodeSize);
    }

    if (m_byteCode.empty())
        throw std::invalid_argument("DX11Shader bytecode is empty");

    const void* code = m_byteCode.data();
    size_t size = m_byteCode.size();

    switch (desc.type) {
    case ShaderType::Vertex: DX11_CHECK(device->CreateVertexShader(code, size, nullptr, &m_vs)); break;
    case ShaderType::Pixel: DX11_CHECK(device->CreatePixelShader(code, size, nullptr, &m_ps)); break;
    case ShaderType::Geometry: DX11_CHECK(device->CreateGeometryShader(code, size, nullptr, &m_gs)); break;
    default: throw std::invalid_argument("DX11Shader stage is not supported by the graphics pipeline");
    }
}

}  // namespace mulan::engine
