/**
 * @file GLShader.cpp
 * @brief OpenGL 着色器实现 — SPIR-V 预编译支持
 * @author terry
 * @date 2026-04-16
 * 
 * Note: Requires OpenGL 4.6+ for SPIR-V support.
 * GLAD is configured for GL 4.6 with glShaderBinary() and glSpecializeShader()
 * available directly from glad.h
 */

#include "GLShader.h"

#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

namespace mulan::engine {

// ================================================================
// 构造 / 析构
// ================================================================

GLShader::GLShader(const ShaderDesc& desc)
    : m_desc(desc)
{
    // 仅在 GL 4.6+ 时支持 SPIR-V
    if (!isSPIRVSupported()) {
        std::fprintf(stderr,
            "[GLShader] OpenGL 4.6+ required for SPIR-V support. "
            "Your driver may not support glSpecializeShader().\n");
        return;
    }

    // 从字节码加载
    if (desc.byteCode && desc.byteCodeSize > 0) {
        createFromSPIRV(desc.byteCode, desc.byteCodeSize);
    }
    // 从文件加载
    else if (!desc.filePath.empty()) {
        loadSPIRVFromFile(desc.filePath);
    }

    if (m_shader) {
        std::fprintf(stdout, "[GLShader] Created %s (handle: %u)\n",
            std::string(desc.name).c_str(), m_shader);
    }
}

GLShader::~GLShader() {
    if (m_shader) {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

// ================================================================
// 从 SPIR-V 字节码创建着色器
// ================================================================

void GLShader::createFromSPIRV(const uint8_t* spirvCode, uint32_t byteSize) {
    if (!spirvCode || byteSize == 0) {
        std::fprintf(stderr, "[GLShader] Invalid SPIR-V code\n");
        return;
    }

    // 创建着色器对象
    GLenum glShaderType = toGLShaderType(m_desc.type);
    m_shader = glCreateShader(glShaderType);
    if (m_shader == 0) {
        std::fprintf(stderr, "[GLShader] glCreateShader failed\n");
        return;
    }

    // 加载 SPIR-V 二进制（GL 4.6+）
    glShaderBinary(1, &m_shader, GL_SHADER_BINARY_FORMAT_SPIR_V,
                   spirvCode, byteSize);

    // 特化着色器（绑定 entry point）
    const char* entryPoint = m_desc.entryPoint.empty() ? "main"
                                                       : m_desc.entryPoint.data();
    glSpecializeShader(m_shader, entryPoint, 0, nullptr, nullptr);

    checkCompileError(m_shader, std::string(m_desc.name).c_str());
}

// ================================================================
// 从文件加载 SPIR-V
// ================================================================

void GLShader::loadSPIRVFromFile(std::string_view filePath) {
    FILE* file = nullptr;
#ifdef _WIN32
    if (fopen_s(&file, std::string(filePath).c_str(), "rb") != 0 || !file) {
#else
    file = fopen(std::string(filePath).c_str(), "rb");
    if (!file) {
#endif
        std::fprintf(stderr, "[GLShader] Failed to open file: %s\n",
            std::string(filePath).c_str());
        return;
    }

    // 读取文件大小
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize <= 0 || (fileSize % 4) != 0) {
        std::fprintf(stderr,
            "[GLShader] Invalid SPIR-V file size: %ld (must be multiple of 4)\n",
            fileSize);
        fclose(file);
        return;
    }

    // 读取 SPIR-V 数据
    std::vector<uint8_t> spirvData(fileSize);
    if (fread(spirvData.data(), 1, fileSize, file) != static_cast<size_t>(fileSize)) {
        std::fprintf(stderr, "[GLShader] Failed to read SPIR-V file\n");
        fclose(file);
        return;
    }
    fclose(file);

    createFromSPIRV(spirvData.data(), static_cast<uint32_t>(fileSize));
}

// ================================================================
// 检查 GL 版本是否支持 SPIR-V
// ================================================================

bool GLShader::isSPIRVSupported() {
    // GL 4.6+ 才能使用 glSpecializeShader()
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    bool supported = (major > 4) || (major == 4 && minor >= 6);
    if (!supported) {
        std::fprintf(stderr,
            "[GLShader] SPIR-V requires OpenGL 4.6+, you have %d.%d\n",
            major, minor);
    }
    return supported;
}

// ================================================================
// 将 ShaderType 转换为 OpenGL 常量
// ================================================================

GLenum GLShader::toGLShaderType(ShaderType type) {
    switch (type) {
    case ShaderType::Vertex:
        return GL_VERTEX_SHADER;
    case ShaderType::Pixel:  // Fragment
        return GL_FRAGMENT_SHADER;
    case ShaderType::Geometry:
        return GL_GEOMETRY_SHADER;
    case ShaderType::Compute:
        return GL_COMPUTE_SHADER;
    case ShaderType::TessControl:  // Hull
        return GL_TESS_CONTROL_SHADER;
    case ShaderType::TessEvaluation:  // Domain
        return GL_TESS_EVALUATION_SHADER;
    default:
        std::fprintf(stderr, "[GLShader] Unknown shader type: %d\n", (int)type);
        return GL_VERTEX_SHADER;
    }
}

// ================================================================
// 检查着色器编译/特化错误
// ================================================================

void GLShader::checkCompileError(GLuint shader, const char* shaderName) {
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            std::vector<GLchar> log(logLength);
            glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            std::fprintf(stderr, "[GLShader] Compilation failed for '%s':\n%s\n",
                shaderName, log.data());
        } else {
            std::fprintf(stderr, "[GLShader] Compilation failed for '%s' (no log)\n",
                shaderName);
        }

        glDeleteShader(shader);
        return;
    }

    std::fprintf(stdout, "[GLShader] Successfully specialized '%s'\n", shaderName);
}

} // namespace mulan::engine
