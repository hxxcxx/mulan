#include "detail/gl_shader.h"

#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

namespace mulan::engine {

// ================================================================
// 构造 / 析构
// ================================================================

GLShader::GLShader(const ShaderDesc& desc) : desc_(desc) {
    if (!desc.source.empty()) {
        createFromGLSL(desc.source.data(), static_cast<int>(desc.source.size()));
    } else if (!desc.filePath.empty()) {
        if (desc.language == ShaderSourceLanguage::GLSL)
            loadGLSLFromFile(desc.filePath);
        else
            loadSPIRVFromFile(desc.filePath);
    } else if (desc.byteCode && desc.byteCodeSize > 0) {
        if (desc.language == ShaderSourceLanguage::SPIRV)
            createFromSPIRV(desc.byteCode, desc.byteCodeSize);
        else
            LOG_ERROR("[OpenGL] Shader creation rejected: bytecode requires SPIR-V language");
    }

    if (shader_) {
        LOG_DEBUG("[OpenGL] Shader created: name={}, handle={}", desc.name, shader_);
    }
    desc_.discardCreationData();
}

GLShader::~GLShader() {
    if (shader_) {
        glDeleteShader(shader_);
        shader_ = 0;
    }
}

// ================================================================
// 从 SPIR-V 字节码创建着色器
// ================================================================

void GLShader::createFromSPIRV(const uint8_t* spirvCode, uint32_t byteSize) {
    if (!spirvCode || byteSize == 0) {
        LOG_ERROR("[OpenGL] Shader creation rejected: invalid SPIR-V code");
        return;
    }

    // 创建着色器对象
    GLenum glShaderType = toGLShaderType(desc_.type);
    shader_ = glCreateShader(glShaderType);
    if (shader_ == 0) {
        LOG_ERROR("[OpenGL] Shader creation failed: glCreateShader returned 0");
        return;
    }

    // 加载 SPIR-V 二进制（GL 4.6+）
    glShaderBinary(1, &shader_, GL_SHADER_BINARY_FORMAT_SPIR_V, spirvCode, byteSize);

    // 特化着色器（绑定 entry point）
    const char* entryPoint = desc_.entryPoint.empty() ? "main" : desc_.entryPoint.data();
    if (!glSpecializeShader) {
        LOG_ERROR("[OpenGL] SPIR-V specialization failed: glSpecializeShader is unavailable");
        glDeleteShader(shader_);
        shader_ = 0;
        return;
    }
    glSpecializeShader(shader_, entryPoint, 0, nullptr, nullptr);

    if (!checkCompileError(shader_, std::string(desc_.name).c_str()))
        shader_ = 0;
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
        LOG_ERROR("[OpenGL] Failed to open SPIR-V file: {}", filePath);
        return;
    }

    // 读取文件大小
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize <= 0 || (fileSize % 4) != 0) {
        LOG_ERROR("[OpenGL] Invalid SPIR-V file size: {} (must be a multiple of 4)", fileSize);
        fclose(file);
        return;
    }

    // 读取 SPIR-V 数据
    std::vector<uint8_t> spirvData(fileSize);
    if (fread(spirvData.data(), 1, fileSize, file) != static_cast<size_t>(fileSize)) {
        LOG_ERROR("[OpenGL] Failed to read SPIR-V file: {}", filePath);
        fclose(file);
        return;
    }
    fclose(file);

    createFromSPIRV(spirvData.data(), static_cast<uint32_t>(fileSize));
}

// ================================================================
// GLSL 源码路径（native OpenGL）
// ================================================================

void GLShader::createFromGLSL(const char* source, int length) {
    GLenum glShaderType = toGLShaderType(desc_.type);
    shader_ = glCreateShader(glShaderType);
    if (shader_ == 0) {
        LOG_ERROR("[OpenGL] GLSL shader creation failed: glCreateShader returned 0");
        return;
    }

    glShaderSource(shader_, 1, &source, length >= 0 ? &length : nullptr);
    glCompileShader(shader_);
    if (!checkCompileError(shader_, std::string(desc_.name).c_str())) {
        shader_ = 0;
    }
}

void GLShader::loadGLSLFromFile(std::string_view filePath) {
    FILE* file = nullptr;
#ifdef _WIN32
    if (fopen_s(&file, std::string(filePath).c_str(), "rb") != 0 || !file) {
#else
    file = fopen(std::string(filePath).c_str(), "rb");
    if (!file) {
#endif
        LOG_ERROR("[OpenGL] Failed to open GLSL file: {}", filePath);
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        LOG_ERROR("[OpenGL] Failed to seek GLSL file: {}", filePath);
        fclose(file);
        return;
    }

    const long fileSize = ftell(file);
    if (fileSize <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        LOG_ERROR("[OpenGL] GLSL file is empty or unreadable: {}", filePath);
        fclose(file);
        return;
    }

    std::vector<char> src(static_cast<std::size_t>(fileSize) + 1, '\0');
    const auto bytesRead = fread(src.data(), 1, static_cast<std::size_t>(fileSize), file);
    fclose(file);

    if (bytesRead != static_cast<std::size_t>(fileSize)) {
        LOG_ERROR("[OpenGL] Failed to read complete GLSL file: {}", filePath);
        return;
    }

    createFromGLSL(src.data(), static_cast<int>(fileSize));
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
        LOG_ERROR("[OpenGL] SPIR-V requires OpenGL 4.6 or newer; detected {}.{}", major, minor);
    }
    return supported;
}

// ================================================================
// 将 ShaderType 转换为 OpenGL 常量
// ================================================================

GLenum GLShader::toGLShaderType(ShaderType type) {
    switch (type) {
    case ShaderType::Vertex: return GL_VERTEX_SHADER;
    case ShaderType::Pixel:  // Fragment
        return GL_FRAGMENT_SHADER;
    // Desktop OpenGL shader stages.
    case ShaderType::Geometry: return GL_GEOMETRY_SHADER;
    case ShaderType::Compute: return GL_COMPUTE_SHADER;
    case ShaderType::TessControl:     // Hull
        return GL_TESS_CONTROL_SHADER;
    case ShaderType::TessEvaluation:  // Domain
        return GL_TESS_EVALUATION_SHADER;
    default: LOG_ERROR("[OpenGL] Unknown shader type: {}", static_cast<int>(type)); return GL_VERTEX_SHADER;
    }
}

// ================================================================
// 检查着色器编译/特化错误
// ================================================================

bool GLShader::checkCompileError(GLuint shader, const char* shaderName) {
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            std::vector<GLchar> log(logLength);
            glGetShaderInfoLog(shader, logLength, nullptr, log.data());
            LOG_ERROR("[OpenGL] Shader compilation failed: name={}, diagnostic={}", shaderName, log.data());
        } else {
            LOG_ERROR("[OpenGL] Shader compilation failed: name={}, no diagnostic", shaderName);
        }

        glDeleteShader(shader);
        return false;
    }

    LOG_DEBUG("[OpenGL] Shader compilation/specialization succeeded: name={}", shaderName);
    return true;
}

}  // namespace mulan::engine
