#include "detail/dx11_texture.h"
#include "detail/dx11_convert.h"

#include <stdexcept>

namespace mulan::engine {

DX11Texture::DX11Texture(const TextureDesc& desc, ID3D11Device* device) : m_desc(desc) {
    if (!device)
        throw std::invalid_argument("DX11Texture requires a valid device");
    if (desc.dimension != TextureDimension::Texture2D)
        throw std::invalid_argument("DX11Texture currently supports Texture2D only");
    if (desc.width == 0 || desc.height == 0 || desc.mipLevels == 0 || desc.arraySize == 0 || desc.sampleCount == 0)
        throw std::invalid_argument("DX11Texture dimensions, mipLevels, arraySize and sampleCount must be non-zero");
    if (desc.arraySize != 1)
        throw std::invalid_argument("DX11Texture currently supports one Texture2D array slice only");
    if (desc.sampleCount > 1 && desc.mipLevels != 1)
        throw std::invalid_argument("multisampled DX11Texture requires one mip level");

    const bool isDepth = desc.usage & TextureUsageFlags::DepthStencil;
    const bool isDepthFormat = isDepthFormat11(desc.format);
    if (isDepth && !isDepthFormat)
        throw std::invalid_argument("DX11 depth-stencil texture requires a depth format");
    if (!isDepth && isDepthFormat)
        throw std::invalid_argument("DX11 color texture cannot use a depth format");

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

    // 深度纹理同时作为 SRV 使用时必须以 typeless 资源格式创建。
    const bool needsSRV = desc.usage & TextureUsageFlags::ShaderResource;
    if (isDepth && needsSRV) {
        td.Format = toTypelessFormat11(desc.format);
    } else if (isDepth) {
        td.Format = toDSVFormat11(desc.format);
    } else {
        td.Format = toDXGIFormat11(desc.format);
    }
    if (td.Format == DXGI_FORMAT_UNKNOWN)
        throw std::invalid_argument("DX11Texture format is not supported by the D3D11 backend");

    if (!checkDX11(device->CreateTexture2D(&td, nullptr, &m_texture), "ID3D11Device::CreateTexture2D"))
        return;

    // Auto-create views
    if (desc.usage & TextureUsageFlags::RenderTarget)
        createRTV(device);
    if (desc.usage & TextureUsageFlags::DepthStencil)
        createDSV(device);
    if (desc.usage & TextureUsageFlags::ShaderResource)
        createSRV(device);
}

DX11Texture::DX11Texture(const TextureDesc& desc, ID3D11Texture2D* existing) : m_desc(desc), m_texture(existing) {
    if (!existing)
        throw std::invalid_argument("DX11Texture cannot wrap a null D3D11 texture");
}

void DX11Texture::createRTV(ID3D11Device* device, DXGI_FORMAT fmt) {
    if (!device || !m_texture)
        throw std::invalid_argument("DX11Texture::createRTV requires a valid texture and device");

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = (fmt != DXGI_FORMAT_UNKNOWN) ? fmt : toDXGIFormat11(m_desc.format);
    if (m_desc.sampleCount > 1) {
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    } else {
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
    }
    if (rtvDesc.Format == DXGI_FORMAT_UNKNOWN)
        throw std::invalid_argument("DX11Texture cannot create RTV for an unknown format");

    if (!checkDX11(device->CreateRenderTargetView(m_texture.Get(), &rtvDesc, &m_rtv),
                   "ID3D11Device::CreateRenderTargetView"))
        return;
}

void DX11Texture::createDSV(ID3D11Device* device, DXGI_FORMAT fmt) {
    if (!device || !m_texture)
        throw std::invalid_argument("DX11Texture::createDSV requires a valid texture and device");

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = (fmt != DXGI_FORMAT_UNKNOWN) ? fmt : toDSVFormat11(m_desc.format);
    if (m_desc.sampleCount > 1) {
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    } else {
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
    }
    if (dsvDesc.Format == DXGI_FORMAT_UNKNOWN)
        throw std::invalid_argument("DX11Texture cannot create DSV for an unknown format");

    if (!checkDX11(device->CreateDepthStencilView(m_texture.Get(), &dsvDesc, &m_dsv),
                   "ID3D11Device::CreateDepthStencilView"))
        return;
}

void DX11Texture::createSRV(ID3D11Device* device, DXGI_FORMAT fmt) {
    if (!device || !m_texture)
        throw std::invalid_argument("DX11Texture::createSRV requires a valid texture and device");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    if (fmt != DXGI_FORMAT_UNKNOWN) {
        srvDesc.Format = fmt;
    } else {
        srvDesc.Format = isDepthFormat11(m_desc.format) ? toSRVFormat11(m_desc.format) : toDXGIFormat11(m_desc.format);
    }
    if (m_desc.sampleCount > 1) {
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    } else {
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = m_desc.mipLevels;
    }
    if (srvDesc.Format == DXGI_FORMAT_UNKNOWN)
        throw std::invalid_argument("DX11Texture cannot create SRV for an unknown format");

    if (!checkDX11(device->CreateShaderResourceView(m_texture.Get(), &srvDesc, &m_srv),
                   "ID3D11Device::CreateShaderResourceView"))
        return;
}

}  // namespace mulan::engine
