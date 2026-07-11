#include "detail/dx12_shader.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace mulan::engine {

core::Result<std::unique_ptr<DX12Shader>> DX12Shader::create(const ShaderDesc& desc) {
    auto obj = std::unique_ptr<DX12Shader>(new DX12Shader(desc));

    bool fromFile = false;
    if (desc.byteCode && desc.byteCodeSize > 0) {
        obj->byte_code_.assign(desc.byteCode, desc.byteCode + desc.byteCodeSize);
    } else if (!desc.filePath.empty()) {
        // 加载预编译的 DXIL 字节码文件
        fromFile = true;
        if (!obj->loadFromFile(desc.filePath)) {
            return std::unexpected(makeError(EngineErrorCode::ShaderFileNotFound,
                                             "Failed to read DXIL file: " + std::string(desc.filePath)));
        }
    } else if (!desc.source.empty()) {
        // 运行时 HLSL 编译 — 当前不支持，需要预编译 DXIL
        // 未来可通过 DXC 集成实现
        return std::unexpected(makeError(EngineErrorCode::ShaderCompileFailed,
                                         "DX12Shader runtime HLSL compile not supported, provide precompiled DXIL"));
    }

    if (obj->byte_code_.empty()) {
        return std::unexpected(
                makeError(fromFile ? EngineErrorCode::ShaderCompileFailed : EngineErrorCode::ShaderCompileFailed,
                          "DX12Shader ended up with empty bytecode"));
    }

    return obj;
}

bool DX12Shader::loadFromFile(std::string_view path) {
    FILE* f = nullptr;
    std::string pathStr(path);
    if (fopen_s(&f, pathStr.c_str(), "rb") != 0 || !f)
        return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(f);
        return false;
    }

    byte_code_.resize(static_cast<size_t>(fileSize));
    size_t readBytes = fread(byte_code_.data(), 1, static_cast<size_t>(fileSize), f);
    fclose(f);

    if (readBytes != static_cast<size_t>(fileSize)) {
        byte_code_.clear();
        return false;
    }
    return true;
}

}  // namespace mulan::engine
