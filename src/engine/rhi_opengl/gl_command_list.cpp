#include "detail/gl_command_list.h"
#include "detail/gl_bind_group.h"
#include "detail/gl_buffer.h"
#include "detail/gl_pipeline_state.h"
#include "detail/gl_render_target.h"
#include "detail/gl_sampler.h"
#include "detail/gl_texture.h"
#include "detail/gl_transient_uniform_arena.h"
#include "../rhi/buffer.h"
#include "../rhi/engine_error_code.h"
#include "../rhi/render_types.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace mulan::engine {

GLCommandList::GLCommandList(uint32_t uniformAlignment, uint32_t maxUniformSize)
    : transient_uniform_arena_(std::make_unique<GLTransientUniformArena>(uniformAlignment, maxUniformSize)) {
    glCreateVertexArrays(1, &vao_);
}

GLCommandList::~GLCommandList() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

core::Result<void> GLCommandList::doBegin() {
    if (!transient_uniform_arena_->beginRecording()) {
        return std::unexpected(
                makeError(EngineErrorCode::CommandRecordingFailed, "OpenGL transient uniform batch is unavailable"));
    }
    // OpenGL 立即模式，begin/end 不做实质工作
    pipeline_state_applied_ = false;
    vertex_layout_dirty_ = true;
    viewport_Dirty = true;
    scissor_dirty_ = true;
    glDisable(GL_SCISSOR_TEST);
    return {};
}

core::Result<void> GLCommandList::doEnd() {
    transient_uniform_arena_->endRecording();
    return {};
}

void GLCommandList::doSetPipelineState(PipelineState* pso) {
    assertResourceCompatible(pso);
    if (!pso) {
        rejectRecording("OpenGL graphics pipeline is null");
        return;
    }
    activateBindGroupLayout(pso->bindGroupLayout());
    if (current_pipeline_ == pso)
        return;

    current_pipeline_ = pso;
    pipeline_state_applied_ = false;
    vertex_layout_dirty_ = true;  // 新 PSO 可能有不同的 VertexLayout
}

void GLCommandList::doSetViewport(const Viewport& vp) {
    if (std::memcmp(&viewport_, &vp, sizeof(Viewport)) == 0) {
        return;  // 无变化
    }

    viewport_ = vp;
    viewport_Dirty = true;
}

void GLCommandList::doSetScissorRect(const ScissorRect& rect) {
    if (std::memcmp(&scissor_rect_, &rect, sizeof(ScissorRect)) == 0) {
        return;  // 无变化
    }

    scissor_rect_ = rect;
    scissor_dirty_ = true;
}

void GLCommandList::doBindGroup(BindGroup& group) {
    if (std::any_of(group.layout().entries().begin(), group.layout().entries().end(),
                    [](const BindGroupLayoutEntry& entry) { return entry.mode == BindingMode::Dynamic; })) {
        LOG_ERROR("[OpenGL] bindGroup rejected: dynamic UniformBuffer bindings are required by the layout");
        rejectRecording("OpenGL bindGroup requires dynamic UniformBuffer bindings");
        return;
    }
    bindGroupEntries(group);
    group.markClean();
}

void GLCommandList::doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) {
    const std::string validationError = validateDynamicUniformBindings(
            group.layout(), dynamicUniforms,
            { transient_uniform_arena_->alignment(), transient_uniform_arena_->maxAllocationSize() },
            transient_uniform_arena_->recordingGeneration());
    if (!validationError.empty()) {
        LOG_ERROR("[OpenGL] Dynamic uniform binding rejected: {}", validationError);
        rejectRecording(validationError);
        return;
    }

    bindGroupEntries(group);
    glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    for (const auto& binding : dynamicUniforms) {
        assertResourceCompatible(binding.slice.backingBuffer);
        if (binding.binding >= 32) {
            LOG_ERROR("[OpenGL] Dynamic uniform binding {} exceeds the backend slot limit", binding.binding);
            rejectRecording("OpenGL dynamic uniform binding exceeds the backend slot limit");
            return;
        }
        auto* buffer = static_cast<GLBuffer*>(binding.slice.backingBuffer);
        if (!buffer || !buffer->isTransientUniformPage()) {
            LOG_ERROR("[OpenGL] Dynamic uniform binding {} does not reference a persistent mapped page",
                      binding.binding);
            rejectRecording("OpenGL dynamic uniform binding does not reference a persistent mapped page");
            return;
        }
        glBindBufferRange(GL_UNIFORM_BUFFER, binding.binding, buffer->handle(),
                          static_cast<GLintptr>(binding.slice.offset), static_cast<GLsizeiptr>(binding.slice.size));
    }
    group.markClean();
}

