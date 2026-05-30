/**
 * @file CommandList.h
 * @brief GPU命令录制接口，支持多后端实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "Buffer.h"
#include "BindGroup.h"
#include "RenderTypes.h"

#include <cstdint>

namespace mulan::engine {

// ============================================================
// 前向声明
// ============================================================

class PipelineState;
class Shader;
class Texture;

// ============================================================
// 命令列表基类
// ============================================================

class CommandList {
public:
    virtual ~CommandList() = default;

    // --- 生命周期 ---

    virtual void begin() = 0;
    virtual void end()   = 0;

    // --- 管线状态 ---

    virtual void setPipelineState(PipelineState* pso) = 0;

    // --- 资源绑定 ---

    /// 绑定资源组（UBO / Texture / 未来 Sampler）
    virtual void bindResources(const BindGroup& group) = 0;

    // --- 视口 / 裁剪 ---

    virtual void setViewport(const Viewport& vp) = 0;
    virtual void setScissorRect(const ScissorRect& rect) = 0;

    // --- 缓冲区绑定（核心：Buffer 不自带 bind，由 CommandList 统一管理）---

    virtual void setVertexBuffer(uint32_t slot, Buffer* buffer,
                                 uint32_t offset = 0) = 0;
    virtual void setVertexBuffers(uint32_t startSlot, uint32_t count,
                                  Buffer** buffers, uint32_t* offsets) = 0;
    virtual void setIndexBuffer(Buffer* buffer, uint32_t offset = 0,
                                IndexType type = IndexType::UInt32) = 0;

    // --- 绘制 ---

    virtual void draw(const DrawAttribs& attribs) = 0;
    virtual void drawIndexed(const DrawIndexedAttribs& attribs) = 0;

    // --- 资源更新 ---

    virtual void updateBuffer(Buffer* buffer, uint32_t offset,
                              uint32_t size, const void* data,
                              ResourceTransitionMode mode =
                                  ResourceTransitionMode::Transition) = 0;

    // --- 资源状态转换 ---

    virtual void transitionResource(Buffer* buffer,
                                    ResourceState newState) = 0;

    virtual void transitionResource(Texture* texture,
                                    ResourceState newState) { (void)texture; (void)newState; }

    /// 将渲染目标 color 纹理复制到 staging buffer（用于 CPU 回读）
    virtual void copyTextureToBuffer(Texture* src, Buffer* dst) { (void)src; (void)dst; }

    // --- RenderPass ---

    virtual void beginRenderPass(const RenderPassBeginInfo& info) { (void)info; }
    virtual void endRenderPass() {}

    // --- 清除 ---

    virtual void clearColor(float r, float g, float b, float a) = 0;
    virtual void clearDepth(float depth) = 0;
    virtual void clearStencil(uint8_t stencil) = 0;

protected:
    CommandList() = default;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;
};

// ============================================================
// RAII guard：作用域内自动 begin/end
// ============================================================

class ScopedCommandRecorder {
public:
    explicit ScopedCommandRecorder(CommandList* cmd) : m_cmd(cmd) { m_cmd->begin(); }
    ~ScopedCommandRecorder() { m_cmd->end(); }

    CommandList* operator->() { return m_cmd; }
    CommandList& cmd() { return *m_cmd; }

    ScopedCommandRecorder(const ScopedCommandRecorder&) = delete;
    ScopedCommandRecorder& operator=(const ScopedCommandRecorder&) = delete;

private:
    CommandList* m_cmd;
};

} // namespace mulan::engine
