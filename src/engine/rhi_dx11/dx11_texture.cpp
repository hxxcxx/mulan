#include "detail/dx11_texture.h"
#include "detail/dx11_convert.h"

namespace mulan::engine {

DX11Texture::DX11Texture(const TextureDesc& desc, ID3D11Device* device) : m_desc(desc) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = desc.mipLevels;
    td.ArraySize = desc.arraySize;
    td.SampleDesc.Count = desc.sampleCount;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.CPUAccessFlags = 0;
    td.MiscFlags = 0;

    // Bind flags
    td.BindFlags = 0;
    if (desc.usage & TextureUsageFlags::ShaderResource)
        td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (desc.usage & TextureUsageFlags::RenderTarget)
        td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (desc.usage & TextureUsageFlags::DepthStencil)
        td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (desc.usage & TextureUsageFlags::UnorderedAccess)
        td.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    // Depth textures need typeless format if also used as SRV
    bool isDepth = (desc.usage & TextureUsageFlags::DepthStencil);
    bool needsSRV = (desc.usage & TextureUsageFlags::ShaderResource);
    if (isDepth && needsSRV) {
        td.Format = toTypelessFormat11(desc.format);
    } else if (isDepth) {
        td.Format = toDSVFormat11(desc.format);
    } else {
        td.Format = toDXGIFormat11(desc.format);
    }

    HRESULT hr = device->CreateTexture2D(&td, nullptr, &m_texture);
    DX11_CHECK(hr);

    // Auto-create views
    if (desc.usage & TextureUsageFlags::RenderTarget)
        createRTV(device);
    if (desc.usage & TextureUsageFlags::DepthStencil)
        createDSV(device);
    if (desc.usage & TextureUsageFlags::ShaderResource)
        createSRV(device);
}

DX11Texture::DX11Texture(const TextureDesc& desc, ID3D11Texture2D* existing) : m_desc(desc), m_texture(existing) {
}

void DX11Texture::createRTV(ID3D11Device* device, DXGI_FORMAT fmt) {
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = (fmt != DXGI_FORMAT_UNKNOWN) ? fmt : toDXGIFormat11(m_desc.format);
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    HRESULT hr = device->CreateRenderTargetView(m_texture.Get(), &rtvDesc, &m_rtv);
    DX11_CHECK(hr);
}

void DX11Texture::createDSV(ID3D11Device* device, DXGI_FORMAT fmt) {
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = (fmt != DXGI_FORMAT_UNKNOWN) ? fmt : toDSVFormat11(m_desc.format);
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    HRESULT hr = device->CreateDepthStencilView(m_texture.Get(), &dsvDesc, &m_dsv);
    DX11_CHECK(hr);
}

void DX11Texture::createSRV(ID3D11Device* device, DXGI_FORMAT fmt) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    if (fmt != DXGI_FORMAT_UNKNOWN) {
        srvDesc.Format = fmt;
    } else {
        // For depth textures used as SRV, pick a readable format
        bool isDepth = (m_desc.usage & TextureUsageFlags::DepthStencil);
        if (isDepth) {
            switch (m_desc.format) {
            case TextureFormat::D16_UNorm: srvDesc.Format = DXGI_FORMAT_R16_UNORM; break;
            case TextureFormat::D24_UNorm_S8_UInt: srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
            case TextureFormat::D32_Float: srvDesc.Format = DXGI_FORMAT_R32_FLOAT; break;
            case TextureFormat::D32_Float_S8X24_UInt: srvDesc.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
            default: srvDesc.Format = toDXGIFormat11(m_desc.format); break;
            }
        } else {
            srvDesc.Format = toDXGIFormat11(m_desc.format);
        }
    }
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = m_desc.mipLevels;

    HRESULT hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_srv);
    DX11_CHECK(hr);
}

}  // namespace mulan::engine
