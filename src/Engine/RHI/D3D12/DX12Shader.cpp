/**
 * @file DX12Shader.cpp
 * @brief D3D12 着色器实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Shader.h"
#include <cstring>

namespace MulanGeo::engine {

DX12Shader::DX12Shader(const ShaderDesc& desc)
    : m_desc(desc)
{
    if (desc.byteCode && desc.byteCodeSize > 0) {
        m_byteCode.assign(desc.byteCode, desc.byteCode + desc.byteCodeSize);
    } else if (!desc.source.empty()) {
        // 运行时编译 HLSL — 当前不支持，需要预编译 DXIL
        // 未来可通过 DXC 集成实现
    } else if (!desc.filePath.empty()) {
        // 运行时从文件加载 — 当前不支持
    }
}

} // namespace MulanGeo::Engine
