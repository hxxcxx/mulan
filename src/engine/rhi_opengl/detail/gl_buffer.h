/**
 * @file gl_buffer.h
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

#include "gl_common.h"
#include "../../rhi/buffer.h"

#include <memory>
#include <vector>

namespace mulan::engine {

class GLBuffer final : public Buffer {
public:
    /// 构造：创建 GL 缓冲区对象
    explicit GLBuffer(const BufferDesc& desc);
    static std::unique_ptr<GLBuffer> createTransientUniformPage(uint32_t size);

    ~GLBuffer();

    // --- Buffer 接口实现 ---

    const BufferDesc& desc() const override { return desc_; }

    /// CPU 端更新缓冲区数据
    ResultVoid write(uint32_t offset, uint32_t size, const void* data) override;

    /// CPU 端回读缓冲区数据（仅 Staging buffer 支持）
    ResultVoid readback(uint32_t offset, uint32_t size, void* outData) override;

    // --- OpenGL 特有接口 ---

    /// 获取 GL 缓冲区对象
    GLuint handle() const { return buffer_; }

    /// 检查缓冲区是否成功创建
    bool isValid() const { return buffer_ != 0; }

    /// 获取 GL 缓冲区目标 (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER 等)
    GLenum bufferTarget() const { return buffer_Target; }

    /// 获取 GL 缓冲区用途 (GL_STATIC_DRAW, GL_DYNAMIC_DRAW 等)
    GLenum bufferUsage() const { return buffer_Usage; }
    void* mappedData() const { return mapped_data_; }
    bool isTransientUniformPage() const { return transient_uniform_page_; }

private:
    explicit GLBuffer(uint32_t transientUniformPageSize);
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
    ResultVoid updateDynamic(uint32_t offset, uint32_t size, const void* data);

    /// 更新 Default 缓冲区（使用 glNamedBufferSubData）
    ResultVoid updateDefault(uint32_t offset, uint32_t size, const void* data);

    // --- 成员变量 ---

    BufferDesc desc_;
    GLuint buffer_ = 0;
    GLenum buffer_Target = GL_COPY_READ_BUFFER;
    GLenum buffer_Usage = GL_STATIC_DRAW;

    // 对于 Dynamic 缓冲区，缓存待上传的数据
    // （在某些实现中可能需要等待 GPU 完成前一帧的读取）
    std::vector<uint8_t> pending_data_;
    void* mapped_data_ = nullptr;
    bool transient_uniform_page_ = false;
};

}  // namespace mulan::engine
