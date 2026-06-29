/**
 * @file ShaderUtil.h
 * @brief Shader 加载工具 — readFile + loadShader 公共逻辑
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../../rhi/Device.h"
#include "../../rhi/Shader.h"

#include <cstdio>
#include <string>
#include <vector>

namespace mulan::engine {

inline std::vector<uint8_t> readFile(const char* path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(sz > 0 ? sz : 0);
    if (sz > 0) fread(d.data(), 1, sz, f);
    fclose(f);
    return d;
}

inline ResourcePtr<Shader> loadShader(RHIDevice& device, ShaderType type, const char* name) {
#ifdef SHADER_DIR
    std::string dir = SHADER_DIR;
#else
    std::string dir = "shaders";
#endif
    const char* ext = ".spv";
    if (device.backend() == GraphicsBackend::D3D12) ext = ".dxil";

    std::string path = dir + "/" + name + ext;
    auto data = readFile(path.c_str());
    if (data.empty()) return nullptr;

    ShaderDesc d;
    d.type         = type;
    d.byteCode     = data.data();
    d.byteCodeSize = static_cast<uint32_t>(data.size());
    return device.createShader(d);
}

} // namespace mulan::engine
