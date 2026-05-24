/**
 * @file GLCommandList.cpp
 * @brief OpenGL 命令列表实现
 * @author terry
 * @date 2026-04-16
 */

#include "GLCommandList.h"
#include "GLBuffer.h"
#include "GLPipelineState.h"
#include "GLTexture.h"
#include "GLRenderTarget.h"
#include "../Buffer.h"
#include "../RenderTypes.h"
#include "../VertexFormat.h"
#include <cstdio>
#include <cstring>

namespace MulanGeo::engine {

GLCommandList::GLCommandList() {
    glGenVertexArrays(1, &m_vao);
}

GLCommandList::~GLCommandList() {
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void GLCommandList::begin() {
    // OpenGL 立即模式，begin/end 不做实质工作
    m_pipelineStateApplied = false;
    m_vertexLayoutDirty    = true;
    m_viewportDirty = true;
    m_scissorDirty  = true;
}

void GLCommandList::end() {
    // OpenGL 立即模式，end 不做实质工作
}

void GLCommandList::setPipelineState(PipelineState* pso) {
    if (m_currentPipeline == pso) return;

    m_currentPipeline      = pso;
    m_pipelineStateApplied = false;
    m_vertexLayoutDirty    = true;  // 新 PSO 可能有不同的 VertexLayout
}

void GLCommandList::setViewport(const Viewport& vp) {
    if (std::memcmp(&m_viewport, &vp, sizeof(Viewport)) == 0) {
        return;  // 无变化
    }

    m_viewport = vp;
    m_viewportDirty = true;
}

void GLCommandList::setScissorRect(const ScissorRect& rect) {
    if (std::memcmp(&m_scissorRect, &rect, sizeof(ScissorRect)) == 0) {
        return;  // 无变化
    }

    m_scissorRect = rect;
    m_scissorDirty = true;
}

void GLCommandList::bindResources(const BindGroup& group) {
    for (uint8_t i = 0; i < group.count; ++i) {
        const auto& e = group.entries[i];
        if (e.buffer) {
            GLuint glBuf = static_cast<GLBuffer*>(e.buffer)->handle();
            glBindBufferRange(GL_UNIFORM_BUFFER, e.binding,
                              glBuf,
                              static_cast<GLintptr>(e.offset),
                              static_cast<GLsizeiptr>(e.size));
        }
        // texture → glActiveTexture + glBindTexture
    }
}

void GLCommandList::setVertexBuffer(uint32_t slot, Buffer* buffer,
                                    uint32_t offset) {
    if (slot >= MAX_VERTEX_BUFFERS) {
        std::fprintf(stderr, "[GLCommandList] setVertexBuffer: slot %u out of range\n", slot);
        return;
    }

    if (m_vertexBuffers[slot] == buffer && 
        m_vertexBufferOffsets[slot] == offset) {
        return;  // 无变化
    }

    m_vertexBuffers[slot]       = buffer;
    m_vertexBufferOffsets[slot] = offset;

    if (slot >= m_vertexBufferCount) {
        m_vertexBufferCount = slot + 1;
    }

    m_vertexLayoutDirty = true;  // VBO 更换，需重新绑定属性指针
}

void GLCommandList::setVertexBuffers(uint32_t startSlot, uint32_t count,
                                     Buffer** buffers, uint32_t* offsets) {
    for (uint32_t i = 0; i < count; ++i) {
        setVertexBuffer(startSlot + i, buffers[i], 
                       offsets ? offsets[i] : 0);
    }
}

void GLCommandList::setIndexBuffer(Buffer* buffer, uint32_t offset,
                                   IndexType type) {
    if (m_indexBuffer == buffer && 
        m_indexBufferOffset == offset &&
        m_indexType == type) {
        return;  // 无变化
    }

    m_indexBuffer       = buffer;
    m_indexBufferOffset = offset;
    m_indexType         = type;

    // IBO 绑定必须在 VAO 绑定中才能被 VAO 记录
    glBindVertexArray(m_vao);
    if (buffer) {
        auto* glBuf = static_cast<GLBuffer*>(buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuf->handle());
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    glBindVertexArray(0);
}

void GLCommandList::draw(const DrawAttribs& attribs) {
    applyPipelineState();

    // 绑定 VAO 并更新顶点属性指针
    glBindVertexArray(m_vao);
    setupVertexAttributes();

    if (m_viewportDirty) {
        glViewport(static_cast<GLint>(m_viewport.x),
                   static_cast<GLint>(m_viewport.y),
                   static_cast<GLint>(m_viewport.width),
                   static_cast<GLint>(m_viewport.height));
#ifdef __EMSCRIPTEN__
        glDepthRangef(m_viewport.minDepth, m_viewport.maxDepth);
#else
        glDepthRange(m_viewport.minDepth, m_viewport.maxDepth);
#endif
        m_viewportDirty = false;
    }

    if (m_scissorDirty) {
        glScissor(static_cast<GLint>(m_scissorRect.x),
                  static_cast<GLint>(m_scissorRect.y),
                  static_cast<GLint>(m_scissorRect.width),
                  static_cast<GLint>(m_scissorRect.height));
        m_scissorDirty = false;
    }

    GLenum topology = GL_TRIANGLES;
    if (m_currentPipeline) {
        auto* glPso = static_cast<GLPipelineState*>(m_currentPipeline);
        topology = glPso->glTopology();
    }
    glDrawArrays(topology, attribs.startVertex, attribs.vertexCount);

    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLCommandList] draw failed (error: 0x%X)\n", err);
    }
}

void GLCommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    applyPipelineState();