core::Result<UniformSlice> GLCommandList::doWriteUniformBytes(std::span<const std::byte> data) {
    const auto allocation = transient_uniform_arena_->upload(data);
    if (!allocation)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "OpenGL transient uniform allocation failed"));
    return UniformSlice{ allocation.backingBuffer, allocation.offset, allocation.size, allocation.recordingGeneration };
}

void GLCommandList::bindGroupEntries(const BindGroup& group) {
    bindEntries(group.entries(), group.entryCount());
}

void GLCommandList::bindEntries(const BindGroupEntry* entries, uint8_t count) {
    Sampler* sharedSampler = nullptr;
    uint8_t samplerCount = 0;

    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (entry.type == DescriptorType::Sampler) {
            sharedSampler = entry.sampler;
            ++samplerCount;
            continue;
        }
        bindEntry(entry);
    }

    if (samplerCount == 1) {
        auto* glSampler = static_cast<GLSampler*>(sharedSampler);
        if (!glSampler) {
            rejectRecording("OpenGL shared sampler binding is invalid");
            return;
        }

        // The OpenGL Slang target exposes sampled textures as combined
        // sampler uniforms at the texture binding. Current render bind groups
        // intentionally have one sampler shared by their texture entries.
        for (uint8_t i = 0; i < count; ++i) {
            const auto& entry = entries[i];
            if (entry.type == DescriptorType::TextureSRV && entry.texture && entry.binding < 32)
                glBindSampler(entry.binding, glSampler->handle());
        }
        return;
    }

    // Preserve the existing binding behavior for groups that expose multiple
    // samplers. Such groups need explicit texture/sampler pairing metadata
    // before they can use combined OpenGL sampler uniforms.
    for (uint8_t i = 0; i < count; ++i) {
        if (entries[i].type == DescriptorType::Sampler)
            bindEntry(entries[i]);
    }
}

void GLCommandList::bindEntry(const BindGroupEntry& e) {
    if (e.binding >= 32) {
        rejectRecording("OpenGL resource binding exceeds the backend slot limit");
        return;
    }

    if (e.type == DescriptorType::UniformBuffer && e.buffer) {
        auto* glBuffer = static_cast<GLBuffer*>(e.buffer);
        if (!glBuffer || !glBuffer->isValid()) {
            rejectRecording("OpenGL uniform-buffer binding is invalid");
            return;
        }
        const GLsizeiptr size =
                e.size ? static_cast<GLsizeiptr>(e.size) : static_cast<GLsizeiptr>(glBuffer->desc().size - e.offset);
        glBindBufferRange(GL_UNIFORM_BUFFER, e.binding, glBuffer->handle(), static_cast<GLintptr>(e.offset), size);
    }

    if (e.type == DescriptorType::TextureSRV && e.texture) {
        auto* glTexture = static_cast<GLTexture*>(e.texture);
        if (glTexture && glTexture->isValid())
            glBindTextureUnit(e.binding, glTexture->handle());
        else
            rejectRecording("OpenGL texture binding is invalid");
    }

    if (e.type == DescriptorType::Sampler && e.sampler) {
        auto* glSampler = static_cast<GLSampler*>(e.sampler);
        if (glSampler && glSampler->isValid())
            glBindSampler(e.binding, glSampler->handle());
        else
            rejectRecording("OpenGL sampler binding is invalid");
    }
}

void GLCommandList::doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    assertResourceCompatible(buffer);
    if (slot >= MAX_VERTEX_BUFFERS) {
        LOG_ERROR("[OpenGL] setVertexBuffer rejected: slot {} is out of range", slot);
        rejectRecording("OpenGL vertex-buffer slot exceeds the backend limit");
        return;
    }
    if (!buffer || offset >= buffer->size()) {
        rejectRecording("OpenGL vertex-buffer binding is invalid");
        return;
    }

    if (vertex_buffers_[slot] == buffer && vertex_buffer_offsets_[slot] == offset) {
        return;  // 无变化
    }

    vertex_buffers_[slot] = buffer;
    vertex_buffer_offsets_[slot] = offset;

    if (slot >= vertex_buffer_count_) {
        vertex_buffer_count_ = slot + 1;
    }

    vertex_layout_dirty_ = true;  // VBO 更换，需重新绑定属性指针
}

