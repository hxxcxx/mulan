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
#include "sampler.h"
#include "submission.h"
#include "render_types.h"
#include "uniform_slice.h"

#include <mulan/core/result/error.h>

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <span>
#include <optional>
#include <string_view>
#include <type_traits>

namespace mulan::engine {

// ============================================================
// 前向声明
// ============================================================

class PipelineState;
class ComputePipelineState;
class Shader;
class Texture;

// ============================================================
// 命令列表基类
// ============================================================

class CommandList : public RHITrackedResource {
public:
    enum class State : uint8_t {
        Initial,
        Recording,
        Executable,
        Submitted,
        Invalid,
    };

    virtual ~CommandList() = default;

    // --- 生命周期 ---

    core::Result<void> begin();
    core::Result<void> end();
    State state() const noexcept { return state_; }
    bool isExecutable() const noexcept { return state_ == State::Executable; }
    const core::Error* recordingError() const noexcept;

    // --- 管线状态 ---

    void setPipelineState(PipelineState* pso);
    void setComputePipelineState(ComputePipelineState* pso);

    // --- 资源绑定 ---

    /// 绑定对象化 BindGroup（后端缓存 descriptor 句柄，零分配）。
    void bindGroup(BindGroup& group);

    /// 绑定对象化 BindGroup，并提供 layout 中声明为 Dynamic 的 UniformBuffer 切片。
    /// 默认实现仅接受空动态绑定，后端实现瞬态 Uniform 分配后覆盖该入口。
    void bindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms);

    /// 将一份小型常量数据写入当前录制周期的后端瞬态 Uniform 分配器。
    core::Result<UniformSlice> writeUniformBytes(std::span<const std::byte> data);

    template <typename T>
    core::Result<UniformSlice> writeUniform(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Uniform data must be trivially copyable");
        return writeUniformBytes(std::as_bytes(std::span{ &value, size_t{ 1 } }));
    }

    // --- 视口 / 裁剪 ---

    void setViewport(const Viewport& vp);
    void setScissorRect(const ScissorRect& rect);

    // --- 缓冲区绑定（核心：Buffer 不自带 bind，由 CommandList 统一管理）---

    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0);
    void setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets);
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0, IndexType type = IndexType::UInt32);

    // --- 绘制 ---

    void draw(const DrawAttribs& attribs);
    void drawIndexed(const DrawIndexedAttribs& attribs);

    // --- 间接绘制（GPU-driven，buffer 内含 DrawIndexedAttribs）---
    // Indirect draw arguments layout in buffer:
    //   DrawIndirectArgs: { indexCount, instanceCount, firstIndex, baseVertex, startInstance }
    //   DispatchIndirectArgs: { groupCountX, groupCountY, groupCountZ }
    void drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount = 1, uint32_t stride = 0);

    // --- Compute ---

    /// 执行 compute shader dispatch
    void dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ);

    /// 间接 dispatch（GPU-driven）
    void dispatchIndirect(Buffer* argsBuffer, uint32_t offset);

    // --- Push Constants（快速小数据路径，不走 UBO）---

    /// 设置 push / root constants（所有后端统一）
    /// @param offset  偏移（字节），对应 shader 中 layout(offset=...) 或 root constant offset
    /// @param size    数据大小（字节），必须是 4 的倍数
    /// @param data    数据指针
    /// @param stageFlags 着色器阶段（PipelineBinding::kStageVertex 等）
    void setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags);

    // --- 资源状态转换 ---

    void transitionResource(Texture* texture, ResourceState newState);

    /// 将渲染目标 color 纹理复制到 staging buffer（用于 CPU 回读）。
    core::Result<void> copyTextureToBuffer(Texture* src, Buffer* dst);

    // --- RenderPass ---

    void beginRenderPass(const RenderPassBeginInfo& info);
    void endRenderPass();

    /// 设备在 queue submit 成功后调用，记录 CommandList 自身的重用边界。
    void markSubmitted(SubmissionToken token);

