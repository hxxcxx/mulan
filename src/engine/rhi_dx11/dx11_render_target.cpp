#include "detail/dx11_render_target.h"
#include "../rhi/engine_error_code.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace mulan::engine {

DX11RenderTarget::DX11RenderTarget(const RenderTargetDesc& desc, ID3D11Device* device)
    : m_desc(desc), m_device(device) {
    createResources();
}

bool DX11RenderTarget::createResources() {
    if (!m_device || m_desc.width == 0 || m_desc.height == 0)
        return false;

    m_desc.sampleCount = m_desc.sampleCount > 1 ? m_desc.sampleCount : 1;

    // colorTexture() 始终返回单采样纹理，便于后续采样与 readback。
    auto colorDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.colorFormat, "DX11OffscreenColor");
    auto colorTexture = std::make_unique<DX11Texture>(colorDesc, m_device);
    if (!colorTexture->isValid())
        return false;

    std::unique_ptr<DX11Texture> msaaColorTexture;
    if (m_desc.sampleCount > 1) {
        auto msaaColorDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.colorFormat,
                                                       "DX11OffscreenMSAAColor", m_desc.sampleCount);
        msaaColorDesc.usage = TextureUsageFlags::RenderTarget;
        msaaColorTexture = std::make_unique<DX11Texture>(msaaColorDesc, m_device);
        if (!msaaColorTexture->isValid())
            return false;
    }

    std::unique_ptr<DX11Texture> depthTexture;
    if (m_desc.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height, m_desc.depthFormat,
                                                   "DX11OffscreenDepth", m_desc.sampleCount);
        // 当前渲染路径不采样深度，避免为 typed depth 资源创建不必要的 SRV。
        depthDesc.usage = TextureUsageFlags::DepthStencil;
        depthTexture = std::make_unique<DX11Texture>(depthDesc, m_device);
        if (!depthTexture->isValid())
            return false;
    }

    // 全部附件成功创建后再替换旧资源，使 resize 失败时仍可继续使用上一帧目标。
    m_colorTexture = std::move(colorTexture);
    m_msaaColorTexture = std::move(msaaColorTexture);
    m_depthTexture = std::move(depthTexture);
    return true;
}

core::Result<void> DX11RenderTarget::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "DX11 render target size must be non-zero"));

    const RenderTargetDesc previousDesc = m_desc;
    m_desc.width = width;
    m_desc.height = height;
    if (!createResources()) {
        m_desc = previousDesc;
        return std::unexpected(makeError(EngineErrorCode::ResizeFailed, "DX11 render target resize failed"));
    }
    return {};
}

RenderPassBeginInfo DX11RenderTarget::renderPassBeginInfo() {
    RenderPassBeginInfo info;
    Texture* color = m_msaaColorTexture ? static_cast<Texture*>(m_msaaColorTexture.get()) : m_colorTexture.get();
    if (color) {
        info.colorAttachments[0].target = color;
        info.colorAttachments[0].resolveTarget = m_msaaColorTexture ? m_colorTexture.get() : nullptr;
        info.colorAttachments[0].loadAction = LoadAction::Clear;
        info.colorAttachments[0].storeAction = m_msaaColorTexture ? StoreAction::DontCare : StoreAction::Store;
        info.colorCount = 1;
    }
    if (m_depthTexture) {
        info.depthAttachment.target = m_depthTexture.get();
        info.depthAttachment.loadAction = LoadAction::Clear;
        info.depthAttachment.storeAction = StoreAction::DontCare;
    }
    std::copy(std::begin(m_desc.clearColor), std::end(m_desc.clearColor), info.clearColor);
    info.clearDepth = m_desc.clearDepth;
    info.width = m_desc.width;
    info.height = m_desc.height;
    return info;
}

}  // namespace mulan::engine