void GLCommandList::doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
    if (!buffers || startSlot >= MAX_VERTEX_BUFFERS || count > MAX_VERTEX_BUFFERS - startSlot) {
        rejectRecording("OpenGL vertex-buffer array exceeds the backend limits");
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
        assertResourceCompatible(buffers[i]);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = offsets ? offsets[i] : 0;
        if (!buffers[i] || offset >= buffers[i]->size()) {
            rejectRecording("OpenGL vertex-buffer array contains an invalid buffer or offset");
            return;
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        doSetVertexBuffer(startSlot + i, buffers[i], offsets ? offsets[i] : 0);
    }
}

void GLCommandList::doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    assertResourceCompatible(buffer);
    if (!buffer || offset >= buffer->size()) {
        rejectRecording("OpenGL index-buffer binding is invalid");
        return;
    }
    if (index_buffer_ == buffer && index_buffer_Offset == offset && index_type_ == type) {
        return;  // 无变化
    }

    index_buffer_ = buffer;
    index_buffer_Offset = offset;
    index_type_ = type;

    if (buffer) {
        auto* glBuf = static_cast<GLBuffer*>(buffer);
        glVertexArrayElementBuffer(vao_, glBuf->handle());
    } else {
        glVertexArrayElementBuffer(vao_, 0);
    }
}

void GLCommandList::doDraw(const DrawAttribs& attribs) {
    applyPipelineState();

    // 绑定 VAO 并更新顶点属性指针
    glBindVertexArray(vao_);
    setupVertexAttributes();

    if (viewport_Dirty) {
        const GLint y = framebuffer_height_ > 0
                                ? framebuffer_height_ - static_cast<GLint>(viewport_.y + viewport_.height)
                                : static_cast<GLint>(viewport_.y);
        glViewport(static_cast<GLint>(viewport_.x), y, static_cast<GLint>(viewport_.width),
                   static_cast<GLint>(viewport_.height));
        glDepthRange(viewport_.minDepth, viewport_.maxDepth);
        viewport_Dirty = false;
    }

    if (scissor_dirty_) {
        const GLint y = framebuffer_height_ > 0 ? framebuffer_height_ - scissor_rect_.y - scissor_rect_.height
                                                : scissor_rect_.y;
        if (scissor_rect_.width > 0 && scissor_rect_.height > 0)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(scissor_rect_.x), y, static_cast<GLint>(scissor_rect_.width),
                  static_cast<GLint>(scissor_rect_.height));
        scissor_dirty_ = false;
    }

    GLenum topology = GL_TRIANGLES;
    if (current_pipeline_) {
        auto* glPso = static_cast<GLPipelineState*>(current_pipeline_);
        topology = glPso->glTopology();
    }
    if (attribs.instanceCount > 1) {
        if (attribs.startInstance > 0)
            glDrawArraysInstancedBaseInstance(topology, attribs.startVertex, attribs.vertexCount, attribs.instanceCount,
                                              attribs.startInstance);
        else
            glDrawArraysInstanced(topology, attribs.startVertex, attribs.vertexCount, attribs.instanceCount);
    } else {
        glDrawArrays(topology, attribs.startVertex, attribs.vertexCount);
    }

    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] draw failed: error=0x{:X}", err);
        rejectRecording("OpenGL draw failed");
    }
}

