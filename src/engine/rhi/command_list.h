/**
 * @file command_list.h
 * @brief GPU命令录制接口，支持多后端实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "buffer.h"
#include "bind_group.h"
#include "resource.h"
#include "render_types.h"
#include "uniform_slice.h"

#include <mulan/core/result/error.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

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

class CommandList : public RHITrackedResource {
public:
    virtual ~CommandList() = default;

    // --- 生命周期 ---

    virtual void begin() = 0;
    virtual void end() = 0;

    // --- 管线状态 ---

    virtual void setPipelineState(PipelineState* pso) = 0;

    // --- 资源绑定 ---

    /// 绑定对象化 BindGroup（后端缓存 descriptor 句柄，零分配）。
    virtual void bindGroup(BindGroup& group) = 0;

    /// 绑定对象化 BindGroup，并提供 layout 中声明为 Dynamic 的 UniformBuffer 切片。
    /// 默认实现仅接受空动态绑定，后端实现瞬态 Uniform 分配后覆盖该入口。
    virtual void bindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms);

    /// 便捷路径：从 BindGroupDesc 临时构建并绑定（无缓存，每帧重新分配）。
    /// 用于兼容旧代码或动态变化的绑定场景。
    virtual void bindResources(const BindGroupDesc& desc) = 0;

    /// 将一份小型常量数据写入当前录制周期的后端瞬态 Uniform 分配器。
    virtual core::Result<UniformSlice> writeUniformBytes(std::span<const std::byte> data);

    template <typename T>
    core::Result<UniformSlice> writeUniform(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Uniform data must be trivially copyable");
        return writeUniformBytes(std::as_bytes(std::span{ &value, size_t{ 1 } }));
    }

    // --- 视口 / 裁剪 ---

    virtual void setViewport(const Viewport& vp) = 0;
    virtual void setScissorRect(const ScissorRect& rect) = 0;

    // --- 缓冲区绑定（核心：Buffer 不自带 bind，由 CommandList 统一管理）---

    virtual void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) = 0;
    virtual void setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) = 0;
    virtual void setIndexBuffer(Buffer* buffer, uint32_t offset = 0, IndexType type = IndexType::UInt32) = 0;

    // --- 绘制 ---

    virtual void draw(const DrawAttribs& attribs) = 0;
    virtual void drawIndexed(const DrawIndexedAttribs& attribs) = 0;

    // --- 间接绘制（GPU-driven，buffer 内含 DrawIndexedAttribs）---
    // Indirect draw arguments layout in buffer:
    //   DrawIndirectArgs: { indexCount, instanceCount, firstIndex, baseVertex, startInstance }
    //   DispatchIndirectArgs: { groupCountX, groupCountY, groupCountZ }
    virtual void drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount = 1, uint32_t stride = 0) {
        (void) argsBuffer;
        (void) offset;
        (void) drawCount;
        (void) stride;
    }

    // --- Compute ---

    /// 执行 compute shader dispatch
    virtual void dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
        (void) threadGroupX;
        (void) threadGroupY;
        (void) threadGroupZ;
    }

    /// 间接 dispatch（GPU-driven）
    virtual void dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
        (void) argsBuffer;
        (void) offset;
    }

    // --- Push Constants（快速小数据路径，不走 UBO）---

    /// 设置 push / root constants（所有后端统一）
    /// @param offset  偏移（字节），对应 shader 中 layout(offset=...) 或 root constant offset
    /// @param size    数据大小（字节），必须是 4 的倍数
    /// @param data    数据指针
    /// @param stageFlags 着色器阶段（PipelineBinding::kStageVertex 等）
    virtual void setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) {
        (void) offset;
        (void) size;
        (void) data;
        (void) stageFlags;
    }

    // --- 资源更新 ---

    virtual void updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                              ResourceTransitionMode mode = ResourceTransitionMode::Transition) = 0;

    // --- 资源状态转换 ---

    virtual void transitionResource(Buffer* buffer, ResourceState newState) = 0;

    virtual void transitionResource(Texture* texture, ResourceState newState) {
        (void) texture;
        (void) newState;
    }

    /// 将渲染目标 color 纹理复制到 staging buffer（用于 CPU 回读）。
    /// 返回 false 表示当前后端或资源组合不支持该复制。
    virtual bool copyTextureToBuffer(Texture* src, Buffer* dst) {
        (void) src;
        (void) dst;
        return false;
    }

    // --- RenderPass ---

    virtual void beginRenderPass(const RenderPassBeginInfo& info) { (void) info; }
    virtual void endRenderPass() {}

    // --- 清除 ---

    virtual void clearColor(float r, float g, float b, float a) = 0;
    virtual void clearDepth(float depth) = 0;
    virtual void clearStencil(uint8_t stencil) = 0;

    /// 设备在 queue submit 成功后调用，将本次使用传播到全部资源。
    void markSubmitted(SubmissionToken token);

protected:
    CommandList() = default;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    void resetResourceUsage();
    void recordResourceUse(RHITrackedResource* resource);
    void recordBindGroupUse(BindGroup& group);
    void recordBindGroupUse(const BindGroupDesc& desc);
    void recordRenderPassUse(const RenderPassBeginInfo& info);

private:
    std::vector<RHITrackedResource*> used_resources_;
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

}  // namespace mulan::engine