    // 绑定 VAO 并更新顶点属性指针
    glBindVertexArray(m_vao);
    setupVertexAttributes();

    if (m_viewportDirty) {
        glViewport(static_cast<GLint>(m_viewport.x),
                   static_cast<GLint>(m_viewport.y),
                   static_cast<GLint>(m_viewport.width),
                   static_cast<GLint>(m_viewport.height));
#ifdef __EMSCRIPTEN__
        glDepthRangef(m_viewport.minDepth, m_viewport.maxDepth);
#else
        glDepthRange(m_viewport.minDepth, m_viewport.maxDepth);
#endif
        m_viewportDirty = false;
    }

    if (m_scissorDirty) {
        glScissor(static_cast<GLint>(m_scissorRect.x),
                  static_cast<GLint>(m_scissorRect.y),
                  static_cast<GLint>(m_scissorRect.width),
                  static_cast<GLint>(m_scissorRect.height));
        m_scissorDirty = false;
    }

    GLenum topology = GL_TRIANGLES;
    if (m_currentPipeline) {
        auto* glPso = static_cast<GLPipelineState*>(m_currentPipeline);
        topology = glPso->glTopology();
    }
    GLenum indexFormat = indexTypeToGLFormat(m_indexType);
    glDrawElements(topology,
                   static_cast<GLsizei>(attribs.indexCount),
                   indexFormat,
                   reinterpret_cast<const void*>(
                       static_cast<uintptr_t>(m_indexBufferOffset + attribs.startIndex)));

    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[GLCommandList] drawIndexed failed (error: 0x%X)\n", err);
    }
}

void GLCommandList::updateBuffer(Buffer* buffer, uint32_t offset,
                                 uint32_t size, const void* data,
                                 ResourceTransitionMode /*mode*/) {
    if (!buffer || !data) return;

    buffer->update(offset, size, data);
}

void GLCommandList::transitionResource(Buffer* buffer,
                                       ResourceState newState) {
    // GL 无需显式资源状态转换
    (void)buffer;
    (void)newState;
}

void GLCommandList::transitionResource(Texture* texture,
                                       ResourceState newState) {
    // GL 无需显式资源状态转换
    (void)texture;
    (void)newState;
}

void GLCommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    // TODO: 实现纹理→缓冲区复制
    // 使用 glReadPixels 或 glGetTexImage
    (void)src;
    (void)dst;
}