protected:
    CommandList() = default;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    core::Result<void> waitForPreviousSubmission();
    virtual core::Result<void> doBegin() = 0;
    virtual core::Result<void> doEnd() = 0;
    virtual void doMarkSubmitted() {}
    virtual void doSetPipelineState(PipelineState* pso) = 0;
    virtual void doSetComputePipelineState(ComputePipelineState* pso) = 0;
    virtual void doBindGroup(BindGroup& group) = 0;
    virtual void doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms);
    virtual core::Result<UniformSlice> doWriteUniformBytes(std::span<const std::byte> data);
    virtual void doSetViewport(const Viewport& vp) = 0;
    virtual void doSetScissorRect(const ScissorRect& rect) = 0;
    virtual void doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) = 0;
    virtual void doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) = 0;
    virtual void doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) = 0;
    virtual void doDraw(const DrawAttribs& attribs) = 0;
    virtual void doDrawIndexed(const DrawIndexedAttribs& attribs) = 0;
    virtual void doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) = 0;
    virtual void doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) = 0;
    virtual void doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) = 0;
    virtual void doTransitionResource(Texture* texture, ResourceState newState) = 0;
    virtual core::Result<void> doCopyTextureToBuffer(Texture* src, Buffer* dst) = 0;
    virtual core::Result<void> doBeginRenderPass(const RenderPassBeginInfo& info) = 0;
    virtual void doEndRenderPass() = 0;

    void invalidate(core::Error error);
    void rejectRecording(std::string_view reason);
    void activateBindGroupLayout(const BindGroupLayout& layout);
    bool validateBindGroupCompatible(const BindGroup& group);
    bool requireRecording(std::string_view operation);
    bool requireGraphicsDraw(std::string_view operation);
    bool requireComputeDispatch(std::string_view operation);
    bool validateResource(const RHITrackedResource* resource, std::string_view operation);
    void assertResourceCompatible(const RHITrackedResource* resource) const noexcept {
#ifndef NDEBUG
        if (!resource)
            return;
        if (trackingDevice())
            assert(!resource->isTracked() || resource->belongsTo(*trackingDevice()));
#else
        (void) resource;
#endif
    }

    void assertBindGroupCompatible(const BindGroup& group) const noexcept {
#ifndef NDEBUG
        assertResourceCompatible(&group);
        for (uint8_t i = 0; i < group.entryCount(); ++i) {
            const BindGroupEntry& entry = group.entries()[i];
            if (entry.buffer)
                assertResourceCompatible(entry.buffer);
            if (entry.texture)
                assertResourceCompatible(entry.texture);
            if (entry.sampler)
                assertResourceCompatible(entry.sampler);
        }
#else
        (void) group;
#endif
    }

    void assertRenderPassCompatible(const RenderPassBeginInfo& info) const noexcept {
#ifndef NDEBUG
        assert(info.colorCount <= RenderPassBeginInfo::kMaxColorTargets);
        for (uint8_t i = 0; i < info.colorCount; ++i) {
            if (info.colorAttachments[i].target)
                assertResourceCompatible(info.colorAttachments[i].target);
            if (info.colorAttachments[i].resolveTarget)
                assertResourceCompatible(info.colorAttachments[i].resolveTarget);
        }
        if (info.depthAttachment.target)
            assertResourceCompatible(info.depthAttachment.target);
        if (info.depthAttachment.resolveTarget)
            assertResourceCompatible(info.depthAttachment.resolveTarget);
#else
        (void) info;
#endif
    }

private:
    enum class PipelineKind : uint8_t {
        None,
        Graphics,
        Compute,
    };

    SubmissionToken last_submission_;
    State state_ = State::Initial;
    bool backend_recording_ = false;
    bool render_pass_active_ = false;
    PipelineKind pipeline_kind_ = PipelineKind::None;
    uint32_t push_constant_size_ = 0;
    const BindGroupLayout* active_bind_group_layout_ = nullptr;
    std::optional<core::Error> recording_error_;
};

}  // namespace mulan::engine
