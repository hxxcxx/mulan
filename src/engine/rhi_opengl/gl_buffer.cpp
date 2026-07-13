#include "detail/gl_buffer.h"
#include <cstddef>
#include <cstring>
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

GLBuffer::GLBuffer(uint32_t transientUniformPageSize)
    : desc_(BufferDesc::uniform(transientUniformPageSize, "GLTransientUniformPage")),
      buffer_Target(GL_UNIFORM_BUFFER),
      buffer_Usage(GL_STREAM_DRAW),
      transient_uniform_page_(true) {
    if (transientUniformPageSize == 0 || !glNamedBufferStorage || !glMapNamedBufferRange)
        return;

    glCreateBuffers(1, &buffer_);
    if (!buffer_)
        return;
    constexpr GLbitfield storageFlags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
    glNamedBufferStorage(buffer_, static_cast<GLsizeiptr>(transientUniformPageSize), nullptr, storageFlags);
    mapped_data_ = glMapNamedBufferRange(buffer_, 0, static_cast<GLsizeiptr>(transientUniformPageSize),
                                         GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    if (!mapped_data_) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
}

std::unique_ptr<GLBuffer> GLBuffer::createTransientUniformPage(uint32_t size) {
    auto page = std::unique_ptr<GLBuffer>(new GLBuffer(size));
    if (!page->isValid() || !page->mappedData())
        return nullptr;
    return page;
}

GLBuffer::~GLBuffer() {
    if (buffer_ != 0) {
        if (mapped_data_)
            glUnmapNamedBuffer(buffer_);
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
    glCreateBuffers(1, &buffer_);
    if (buffer_ == 0) {
        LOG_ERROR("[OpenGL] Buffer creation failed: glCreateBuffers returned 0");
        return;
    }

    // 分配内存并上传初始数据
    glNamedBufferData(buffer_, static_cast<GLsizeiptr>(desc_.size), desc_.initData, buffer_Usage);

    // 检查错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Buffer allocation failed: error=0x{:X}, name={}", err, desc_.name);
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        return;
    }

    LOG_DEBUG("[OpenGL] Buffer created: handle={}, size={}, name={}", buffer_, desc_.size, desc_.name);
}

void GLBuffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (!isValid() || !data)
        return;

    if (offset + size > desc_.size) {
        LOG_ERROR("[OpenGL] Buffer update rejected: offset={}, size={}, bufferSize={}", offset, size, desc_.size);
        return;
    }

    if (mapped_data_) {
        std::memcpy(static_cast<std::byte*>(mapped_data_) + offset, data, size);
        return;
    }

    switch (desc_.usage) {
    case BufferUsage::Immutable:
        // 不可修改
        LOG_ERROR("[OpenGL] Buffer update rejected: immutable buffer");
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
    // 使用 DSA 更新部分数据
    glNamedBufferSubData(buffer_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Buffer update failed: error=0x{:X}, offset={}, size={}", err, offset, size);
    }
}

void GLBuffer::updateDynamic(uint32_t offset, uint32_t size, const void* data) {
    // 这里使用简单的 DSA 更新，对于动态缓冲区大小的内容可以接受
    glNamedBufferSubData(buffer_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Dynamic buffer update failed: error=0x{:X}", err);
    }
}

bool GLBuffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (!isValid() || !outData)
        return false;

    if (desc_.usage != BufferUsage::Staging) {
        LOG_ERROR("[OpenGL] Buffer readback rejected: staging buffer required");
        return false;
    }

    if (offset + size > desc_.size) {
        LOG_ERROR("[OpenGL] Buffer readback rejected: offset={}, size={}, bufferSize={}", offset, size, desc_.size);
        return false;
    }

    // 使用 DSA 读取数据
    glGetNamedBufferSubData(buffer_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), outData);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] Buffer readback failed: error=0x{:X}", err);
        return false;
    }

    return true;
}

}  // namespace mulan::engine