void GLCommandList::doDrawIndexed(const DrawIndexedAttribs& attribs) {
#if defined(_WIN32)
    if (!wglGetCurrentContext()) {
        LOG_ERROR("[OpenGL] drawIndexed rejected: no current WGL context");
        rejectRecording("OpenGL indexed draw requires a current WGL context");
        return;
    }
#endif
    if (!glDrawElements) {
        LOG_ERROR("[OpenGL] drawIndexed rejected: glDrawElements is unavailable");
        rejectRecording("OpenGL indexed draw entry point is unavailable");
        return;
    }

    applyPipelineState();

    // 绑定 VAO 并更新顶点属性指针
    glBindVertexArray(vao_);
    setupVertexAttributes();

    auto* glIndexBuffer = static_cast<GLBuffer*>(index_buffer_);
    if (!glIndexBuffer || !glIndexBuffer->isValid()) {
        LOG_ERROR("[OpenGL] drawIndexed rejected: no valid index buffer is bound");
        rejectRecording("OpenGL indexed draw requires a valid index buffer");
        glBindVertexArray(0);
        return;
    }

    if (viewport_Dirty) {
        const GLint y = framebuffer_height_ > 0
                                ? framebuffer_height_ - static_cast<GLint>(viewport_.y + viewport_.height)
                                : static_cast<GLint>(viewport_.y);
        glViewport(static_cast<GLint>(viewport_.x), y, static_cast<GLint>(viewport_.width),
                   static_cast<GLint>(viewport_.height));
        glDepthRange(viewport_.minDepth, viewport_.maxDepth);
        viewport_Dirty = false;
    }

    if (scissor_dirty_) {
        const GLint y = framebuffer_height_ > 0 ? framebuffer_height_ - scissor_rect_.y - scissor_rect_.height
                                                : scissor_rect_.y;
        if (scissor_rect_.width > 0 && scissor_rect_.height > 0)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(scissor_rect_.x), y, static_cast<GLint>(scissor_rect_.width),
                  static_cast<GLint>(scissor_rect_.height));
        scissor_dirty_ = false;
    }

    GLenum topology = GL_TRIANGLES;
    if (current_pipeline_) {
        auto* glPso = static_cast<GLPipelineState*>(current_pipeline_);
        topology = glPso->glTopology();
    }
    const GLenum indexFormat = indexTypeToGLFormat(attribs.indexType);
    const uint32_t indexStride = attribs.indexType == IndexType::UInt16 ? 2u : 4u;
    const uint64_t indexOffset =
            static_cast<uint64_t>(index_buffer_Offset) + static_cast<uint64_t>(attribs.startIndex) * indexStride;
    const uint64_t indexEnd = indexOffset + static_cast<uint64_t>(attribs.indexCount) * indexStride;
    if (indexEnd > glIndexBuffer->size()) {
        LOG_ERROR("[OpenGL] drawIndexed rejected: offset={}, count={}, stride={}, bufferSize={}", indexOffset,
                  attribs.indexCount, indexStride, glIndexBuffer->size());
        rejectRecording("OpenGL indexed draw range exceeds the index buffer");
        glBindVertexArray(0);
        return;
    }

#ifdef _DEBUG
    GLint boundIndexBuffer = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundIndexBuffer);
    if (boundIndexBuffer != static_cast<GLint>(glIndexBuffer->handle())) {
        LOG_ERROR("[OpenGL] drawIndexed rejected: VAO EBO mismatch, expected={}, actual={}", glIndexBuffer->handle(),
                  boundIndexBuffer);
        rejectRecording("OpenGL indexed draw detected a VAO index-buffer mismatch");
        glBindVertexArray(0);
        return;
    }
#endif

    const void* indexPointer = reinterpret_cast<const void*>(static_cast<uintptr_t>(indexOffset));
    if (attribs.instanceCount > 1) {
        glDrawElementsInstancedBaseVertexBaseInstance(topology, static_cast<GLsizei>(attribs.indexCount), indexFormat,
                                                      indexPointer, attribs.instanceCount, attribs.baseVertex,
                                                      attribs.startInstance);
    } else if (attribs.baseVertex != 0) {
        glDrawElementsBaseVertex(topology, static_cast<GLsizei>(attribs.indexCount), indexFormat, indexPointer,
                                 attribs.baseVertex);
    } else {
        glDrawElements(topology, static_cast<GLsizei>(attribs.indexCount), indexFormat, indexPointer);
    }

    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_ERROR("[OpenGL] drawIndexed failed: error=0x{:X}", err);
        rejectRecording("OpenGL indexed draw failed");
    }
}

void GLCommandList::doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) {
    assertResourceCompatible(argsBuffer);
    (void) offset;
    (void) drawCount;
    (void) stride;
    LOG_ERROR("[OpenGL] drawIndirect rejected: indirect drawing is not implemented");
    rejectRecording("OpenGL indirect drawing is not implemented");
}

void GLCommandList::doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    (void) threadGroupX;
    (void) threadGroupY;
    (void) threadGroupZ;
    LOG_ERROR("[OpenGL] dispatch rejected: compute pipeline is not implemented");
    rejectRecording("OpenGL compute dispatch is not implemented");
}

void GLCommandList::doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    assertResourceCompatible(argsBuffer);
    (void) offset;
    LOG_ERROR("[OpenGL] dispatchIndirect rejected: compute pipeline is not implemented");
    rejectRecording("OpenGL indirect compute dispatch is not implemented");
}

void GLCommandList::doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) {
    (void) offset;
    (void) size;
    (void) data;
    (void) stageFlags;
    LOG_ERROR("[OpenGL] setPushConstants rejected: push constants are not implemented");
    rejectRecording("OpenGL push constants are not implemented");
}

