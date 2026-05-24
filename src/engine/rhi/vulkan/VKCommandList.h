/**
 * @file VKCommandList.h
 * @brief Vulkan命令列表实现，支持独立与外部buffer两种模式
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../CommandList.h"
#include "VkConvert.h"
#include "VKBuffer.h"
#include "VKPipelineState.h"
#include "VKDescriptorSet.h"

#include <array>

namespace mulan::engine {

class VKDescriptorAllocator;
class VKDevice;

class VKCommandList : public CommandList {
public:
    /// 独立模式：自己创建 command pool + buffer
    VKCommandList(vk::Device device, uint32_t queueFamilyIndex);

    /// 独立模式 + descriptor allocator（用于 bindResources）
    VKCommandList(vk::Device device, uint32_t queueFamilyIndex,
                  VKDescriptorAllocator* allocator);

    /// 外部 buffer 模式：引用 frameContext 的 command buffer
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd);

    /// 外部 buffer 模式 + descriptor allocator（帧循环用）
    VKCommandList(vk::Device device, vk::CommandBuffer externalCmd,
                  VKDescriptorAllocator* allocator);

    ~VKCommandList();

    vk::CommandBuffer cmdBuffer() const { return m_cmdBuffer; }

    // --- 生命周期 ---
    void begin() override;
    void end() override;

    // --- 管线状态 ---
    void setPipelineState(PipelineState* pso) override;

    // --- 资源绑定 ---
    void bindResources(const BindGroup& group) override;

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

    // --- Vulkan 特有：开始/结束 RenderPass（底层，被 RHI override 调用）---
    void beginVkRenderPass(vk::RenderPass renderPass, vk::Framebuffer framebuffer,
                           uint32_t width, uint32_t height,
                           const std::array<float, 4>& clearColor = {0.15f, 0.15f, 0.15f, 1.0f},
                           float clearDepth = 1.0f);
    void endRenderPass() override;

    // --- RenderPass (RHI override) ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;

    vk::PipelineLayout currentLayout() const { return m_currentLayout; }

    /// 设置所属 VKDevice（用于访问 RenderPass/Framebuffer cache）
    void setOwnerDevice(VKDevice* dev) { m_ownerDevice = dev; }

    /// 绑定 descriptor set 到当前管线
    void bindDescriptorSet(vk::PipelineLayout layout, vk::DescriptorSet set,
                           uint32_t firstSet = 0);

private:
    vk::Device              m_device;
    vk::CommandPool         m_pool;
    vk::CommandBuffer       m_cmdBuffer;
    vk::PipelineLayout      m_currentLayout;
    vk::DescriptorSetLayout m_currentDescSetLayout;
    VKDescriptorAllocator*  m_allocator = nullptr;
    VKDevice*               m_ownerDevice = nullptr;
    bool                    m_ownsPool;
};

} // namespace mulan::Engine
