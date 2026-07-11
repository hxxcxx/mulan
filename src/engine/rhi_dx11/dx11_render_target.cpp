#include "detail/dx11_render_target.h"
#include "detail/dx11_command_list.h"
#include "detail/dx11_convert.h"

namespace mulan::engine {

DX11RenderTarget::DX11RenderTarget(const RenderTargetDesc& desc, ID3D11Device* device)
    : m_desc(desc), m_device(device) {
    createResources();
}

void DX11RenderTarget::createResources() {
    // Color texture
    auto colorDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.colorFormat);
    m_colorTexture = std::make_unique<DX11Texture>(colorDesc, m_device);

    if (m_desc.hasDepth) {
        auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height, m_desc.depthFormat);
        m_depthTexture = std::make_unique<DX11Texture>(depthDesc, m_device);
    }
}

void DX11RenderTarget::resize(uint32_t width, uint32_t height) {
    m_desc.width = width;
    m_desc.height = height;
    m_colorTexture.reset();
    m_depthTexture.reset();
    createResources();
}

}  // namespace mulan::engine
