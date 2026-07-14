#include "detail/dx11_shader.h"

namespace mulan::engine {

DX11Shader::DX11Shader(const ShaderDesc& desc, ID3D11Device* device) : m_desc(desc) {
    if (!device || desc.language != ShaderSourceLanguage::DXBC) {
        LOG_ERROR("[DX11] Shader initialization rejected: invalid device or bytecode language");
        return;
    }

    if (desc.byteCode && desc.byteCodeSize > 0) {
        m_byteCode.assign(desc.byteCode, desc.byteCode + desc.byteCodeSize);
    }

    if (m_byteCode.empty()) {
        LOG_ERROR("[DX11] Shader initialization rejected: empty bytecode");
        return;
    }

    const void* code = m_byteCode.data();
    size_t size = m_byteCode.size();

    switch (desc.type) {
    case ShaderType::Vertex:
        if (!checkDX11(device->CreateVertexShader(code, size, nullptr, &m_vs), "ID3D11Device::CreateVertexShader"))
            return;
        break;
    case ShaderType::Pixel:
        if (!checkDX11(device->CreatePixelShader(code, size, nullptr, &m_ps), "ID3D11Device::CreatePixelShader"))
            return;
        break;
    case ShaderType::Geometry:
        if (!checkDX11(device->CreateGeometryShader(code, size, nullptr, &m_gs), "ID3D11Device::CreateGeometryShader"))
            return;
        break;
    default: LOG_ERROR("[DX11] Shader initialization rejected: unsupported stage"); return;
    }
    m_desc.discardCreationData();
}

}  // namespace mulan::engine
