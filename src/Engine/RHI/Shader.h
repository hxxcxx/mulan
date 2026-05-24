/**
 * @file Shader.h
 * @brief 着色器资源描述与接口定义
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace MulanGeo::engine {

// ============================================================
// 着色器阶段
// ============================================================

enum class ShaderType : uint8_t {
    Vertex         = 0,
    Pixel          = 1,   // Fragment
    Geometry       = 2,
    Compute        = 3,
    TessControl    = 4,   // Hull
    TessEvaluation = 5,   // Domain
};

// ============================================================
// 着色器源码语言
// ============================================================

enum class ShaderSourceLanguage : uint8_t {
    GLSL,
    HLSL,
    SPIRV,     // Vulkan precompiled
    WGSL,      // WebGPU
};

// ============================================================
// 着色器描述结构体
// ============================================================

struct ShaderDesc {
    std::string_view name;              // 调试名称
    ShaderType       type = ShaderType::Vertex;

    // 源码 — 三种方式任选其一
    std::string_view source;            // 源码文本
    std::string_view filePath;          // 或文件路径
    const uint8_t*   byteCode   = nullptr;  // 或预编译字节码
    uint32_t         byteCodeSize = 0;

    std::string_view entryPoint = "main";
    ShaderSourceLanguage language = ShaderSourceLanguage::GLSL;
};

// ============================================================
// 着色器基类
//
// 平台无关的着色器资源，由 Device 创建，PipelineState 持有。
// ============================================================

class Shader {
public:
    virtual ~Shader() = default;

    virtual const ShaderDesc& desc() const = 0;

    ShaderType type() const { return desc().type; }
    std::string_view name() const { return desc().name; }

protected:
    Shader() = default;
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
};

} // namespace MulanGeo::Engine
