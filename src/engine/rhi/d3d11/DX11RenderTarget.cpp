/**
 * @file DX11RenderTarget.cpp
 * @brief D3D11 离屏渲染目标实现
 * @author zmb
 * @date 2026-04-19
 */
#include "DX11RenderTarget.h"
#include "DX11CommandList.h"
#include "DX11Convert.h"

namespace mulan::engine
{

DX11RenderTarget::DX11RenderTarget(const RenderTargetDesc& desc,
                                   ID3D11Device* device)
    : m_desc(desc)
    , m_device(device)
{
    createResources();
}

void DX11RenderTarget::createResources()
{
    // Color texture
    auto colorDesc = TextureDesc::renderTarget(m_desc.width, m_desc.height, m_desc.colorFormat);
    m_colorTexture = std::make_unique<DX11Texture>(colorDesc, m_device);

    if (m_desc.hasDepth)
    {
        auto depthDesc = TextureDesc::depthStencil(m_desc.width, m_desc.height, m_desc.depthFormat);
        m_depthTexture = std::make_unique<DX11Texture>(depthDesc, m_device);
    }
}

void DX11RenderTarget::resize(uint32_t width, uint32_t height)
{
    m_desc.width  = width;
    m_desc.height = height;
    m_colorTexture.reset();
    m_depthTexture.reset();
    createResources();
}

} // namespace mulan::engine
