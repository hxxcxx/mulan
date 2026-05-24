/**
 * @file GLShader.h
 * @brief OpenGL 着色器实现 — SPIR-V 预编译支持（GL 4.6+）
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#include "GLCommon.h"
#include "../Shader.h"

namespace mulan::engine {

/**
 * @brief OpenGL 着色器
 *
 * 支持 SPIR-V 预编译字节码加载（OpenGL 4.6+）
 * 通过 glSpecializeShader() 将 SPIR-V 二进制转换为 GL 着色器对象
 */
class GLShader : public Shader {
public:
    GLShader(const ShaderDesc& desc);
    ~GLShader();

    // Shader 接口
    const ShaderDesc& desc() const override { return m_desc; }

    // GLShader 特定接口
    GLuint handle() const { return m_shader; }
    bool isValid() const { return m_shader != 0; }

private:
    GLShader(const GLShader&) = delete;
    GLShader& operator=(const GLShader&) = delete;

    // 从 SPIR-V 字节码创建着色器
    void createFromSPIRV(const uint8_t* spirvCode, uint32_t byteSize);

    // 从文件加载 SPIR-V
    void loadSPIRVFromFile(std::string_view filePath);

    // 从 GLSL 源码创建着色器（WebGL/Emscripten 路径）
    void createFromGLSL(const char* source, int length = -1);

    // 从文件加载 GLSL 源码（WebGL/Emscripten 路径）
    void loadGLSLFromFile(std::string_view filePath);

    // 检查 GL 版本是否支持 SPIR-V（需要 4.6+）
    static bool isSPIRVSupported();

    // 获取 OpenGL 着色器类型
    static GLenum toGLShaderType(ShaderType type);

    // 检查编译/链接错误
    static void checkCompileError(GLuint shader, const char* shaderName);

    ShaderDesc m_desc;
    GLuint     m_shader = 0;
};

} // namespace mulan::Engine
