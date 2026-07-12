#include "detail/gl_buffer.h"
#include <cstring>
#include <cstdio>
#include <string>

namespace mulan::engine {

// ============================================================
// 内部工具函数
// ============================================================

namespace {

/// 确定主要的缓冲区目标
GLenum determinePrimaryTarget(BufferBindFlags bindFlags) {
    // 选择 "最优" 的目标以进行初始绑定
    // 注意：OpenGL 缓冲区可以绑定到多个目标
    if (bindFlags & BufferBindFlags::VertexBuffer) {
        return GL_ARRAY_BUFFER;
    }
    if (bindFlags & BufferBindFlags::IndexBuffer) {
        return GL_ELEMENT_ARRAY_BUFFER;
    }
    if (bindFlags & BufferBindFlags::UniformBuffer) {
        return GL_UNIFORM_BUFFER;
    }
    // Desktop OpenGL supports SSBO and indirect buffers.
    if (bindFlags & BufferBindFlags::ShaderResource) {
        return GL_SHADER_STORAGE_BUFFER;
    }
    if (bindFlags & BufferBindFlags::UnorderedAccess) {
        return GL_SHADER_STORAGE_BUFFER;
    }
    if (bindFlags & BufferBindFlags::IndirectBuffer) {
        return GL_DRAW_INDIRECT_BUFFER;
    }
    return GL_COPY_READ_BUFFER;  // 默认
}

/// 将 BufferUsage 转换为 GL usage hint
GLenum getGLUsageHint(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Immutable: return GL_STATIC_DRAW;
    case BufferUsage::Default: return GL_STATIC_DRAW;
    case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
    case BufferUsage::Staging: return GL_STREAM_READ;
    default: return GL_STATIC_DRAW;
    }
}

}  // anonymous namespace

// ============================================================
// GLBuffer 实现
// ============================================================

GLBuffer::GLBuffer(const BufferDesc& desc) : desc_(desc) {
    buffer_Target = determinePrimaryTarget(desc.bindFlags);
    buffer_Usage = getGLUsageHint(desc.usage);

    createBuffer();
}

GLBuffer::~GLBuffer() {
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
}

GLenum GLBuffer::determineBufferTarget(BufferBindFlags bindFlags) {
    return determinePrimaryTarget(bindFlags);
}

GLenum GLBuffer::determineBufferUsage(BufferUsage usage) {
    return getGLUsageHint(usage);
}

void GLBuffer::createBuffer() {
    // 创建缓冲区对象
    glGenBuffers(1, &buffer_);
    if (buffer_ == 0) {
        std::fprintf(stderr, "[GLBuffer] glGenBuffers failed\n");
        return;
    }

    // 绑定缓冲区
    glBindBuffer(buffer_Target, buffer_);

    // 分配内存并上传初始数据
    if (desc_.initData) {
        glBufferData(buffer_Target, desc_.size, desc_.initData, buffer_Usage);
    } else {
        glBufferData(buffer_Target, desc_.size, nullptr, buffer_Usage);
    }

    // 检查错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLBuffer] glBufferData failed (error: 0x%X, name: %s)\n", err,
                     std::string(desc_.name).c_str());
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        return;
    }

    std::fprintf(stdout, "[GLBuffer] Created buffer (handle: %u, size: %u, name: %s)\n", buffer_, desc_.size,
                 std::string(desc_.name).c_str());
}

void GLBuffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (!isValid() || !data)
        return;

    if (offset + size > desc_.size) {
        std::fprintf(stderr, "[GLBuffer] Update out of bounds (offset: %u, size: %u, buffer size: %u)\n", offset, size,
                     desc_.size);
        return;
    }

    switch (desc_.usage) {
    case BufferUsage::Immutable:
        // 不可修改
        std::fprintf(stderr, "[GLBuffer] Cannot update Immutable buffer\n");
        break;

    case BufferUsage::Default: updateDefault(offset, size, data); break;

    case BufferUsage::Dynamic: updateDynamic(offset, size, data); break;

    case BufferUsage::Staging:
        // Staging 缓冲区应使用 readback()，如果必要可以 update
        updateDefault(offset, size, data);
        break;
    }
}

void GLBuffer::updateDefault(uint32_t offset, uint32_t size, const void* data) {
    // 使用 glBufferSubData 更新部分数据
    glBindBuffer(buffer_Target, buffer_);
    glBufferSubData(buffer_Target, offset, size, data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLBuffer] glBufferSubData failed (error: 0x%X, offset: %u, size: %u)\n", err, offset,
                     size);
    }
}

void GLBuffer::updateDynamic(uint32_t offset, uint32_t size, const void* data) {
    glBindBuffer(buffer_Target, buffer_);

    // 方案 1: 使用 glBufferSubData（通常足够）
    // 方案 2: 使用 DISCARD + 重新映射（GL 4.5+ glNamedBufferData）
    // 方案 3: 使用 persistent mapping（GL 4.4+）

    // 这里使用简单的 glBufferSubData，对于动态缓冲区大小的内容可以接受
    glBufferSubData(buffer_Target, offset, size, data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLBuffer] Dynamic buffer update failed (error: 0x%X)\n", err);
    }
}

bool GLBuffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (!isValid() || !outData)
        return false;

    if (desc_.usage != BufferUsage::Staging) {
        std::fprintf(stderr, "[GLBuffer] readback only supported for Staging buffers\n");
        return false;
    }

    if (offset + size > desc_.size) {
        std::fprintf(stderr, "[GLBuffer] Readback out of bounds (offset: %u, size: %u, buffer size: %u)\n", offset,
                     size, desc_.size);
        return false;
    }

    glBindBuffer(GL_COPY_READ_BUFFER, buffer_);

    // 使用 glGetBufferSubData 读取数据
    glGetBufferSubData(GL_COPY_READ_BUFFER, offset, size, outData);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLBuffer] readback failed (error: 0x%X)\n", err);
        return false;
    }

    return true;
}

}  // namespace mulan::engine