void GLCommandList::doTransitionResource(Texture* texture, ResourceState newState) {
    assertResourceCompatible(texture);
    if (!texture) {
        rejectRecording("OpenGL texture transition requires a valid texture");
        return;
    }
    GLbitfield barriers = 0;
    switch (newState) {
    case ResourceState::ShaderResource: barriers = GL_TEXTURE_FETCH_BARRIER_BIT; break;
    case ResourceState::UnorderedAccess: barriers = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT; break;
    case ResourceState::RenderTarget: barriers = GL_FRAMEBUFFER_BARRIER_BIT; break;
    case ResourceState::CopySrc:
    case ResourceState::CopyDest: barriers = GL_PIXEL_BUFFER_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT; break;
    default: break;
    }
    if (barriers)
        glMemoryBarrier(barriers);
}

core::Result<void> GLCommandList::doCopyTextureToBuffer(Texture* src, Buffer* dst) {
    assertResourceCompatible(src);
    assertResourceCompatible(dst);
    const auto rejectCopy = [this](std::string_view reason) -> core::Result<void> {
        rejectRecording(reason);
        return std::unexpected(makeError(EngineErrorCode::ResourceReadbackFailed, reason));
    };
    auto* texture = static_cast<GLTexture*>(src);
    auto* buffer = static_cast<GLBuffer*>(dst);
    if (!texture || !texture->isValid() || !buffer || !buffer->isValid() || buffer->usage() != BufferUsage::Staging ||
        texture->desc().dimension != TextureDimension::Texture2D)
        return rejectCopy("OpenGL texture copy requires a Texture2D and staging buffer");

    GLint previous_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_fbo);

    GLuint read_fbo = 0;
    glCreateFramebuffers(1, &read_fbo);
    glNamedFramebufferTexture(read_fbo, GL_COLOR_ATTACHMENT0, texture->handle(), 0);
    glNamedFramebufferReadBuffer(read_fbo, GL_COLOR_ATTACHMENT0);

    GLuint resolve_fbo = 0;
    GLuint resolve_texture = 0;
    const bool multisampled = texture->desc().sampleCount > 1;
    if (multisampled) {
        glCreateFramebuffers(1, &resolve_fbo);
        glCreateTextures(GL_TEXTURE_2D, 1, &resolve_texture);
        glTextureStorage2D(resolve_texture, 1, GLTexture::toGLInternalFormat(texture->desc().format),
                           static_cast<GLsizei>(texture->desc().width), static_cast<GLsizei>(texture->desc().height));
        glTextureParameteri(resolve_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(resolve_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glNamedFramebufferTexture(resolve_fbo, GL_COLOR_ATTACHMENT0, resolve_texture, 0);
        glNamedFramebufferDrawBuffer(resolve_fbo, GL_COLOR_ATTACHMENT0);
        glBlitNamedFramebuffer(read_fbo, resolve_fbo, 0, 0, static_cast<GLint>(texture->desc().width),
                               static_cast<GLint>(texture->desc().height), 0, 0,
                               static_cast<GLint>(texture->desc().width), static_cast<GLint>(texture->desc().height),
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
    } else {
        glNamedFramebufferReadBuffer(read_fbo, GL_COLOR_ATTACHMENT0);
    }
    const GLuint readback_fbo = multisampled ? resolve_fbo : read_fbo;

    GLenum read_format = GL_RGBA;
    GLenum read_type = GL_UNSIGNED_BYTE;
    switch (texture->desc().format) {
    case TextureFormat::R8_UNorm: read_format = GL_RED; break;
    case TextureFormat::BGRA8_UNorm:
    case TextureFormat::BGRA8_sRGB: read_format = GL_BGRA; break;
    case TextureFormat::RG16_Float:
        read_format = GL_RG;
        read_type = GL_HALF_FLOAT;
        break;
    case TextureFormat::RGBA16_Float: read_type = GL_HALF_FLOAT; break;
    case TextureFormat::RGBA32_Float: read_type = GL_FLOAT; break;
    case TextureFormat::R16_Float:
        read_format = GL_RED;
        read_type = GL_HALF_FLOAT;
        break;
    case TextureFormat::R32_Float:
        read_format = GL_RED;
        read_type = GL_FLOAT;
        break;
    default: break;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readback_fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer->handle());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(texture->desc().width), static_cast<GLsizei>(texture->desc().height),
                 read_format, read_type, nullptr);
    const uint32_t bytes_per_pixel = textureFormatBytesPerPixel(texture->desc().format);
    const size_t row_bytes = static_cast<size_t>(texture->desc().width) * bytes_per_pixel;
    const size_t image_bytes = row_bytes * texture->desc().height;
    bool copied = false;
    if (bytes_per_pixel && image_bytes <= buffer->desc().size) {
        auto* mapped = static_cast<uint8_t*>(glMapNamedBufferRange(
                buffer->handle(), 0, static_cast<GLsizeiptr>(image_bytes), GL_MAP_READ_BIT | GL_MAP_WRITE_BIT));
        if (mapped) {
            std::vector<uint8_t> row(row_bytes);
            for (uint32_t y = 0; y < texture->desc().height / 2; ++y) {
                auto* top = mapped + static_cast<size_t>(y) * row_bytes;
                auto* bottom = mapped + static_cast<size_t>(texture->desc().height - 1 - y) * row_bytes;
                std::memcpy(row.data(), top, row_bytes);
                std::memcpy(top, bottom, row_bytes);
                std::memcpy(bottom, row.data(), row_bytes);
            }
            glUnmapNamedBuffer(buffer->handle());
            copied = true;
        }
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    if (resolve_texture)
        glDeleteTextures(1, &resolve_texture);
    if (resolve_fbo)
        glDeleteFramebuffers(1, &resolve_fbo);
    if (read_fbo)
        glDeleteFramebuffers(1, &read_fbo);
    if (!copied)
        return rejectCopy("OpenGL texture readback failed or the destination buffer is too small");
    return {};
}

void GLCommandList::doSetComputePipelineState(ComputePipelineState*) {
    LOG_ERROR("[OpenGL] Compute pipeline binding rejected: compute is not implemented");
    rejectRecording("OpenGL compute pipelines are not implemented");
}

Buffer* GLCommandList::currentVertexBuffer(uint32_t slot) const {
    if (slot >= MAX_VERTEX_BUFFERS)
        return nullptr;
    return vertex_buffers_[slot];
}

GLCommandList::GLAttribType GLCommandList::vertexFormatToGL(VertexFormat fmt) {
    const auto& info = getVertexFormatInfo(fmt);
    GLenum type = GL_FLOAT;
    if (info.isFloat) {
        switch (info.bytesPerComponent) {
        case 2: type = GL_HALF_FLOAT; break;
        case 4: type = GL_FLOAT; break;
        default: type = GL_FLOAT; break;
        }
    } else if (info.isInteger) {
        switch (info.bytesPerComponent) {
        case 1: type = info.isNormalized ? GL_UNSIGNED_BYTE : GL_UNSIGNED_BYTE; break;
        case 2: type = GL_UNSIGNED_SHORT; break;
        case 4: type = GL_UNSIGNED_INT; break;
        default: type = GL_UNSIGNED_INT; break;
        }
    } else if (info.isNormalized) {
        // SNorm / UNorm
        switch (info.bytesPerComponent) {
        case 1: type = info.format == VertexFormat::Byte4N ? GL_BYTE : GL_UNSIGNED_BYTE; break;
        case 2: type = GL_UNSIGNED_SHORT; break;
        default: type = GL_FLOAT; break;
        }
    }
    return { type, static_cast<GLint>(info.componentCount),
             static_cast<GLboolean>(info.isNormalized ? GL_TRUE : GL_FALSE), info.isInteger && !info.isNormalized };
}

void GLCommandList::setupVertexAttributes() {
    if (!vertex_layout_dirty_)
        return;
    if (!current_pipeline_)
        return;

    const auto& layout = current_pipeline_->desc().vertexLayout;
    if (layout.empty())
        return;

    GLsizei stride = static_cast<GLsizei>(layout.stride());

    // 通过 DSA 先禁用所有属性，再按布局重新开启
    for (GLuint i = 0; i < 16; ++i)
        glDisableVertexArrayAttrib(vao_, i);

    const uint32_t slotCount = std::min<uint32_t>(layout.bufferCount(), MAX_VERTEX_BUFFERS);
    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        auto* glBuf = vertex_buffers_[slot] ? static_cast<GLBuffer*>(vertex_buffers_[slot]) : nullptr;
        glVertexArrayVertexBuffer(vao_, slot, glBuf ? glBuf->handle() : 0,
                                  glBuf ? static_cast<GLintptr>(vertex_buffer_offsets_[slot]) : 0, stride);
        glVertexArrayBindingDivisor(vao_, slot, 0);
    }

    for (uint8_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];
        GLuint location = static_cast<GLuint>(i);  // location = attribute index

        uint8_t slot = attr.bufferSlot;
        if (slot >= slotCount || !vertex_buffers_[slot]) {
            continue;  // 该 slot 无 VBO，跳过
        }

        auto glType = vertexFormatToGL(attr.format);
        glEnableVertexArrayAttrib(vao_, location);
        glVertexArrayAttribBinding(vao_, location, slot);
        if (glType.isInteger) {
            glVertexArrayAttribIFormat(vao_, location, glType.components, glType.type, attr.offset);
        } else {
            glVertexArrayAttribFormat(vao_, location, glType.components, glType.type, glType.normalized, attr.offset);
        }
    }

    vertex_layout_dirty_ = false;
}