void GLCommandList::clearColor(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLCommandList::clearDepth(float depth) {
    glClearDepthf(depth);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void GLCommandList::clearStencil(uint8_t stencil) {
    glClearStencil(stencil);
    glClear(GL_STENCIL_BUFFER_BIT);
}

Buffer* GLCommandList::currentVertexBuffer(uint32_t slot) const {
    if (slot >= MAX_VERTEX_BUFFERS) return nullptr;
    return m_vertexBuffers[slot];
}

GLCommandList::GLAttribType GLCommandList::vertexFormatToGL(VertexFormat fmt) {
    const auto& info = getVertexFormatInfo(fmt);
    GLenum type = GL_FLOAT;
    if (info.isFloat) {
        switch (info.bytesPerComponent) {
        case 2:  type = GL_HALF_FLOAT; break;
        case 4:  type = GL_FLOAT;      break;
        default: type = GL_FLOAT;      break;
        }
    } else if (info.isInteger) {
        switch (info.bytesPerComponent) {
        case 1:  type = info.isNormalized ? GL_UNSIGNED_BYTE  : GL_UNSIGNED_BYTE;  break;
        case 2:  type = GL_UNSIGNED_SHORT; break;
        case 4:  type = GL_UNSIGNED_INT;   break;
        default: type = GL_UNSIGNED_INT;   break;
        }
    } else if (info.isNormalized) {
        // SNorm / UNorm
        switch (info.bytesPerComponent) {
        case 1:  type = info.format == VertexFormat::Byte4N ? GL_BYTE : GL_UNSIGNED_BYTE; break;
        case 2:  type = GL_UNSIGNED_SHORT; break;
        default: type = GL_FLOAT;          break;
        }
    }
    return { type,
             static_cast<GLint>(info.componentCount),
             static_cast<GLboolean>(info.isNormalized ? GL_TRUE : GL_FALSE),
             info.isInteger && !info.isNormalized };
}

void GLCommandList::setupVertexAttributes() {
    if (!m_vertexLayoutDirty) return;
    if (!m_currentPipeline)   return;

    const auto& layout = m_currentPipeline->desc().vertexLayout;
    if (layout.empty())       return;

    GLsizei stride = static_cast<GLsizei>(layout.stride());

    // 先禁用所有属性，再按布局重新开启
    for (GLuint i = 0; i < 16; ++i)
        glDisableVertexAttribArray(i);

    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        GLuint location  = static_cast<GLuint>(i);  // location = attribute index

        // 绑定该 slot 对应的 VBO
        uint8_t slot = attr.bufferSlot;
        if (slot < m_vertexBufferCount && m_vertexBuffers[slot]) {
            auto* glBuf = static_cast<GLBuffer*>(m_vertexBuffers[slot]);
            glBindBuffer(GL_ARRAY_BUFFER, glBuf->handle());
        } else {
            continue;  // 该 slot 无 VBO，跳过
        }

        auto glType = vertexFormatToGL(attr.format);
        const void* offsetPtr = reinterpret_cast<const void*>(
            static_cast<uintptr_t>(attr.offset + m_vertexBufferOffsets[slot]));

        glEnableVertexAttribArray(location);
        if (glType.isInteger) {
            glVertexAttribIPointer(location, glType.components, glType.type,
                                   stride, offsetPtr);
        } else {
            glVertexAttribPointer(location, glType.components, glType.type,
                                  glType.normalized, stride, offsetPtr);
        }
    }

    m_vertexLayoutDirty = false;
}

void GLCommandList::applyPipelineState() {
    if (!m_currentPipeline || m_pipelineStateApplied) return;

    auto* glPipeline = static_cast<GLPipelineState*>(m_currentPipeline);

    // 应用着色器程序
    glUseProgram(glPipeline->program());

    // 应用渲染状态（栅格化、深度测试、混合等）
    glPipeline->applyRenderState();

    m_pipelineStateApplied = true;
}

GLenum GLCommandList::indexTypeToGLFormat(IndexType type) {
    switch (type) {
    case IndexType::UInt16: return GL_UNSIGNED_SHORT;
    case IndexType::UInt32: return GL_UNSIGNED_INT;
    default:                return GL_UNSIGNED_INT;
    }
}

void GLCommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    // GL FBO selection:
    // - Swapchain (presentSource=true): bind default framebuffer (0)
    // - RenderTarget: use nativeHandle (set by GLRenderTarget convenience method)
    GLuint fbo = static_cast<GLuint>(info.nativeHandle);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glViewport(0, 0,
               static_cast<GLsizei>(info.width),
               static_cast<GLsizei>(info.height));

    // Perform clears based on LoadAction. Depth/color clears obey write masks,
    // so restore masks before clearing to avoid stale depth from a previous pipeline.
    GLbitfield clearBits = 0;
    for (uint32_t i = 0; i < info.colorCount; ++i) {
        if (info.colorAttachments[i].loadAction == LoadAction::Clear) {
            glClearColor(info.clearColor[0], info.clearColor[1],
                         info.clearColor[2], info.clearColor[3]);
            clearBits |= GL_COLOR_BUFFER_BIT;
        }
    }
    if (info.depthAttachment.loadAction == LoadAction::Clear &&
        (info.depthAttachment.target || info.presentSource)) {
        glClearDepthf(info.clearDepth);
        clearBits |= GL_DEPTH_BUFFER_BIT;
    }
    if (clearBits != 0) {
        if (clearBits & GL_COLOR_BUFFER_BIT) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        if (clearBits & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
        }
        glClear(clearBits);
    }

    // Mark viewport dirty since we overrode it
    m_viewportDirty = true;
}

void GLCommandList::endRenderPass() {
    // GL has no explicit render pass end. Reset to default FBO.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace MulanGeo::Engine
