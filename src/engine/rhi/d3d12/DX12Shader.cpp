/**
 * @file DX12Shader.cpp
 * @brief D3D12 着色器实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Shader.h"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace mulan::engine {

DX12Shader::DX12Shader(const ShaderDesc& desc)
    : m_desc(desc)
{
    if (desc.byteCode && desc.byteCodeSize > 0) {
        m_byteCode.assign(desc.byteCode, desc.byteCode + desc.byteCodeSize);
    } else if (!desc.filePath.empty()) {
        // 加载预编译的 DXIL 字节码文件
        loadFromFile(desc.filePath);
    } else if (!desc.source.empty()) {
        // 运行时 HLSL 编译 — 当前不支持，需要预编译 DXIL
        // 未来可通过 DXC 集成实现
    }
}

void DX12Shader::loadFromFile(std::string_view path) {
    FILE* f = nullptr;
    std::string pathStr(path);
    if (fopen_s(&f, pathStr.c_str(), "rb") != 0 || !f) return;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(f);
        return;
    }

    m_byteCode.resize(static_cast<size_t>(fileSize));
    size_t readBytes = fread(m_byteCode.data(), 1, static_cast<size_t>(fileSize), f);
    fclose(f);

    if (readBytes != static_cast<size_t>(fileSize)) {
        m_byteCode.clear();
    }
}

} // namespace mulan::engine