void GLCommandList::applyPipelineState() {
    if (!current_pipeline_ || pipeline_state_applied_)
        return;

    auto* glPipeline = static_cast<GLPipelineState*>(current_pipeline_);

    // 应用着色器程序
    glUseProgram(glPipeline->program());

    // 应用渲染状态（栅格化、深度测试、混合等）
    glPipeline->applyRenderState();

    pipeline_state_applied_ = true;
}

GLenum GLCommandList::indexTypeToGLFormat(IndexType type) {
    switch (type) {
    case IndexType::UInt16: return GL_UNSIGNED_SHORT;
    case IndexType::UInt32: return GL_UNSIGNED_INT;
    default: return GL_UNSIGNED_INT;
    }
}

core::Result<void> GLCommandList::doBeginRenderPass(const RenderPassBeginInfo& info) {
    if (info.colorCount > RenderPassBeginInfo::kMaxColorTargets || info.width == 0 || info.height == 0)
        return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                         "OpenGL render pass dimensions or attachment count are invalid"));
    if (!info.presentSource) {
        for (uint8_t i = 0; i < info.colorCount; ++i) {
            if (!info.colorAttachments[i].target)
                return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                                 "OpenGL offscreen render pass requires color textures"));
        }
    }
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        const Texture* color = info.colorAttachments[i].target;
        const Texture* resolve = info.colorAttachments[i].resolveTarget;
        if (!resolve)
            continue;
        if (i != 0 || !color || color->desc().sampleCount <= 1 || resolve->desc().sampleCount != 1 ||
            resolve->desc().format != color->desc().format || resolve->width() != color->width() ||
            resolve->height() != color->height()) {
            return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                             "OpenGL resolve attachment is unsupported or incompatible"));
        }
    }
    GLint previous_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_fbo);
    previous_framebuffer_ = static_cast<GLuint>(previous_fbo);
    resolve_source_ = info.colorCount > 0 ? static_cast<GLTexture*>(info.colorAttachments[0].target) : nullptr;
    resolve_target_ = info.colorCount > 0 ? static_cast<GLTexture*>(info.colorAttachments[0].resolveTarget) : nullptr;

    GLuint framebuffer = 0;
    if (resolve_source_)
        framebuffer = resolve_source_->renderTargetFramebuffer();
    const bool requiresOffscreenFramebuffer = !info.presentSource || resolve_source_ != nullptr;
    if (requiresOffscreenFramebuffer && framebuffer == 0) {
        glCreateFramebuffers(1, &temporary_framebuffer_);
        if (temporary_framebuffer_ == 0) {
            LOG_ERROR("[OpenGL] Failed to create a temporary framebuffer");
            resolve_source_ = nullptr;
            resolve_target_ = nullptr;
            return std::unexpected(
                    makeError(EngineErrorCode::CommandRecordingFailed, "OpenGL temporary framebuffer creation failed"));
        }
        framebuffer = temporary_framebuffer_;
        for (uint8_t i = 0; i < info.colorCount; ++i) {
            auto* color = static_cast<GLTexture*>(info.colorAttachments[i].target);
            if (color)
                glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0 + i, color->handle(), 0);
        }
        if (auto* depth = static_cast<GLTexture*>(info.depthAttachment.target)) {
            const GLenum attachment = depth->desc().format == TextureFormat::D24_UNorm_S8_UInt ||
                                                      depth->desc().format == TextureFormat::D32_Float_S8X24_UInt
                                              ? GL_DEPTH_STENCIL_ATTACHMENT
                                              : GL_DEPTH_ATTACHMENT;
            glNamedFramebufferTexture(framebuffer, attachment, depth->handle(), 0);
        }
        if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOG_ERROR("[OpenGL] Temporary framebuffer is incomplete");
            glDeleteFramebuffers(1, &temporary_framebuffer_);
            temporary_framebuffer_ = 0;
            resolve_source_ = nullptr;
            resolve_target_ = nullptr;
            return std::unexpected(
                    makeError(EngineErrorCode::CommandRecordingFailed, "OpenGL temporary framebuffer is incomplete"));
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    active_framebuffer_ = framebuffer;
    render_pass_active_ = true;
    framebuffer_height_ = static_cast<int32_t>(info.height);

    if (info.colorCount > 0) {
        std::array<GLenum, RenderPassBeginInfo::kMaxColorTargets> draw_buffers{};
        for (uint8_t i = 0; i < info.colorCount; ++i)
            draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
        if (framebuffer != 0)
            glDrawBuffers(info.colorCount, draw_buffers.data());

        const auto* color = static_cast<GLTexture*>(info.colorAttachments[0].target);
        const bool srgb = color && (color->desc().format == TextureFormat::RGBA8_sRGB ||
                                    color->desc().format == TextureFormat::BGRA8_sRGB);
        if (srgb)
            glEnable(GL_FRAMEBUFFER_SRGB);
        else
            glDisable(GL_FRAMEBUFFER_SRGB);
    }

    glViewport(0, 0, static_cast<GLsizei>(info.width), static_cast<GLsizei>(info.height));

    // Perform clears based on LoadAction. Depth/color clears obey write masks,
    // so restore masks before clearing to avoid stale depth from a previous pipeline.
    GLbitfield clearBits = 0;
    for (uint32_t i = 0; i < info.colorCount; ++i) {
        if (info.colorAttachments[i].loadAction == LoadAction::Clear) {
            glClearColor(info.clearColor[0], info.clearColor[1], info.clearColor[2], info.clearColor[3]);
            clearBits |= GL_COLOR_BUFFER_BIT;
        }
    }
    if (info.depthAttachment.loadAction == LoadAction::Clear && (info.depthAttachment.target || info.presentSource)) {
        glClearDepth(static_cast<GLdouble>(info.clearDepth));
        clearBits |= GL_DEPTH_BUFFER_BIT;
        if (auto* depth = static_cast<GLTexture*>(info.depthAttachment.target);
            depth && (depth->desc().format == TextureFormat::D24_UNorm_S8_UInt ||
                      depth->desc().format == TextureFormat::D32_Float_S8X24_UInt)) {
            glClearStencil(info.clearStencil);
            clearBits |= GL_STENCIL_BUFFER_BIT;
        } else if (info.presentSource) {
            GLint stencil_bits = 0;
            glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
            if (stencil_bits > 0) {
                glClearStencil(info.clearStencil);
                clearBits |= GL_STENCIL_BUFFER_BIT;
            }
        }
    }
    if (clearBits != 0) {
        const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        if (clearBits & GL_COLOR_BUFFER_BIT) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        if (clearBits & GL_DEPTH_BUFFER_BIT) {
            glDepthMask(GL_TRUE);
        }
        glClear(clearBits);
        if (scissor_enabled)
            glEnable(GL_SCISSOR_TEST);
    }

    // Mark viewport dirty since we overrode it
    viewport_Dirty = true;
    scissor_dirty_ = true;
    return {};
}

