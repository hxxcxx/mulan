/**
 * @file command_list.h
 * @brief GPU命令录制接口，支持多后端实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "buffer.h"
#include "bind_group.h"
#include "render_types.h"

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

    /// 绑定对象化 BindGroup（后端缓存 descriptor 句柄，零分配）。
    virtual void bindGroup(BindGroup& group) = 0;

    /// 便捷路径：从 BindGroupDesc 临时构建并绑定（无缓存，每帧重新分配）。
    /// 用于兼容旧代码或动态变化的绑定场景。
    virtual void bindResources(const BindGroupDesc& desc) = 0;

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

    // --- 间接绘制（GPU-driven，buffer 内含 DrawIndexedAttribs）---
    // Indirect draw arguments layout in buffer:
    //   DrawIndirectArgs: { indexCount, instanceCount, firstIndex, baseVertex, startInstance }
    //   DispatchIndirectArgs: { groupCountX, groupCountY, groupCountZ }
    virtual void drawIndirect(Buffer* argsBuffer, uint32_t offset,
                              uint32_t drawCount = 1, uint32_t stride = 0) {
        (void)argsBuffer; (void)offset; (void)drawCount; (void)stride;
    }

    // --- Compute ---

    /// 执行 compute shader dispatch
    virtual void dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
        (void)threadGroupX; (void)threadGroupY; (void)threadGroupZ;
    }

    /// 间接 dispatch（GPU-driven）
    virtual void dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
        (void)argsBuffer; (void)offset;
    }

    // --- Push Constants（快速小数据路径，不走 UBO）---

    /// 设置 push / root constants（所有后端统一）
    /// @param offset  偏移（字节），对应 shader 中 layout(offset=...) 或 root constant offset
    /// @param size    数据大小（字节），必须是 4 的倍数
    /// @param data    数据指针
    /// @param stageFlags 着色器阶段（PipelineBinding::kStageVertex 等）
    virtual void setPushConstants(uint32_t offset, uint32_t size,
                                  const void* data, uint32_t stageFlags) {
        (void)offset; (void)size; (void)data; (void)stageFlags;
    }

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
    explicit ScopedCommandRecorder(CommandList* cmd) : cmd_(cmd) { cmd_->begin(); }
    ~ScopedCommandRecorder() { cmd_->end(); }

    CommandList* operator->() { return cmd_; }
    CommandList& cmd() { return *cmd_; }

    ScopedCommandRecorder(const ScopedCommandRecorder&) = delete;
    ScopedCommandRecorder& operator=(const ScopedCommandRecorder&) = delete;

private:
    CommandList* cmd_;
};

} // namespace mulan::engine
