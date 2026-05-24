/**
 * @file VKRenderTarget.h
 * @brief Vulkan 离屏渲染目标实现
 * @author hxxcxx
 * @date 2026-04-17
 */

#pragma once

#include "../RenderTarget.h"
#include "VkConvert.h"
#include "VKTexture.h"
#include "VKCommandList.h"

#include <memory>

namespace mulan::engine {

class VKRenderTarget : public RenderTarget {
public:
    VKRenderTarget(const RenderTargetDesc& desc,
                   vk::Device device, VmaAllocator allocator);
    ~VKRenderTarget();

    const RenderTargetDesc& desc() const override { return m_desc; }

    Texture* colorTexture() override { return m_colorTexture.get(); }
    Texture* depthTexture() override { return m_depthTexture.get(); }

    void resize(uint32_t width, uint32_t height) override;

    // --- Vulkan 特有 ---
    vk::RenderPass  renderPass()  const { return m_renderPass; }
    vk::Framebuffer framebuffer() const { return m_framebuffer; }

private:
    void createResources();
    void cleanup();

    RenderTargetDesc m_desc;
    vk::Device       m_device;
    VmaAllocator     m_allocator;

    std::unique_ptr<VKTexture> m_colorTexture;
    std::unique_ptr<VKTexture> m_depthTexture;

    vk::RenderPass   m_renderPass  = nullptr;
    vk::Framebuffer  m_framebuffer = nullptr;
};

} // namespace mulan::Engine
