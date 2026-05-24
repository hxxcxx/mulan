/**
 * @file GLBuffer.h
 * @brief OpenGL 缓冲区实现
 * @author terry
 * @date 2026-04-16
 *
 * 支持以下缓冲区类型：
 * - 顶点缓冲区 (Vertex Buffer)
 * - 索引缓冲区 (Index Buffer)
 * - 统一缓冲区 (Uniform Buffer / UBO)
 * - SSBO (Shader Storage Buffer Object) / 无序访问
 * - 间接参数缓冲区 (Indirect Buffer)
 *
 * OpenGL 缓冲区映射策略：
 * - Immutable: glBufferData() + 内存管理由 GL 负责
 * - Default: glBufferData() + glBufferSubData() 更新
 * - Dynamic: glBufferData() + persistent mapping (GL 4.4+) 或 DISCARD
 * - Staging: glBufferData() + glGetBufferSubData() 回读
 */

#pragma once

#include "GLCommon.h"
#include "../Buffer.h"

#include <vector>

namespace MulanGeo::engine {

class GLBuffer final : public Buffer {
public:
    /// 构造：创建 GL 缓冲区对象
    explicit GLBuffer(const BufferDesc& desc);

    ~GLBuffer();

    // --- Buffer 接口实现 ---

    const BufferDesc& desc() const override { return m_desc; }

    /// CPU 端更新缓冲区数据
    void update(uint32_t offset, uint32_t size, const void* data) override;

    /// CPU 端回读缓冲区数据（仅 Staging buffer 支持）
    bool readback(uint32_t offset, uint32_t size, void* outData) override;

    // --- OpenGL 特有接口 ---

    /// 获取 GL 缓冲区对象
    GLuint handle() const { return m_buffer; }

    /// 检查缓冲区是否成功创建
    bool isValid() const { return m_buffer != 0; }

    /// 获取 GL 缓冲区目标 (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER 等)
    GLenum bufferTarget() const { return m_bufferTarget; }

    /// 获取 GL 缓冲区用途 (GL_STATIC_DRAW, GL_DYNAMIC_DRAW 等)
    GLenum bufferUsage() const { return m_bufferUsage; }

private:
    // --- 内部方法 ---

    /// 确定 GL 缓冲区目标
    static GLenum determineBufferTarget(BufferBindFlags bindFlags);

    /// 确定 GL 缓冲区用途提示
    static GLenum determineBufferUsage(BufferUsage usage);

    /// 创建 GL 缓冲区对象
    void createBuffer();

    /// 上传初始数据
    void uploadInitData();

    /// 更新 Dynamic 缓冲区（使用 DISCARD 或 persistent mapping）
    void updateDynamic(uint32_t offset, uint32_t size, const void* data);

    /// 更新 Default 缓冲区（使用 glBufferSubData）
    void updateDefault(uint32_t offset, uint32_t size, const void* data);

    // --- 成员变量 ---

    BufferDesc        m_desc;
    GLuint            m_buffer = 0;
    GLenum            m_bufferTarget = GL_COPY_READ_BUFFER;
    GLenum            m_bufferUsage = GL_STATIC_DRAW;

    // 对于 Dynamic 缓冲区，缓存待上传的数据
    // （在某些实现中可能需要等待 GPU 完成前一帧的读取）
    std::vector<uint8_t> m_pendingData;
};

} // namespace MulanGeo::Engine
