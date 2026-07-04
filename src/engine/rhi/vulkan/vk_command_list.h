/**
 * @file vk_command_list.h
 * @brief Vulkan命令列表实现，支持独立与外部buffer两种模式
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../command_list.h"
#include "vk_convert.h"
#include "vk_buffer.h"
#include "vk_pipeline_state.h"
#include "vk_descriptor_set.h"

#include <mulan/core/result/error.h>

#include <array>
#include <expected>
#include <memory>
#include <optional>

namespace mulan::engine {

class VKDescriptorAllocator;
class VKDevice;

class VKCommandList : public CommandList {
public:
    /// 独立模式：自建 command pool + buffer（可选 descriptor allocator）。
    /// 失败返回 CommandListCreateFailed。
    static std::expected<std::unique_ptr<VKCommandList>, core::Error>
        create(vk::Device device, uint32_t queueFamilyIndex,
               VKDescriptorAllocator* allocator = nullptr);

    /// 外部 buffer 模式：引用 frameContext 的 command buffer
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd);

    /// 外部 buffer 模式 + descriptor allocator（帧循环用）
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd,
                  VKDescriptorAllocator* allocator);

    ~VKCommandList();

    vk::CommandBuffer cmdBuffer() const { return cmd_buffer_; }

    // --- 生命周期 ---
    void begin() override;
    void end() override;

    // --- 管线状态 ---
    void setPipelineState(PipelineState* pso) override;

    // --- 资源绑定 ---
    void bindGroup(BindGroup& group) override;
    void bindResources(const BindGroupDesc& desc) override;

    // --- 视口 / 裁剪 ---
    void setViewport(const Viewport& vp) override;
    void setScissorRect(const ScissorRect& rect) override;

    // --- 缓冲区绑定 ---
    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) override;
    void setVertexBuffers(uint32_t startSlot, uint32_t count,
                          Buffer** buffers, uint32_t* offsets) override;
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0,
                        IndexType type = IndexType::UInt32) override;

    // --- 绘制 ---
    void draw(const DrawAttribs& attribs) override;
    void drawIndexed(const DrawIndexedAttribs& attribs) override;
    void drawIndirect(Buffer* argsBuffer, uint32_t offset,
                      uint32_t drawCount = 1, uint32_t stride = 0) override;

    // --- Compute ---
    void dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) override;
    void dispatchIndirect(Buffer* argsBuffer, uint32_t offset) override;

    // --- Push Constants ---
    void setPushConstants(uint32_t offset, uint32_t size,
                          const void* data, uint32_t stageFlags) override;

    // --- 资源更新 ---
    void updateBuffer(Buffer* buffer, uint32_t offset,
                      uint32_t size, const void* data,
                      ResourceTransitionMode mode =
                          ResourceTransitionMode::Transition) override;

    // --- 资源状态转换 ---
    void transitionResource(Buffer* buffer, ResourceState newState) override;
    void transitionResource(Texture* texture, ResourceState newState) override;

    // --- 纹理 → 缓冲区复制（用于离屏回读）---
    void copyTextureToBuffer(Texture* src, Buffer* dst) override;

    // --- 清除 ---
    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;
    void clearStencil(uint8_t stencil) override;

    // --- RenderPass (RHI override) ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;

    vk::PipelineLayout currentLayout() const { return current_layout_; }

private:
    // 独立模式私有构造（create() 使用）
    VKCommandList(vk::Device device, vk::CommandPool pool, vk::CommandBuffer cmd)
        : device_(device), pool_(pool), cmd_buffer_(cmd), owns_pool_(true) {}

    vk::Device              device_;
    vk::CommandPool         pool_;
    vk::CommandBuffer       cmd_buffer_;
    vk::PipelineLayout      current_layout_;
    vk::DescriptorSetLayout current_desc_set_layout_;
    VKDescriptorAllocator*  allocator_ = nullptr;
    bool                    rp_present_source_ = false;
    std::optional<vk::Image> swapchain_color_image_; // endRenderPass 时转 PRESENT_SRC_KHR
    bool                    owns_pool_;
};

} // namespace mulan::engine
