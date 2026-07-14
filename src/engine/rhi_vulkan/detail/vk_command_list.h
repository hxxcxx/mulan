/**
 * @file vk_command_list.h
 * @brief Vulkan命令列表实现，支持独立与外部buffer两种模式
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../rhi/command_list.h"
#include "vk_convert.h"
#include "vk_buffer.h"
#include "vk_pipeline_state.h"
#include "vk_descriptor_set.h"

#include <mulan/core/result/error.h>

#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace mulan::engine {

class VKDescriptorAllocator;
class VKDevice;
class VKTexture;
class VKBuffer;
class VKBindGroup;
class VKTransientUniformArena;

class VKCommandList : public CommandList {
public:
    /// 独立模式：自建 command pool + buffer（可选 descriptor allocator）。
    /// 失败返回 CommandListCreateFailed。
    static core::Result<std::unique_ptr<VKCommandList>> create(vk::Device device, uint32_t queueFamilyIndex,
                                                               VKDescriptorAllocator* allocator,
                                                               VmaAllocator memoryAllocator, uint32_t uniformAlignment,
                                                               uint32_t maxUniformSize);

    /// 外部 buffer 模式：引用 frameContext 的 command buffer
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd);

    /// 外部 buffer 模式 + descriptor allocator（帧循环用）
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd, VKDescriptorAllocator* allocator,
                  VKTransientUniformArena* transientUniformArena);

    ~VKCommandList();

    vk::CommandBuffer cmdBuffer() const { return cmd_buffer_; }

    // --- 生命周期 ---
    core::Result<void> doBegin() override;
    core::Result<void> doEnd() override;

    // --- 管线状态 ---
    void doSetPipelineState(PipelineState* pso) override;
    void doSetComputePipelineState(ComputePipelineState* pso) override;

    // --- 资源绑定 ---
    void doBindGroup(BindGroup& group) override;
    void doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) override;
    core::Result<UniformSlice> doWriteUniformBytes(std::span<const std::byte> data) override;

    // --- 视口 / 裁剪 ---
    void doSetViewport(const Viewport& vp) override;
    void doSetScissorRect(const ScissorRect& rect) override;

    // --- 缓冲区绑定 ---
    void doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) override;
    void doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;
    void doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) override;

    // --- 绘制 ---
    void doDraw(const DrawAttribs& attribs) override;
    void doDrawIndexed(const DrawIndexedAttribs& attribs) override;
    void doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) override;

    // --- Compute ---
    void doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) override;
    void doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) override;

    // --- Push Constants ---
    void doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) override;

    // --- 资源状态转换 ---
    void doTransitionResource(Texture* texture, ResourceState newState) override;

    // --- 纹理 → 缓冲区复制（用于离屏回读）---
    core::Result<void> doCopyTextureToBuffer(Texture* src, Buffer* dst) override;

    // --- 清除 ---

    // --- RenderPass (RHI override) ---
    core::Result<void> doBeginRenderPass(const RenderPassBeginInfo& info) override;
    void doEndRenderPass() override;

    vk::PipelineLayout currentLayout() const { return current_layout_; }

    // 注入当前帧的 frame token（由 VKDevice::frameCommandList 设置）。
    // bindGroup 据此判断 BindGroup 缓存句柄是否跨帧失效。独立 cmd list 不调用，
    // token 保持 0，BindGroup token 也初始为 0 → 视为同帧，不会误失效。
    void setFrameToken(uint64_t token) { frame_token_ = token; }

private:
    struct DynamicDescriptorSetCacheEntry {
        VKBindGroup* group = nullptr;
        std::array<VKBuffer*, BindGroupDesc::kMaxEntries> buffers{};
        std::array<uint32_t, BindGroupDesc::kMaxEntries> bindings{};
        std::array<uint32_t, BindGroupDesc::kMaxEntries> ranges{};
        uint8_t count = 0;
        vk::DescriptorSet set;
    };

    // 独立模式私有构造（create() 使用）
    VKCommandList(vk::Device device, vk::CommandPool pool, vk::CommandBuffer cmd);

    vk::Device device_;
    vk::CommandPool pool_;
    vk::CommandBuffer cmd_buffer_;
    vk::PipelineLayout current_layout_;
    vk::DescriptorSetLayout current_desc_set_layout_;
    vk::PipelineBindPoint current_bind_point_ = vk::PipelineBindPoint::eGraphics;
    uint32_t current_push_constant_size_ = 0;
    VKDescriptorAllocator* allocator_ = nullptr;
    bool rp_present_source_ = false;
    VKTexture* swapchain_color_texture_ = nullptr;  // endRenderPass 时转 PRESENT_SRC_KHR
    bool owns_pool_;
    uint64_t frame_token_ = 0;                      // 当前帧 token，0=独立/未注入
    std::unique_ptr<VKTransientUniformArena> owned_transient_uniform_arena_;
    VKTransientUniformArena* transient_uniform_arena_ = nullptr;
    std::vector<DynamicDescriptorSetCacheEntry> dynamic_set_cache_;
};

}  // namespace mulan::engine
