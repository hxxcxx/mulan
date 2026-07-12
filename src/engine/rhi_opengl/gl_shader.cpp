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
            std::fprintf(stderr, "[GLShader] Bytecode requires SPIR-V language\n");
    }

    if (shader_) {
        std::fprintf(stdout, "[GLShader] Created %s (handle: %u)\n", std::string(desc.name).c_str(), shader_);
    }
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
        std::fprintf(stderr, "[GLShader] Invalid SPIR-V code\n");
        return;
    }

    // 创建着色器对象
    GLenum glShaderType = toGLShaderType(desc_.type);
    shader_ = glCreateShader(glShaderType);
    if (shader_ == 0) {
        std::fprintf(stderr, "[GLShader] glCreateShader failed\n");
        return;
    }

    // 加载 SPIR-V 二进制（GL 4.6+）
    glShaderBinary(1, &shader_, GL_SHADER_BINARY_FORMAT_SPIR_V, spirvCode, byteSize);

    // 特化着色器（绑定 entry point）
    const char* entryPoint = desc_.entryPoint.empty() ? "main" : desc_.entryPoint.data();
    if (!glSpecializeShader) {
        std::fprintf(stderr, "[GLShader] glSpecializeShader is unavailable\n");
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
        std::fprintf(stderr, "[GLShader] Failed to open file: %s\n", std::string(filePath).c_str());
        return;
    }

    // 读取文件大小
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize <= 0 || (fileSize % 4) != 0) {
        std::fprintf(stderr, "[GLShader] Invalid SPIR-V file size: %ld (must be multiple of 4)\n", fileSize);
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
// GLSL 源码路径（native OpenGL）
// ================================================================

void GLShader::createFromGLSL(const char* source, int length) {
    GLenum glShaderType = toGLShaderType(desc_.type);
    shader_ = glCreateShader(glShaderType);
    if (shader_ == 0) {
        std::fprintf(stderr, "[GLShader] glCreateShader failed\n");
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
    if (fopen_s(&file, std::string(filePath).c_str(), "r") != 0 || !file) {
#else
    file = fopen(std::string(filePath).c_str(), "r");
    if (!file) {
#endif
        std::fprintf(stderr, "[GLShader] Failed to open GLSL file: %s\n", std::string(filePath).c_str());
        return;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize <= 0) {
        std::fprintf(stderr, "[GLShader] Empty GLSL file: %s\n", std::string(filePath).c_str());
        fclose(file);
        return;
    }

    std::vector<char> src(fileSize + 1, '\0');
    fread(src.data(), 1, fileSize, file);
    fclose(file);

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
        std::fprintf(stderr, "[GLShader] SPIR-V requires OpenGL 4.6+, you have %d.%d\n", major, minor);
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
    default: std::fprintf(stderr, "[GLShader] Unknown shader type: %d\n", (int) type); return GL_VERTEX_SHADER;
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
            std::fprintf(stderr, "[GLShader] Compilation failed for '%s':\n%s\n", shaderName, log.data());
        } else {
            std::fprintf(stderr, "[GLShader] Compilation failed for '%s' (no log)\n", shaderName);
        }

        glDeleteShader(shader);
        return false;
    }

    std::fprintf(stdout, "[GLShader] Successfully specialized '%s'\n", shaderName);
    return true;
}

}  // namespace mulan::engine