void GLCommandList::doEndRenderPass() {
    if (render_pass_active_ && resolve_source_ && resolve_target_ && resolve_target_->desc().sampleCount == 1) {
        GLuint resolve_fbo = 0;
        glCreateFramebuffers(1, &resolve_fbo);
        glNamedFramebufferTexture(resolve_fbo, GL_COLOR_ATTACHMENT0, resolve_target_->handle(), 0);
        glNamedFramebufferDrawBuffer(resolve_fbo, GL_COLOR_ATTACHMENT0);
        glBlitNamedFramebuffer(active_framebuffer_, resolve_fbo, 0, 0,
                               static_cast<GLint>(resolve_source_->desc().width),
                               static_cast<GLint>(resolve_source_->desc().height), 0, 0,
                               static_cast<GLint>(resolve_target_->desc().width),
                               static_cast<GLint>(resolve_target_->desc().height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDeleteFramebuffers(1, &resolve_fbo);
    }
    if (render_pass_active_)
        glBindFramebuffer(GL_FRAMEBUFFER, previous_framebuffer_);
    if (temporary_framebuffer_) {
        glDeleteFramebuffers(1, &temporary_framebuffer_);
        temporary_framebuffer_ = 0;
    }
    active_framebuffer_ = 0;
    render_pass_active_ = false;
    resolve_source_ = nullptr;
    resolve_target_ = nullptr;
}

}  // namespace mulan::engine
