/**
 * @file dx11_texture.h
 * @brief D3D11 纹理实现
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/texture.h"
#include "dx11_common.h"

namespace mulan::engine {

class DX11Texture final : public Texture {
public:
    DX11Texture(const TextureDesc& desc, ID3D11Device* device);
    /// 从已有资源包装（SwapChain back buffer）
    DX11Texture(const TextureDesc& desc, ID3D11Texture2D* existing);
    ~DX11Texture() = default;

    const TextureDesc& desc() const override { return m_desc; }

    ID3D11Texture2D* resource() const { return m_texture.Get(); }
    ID3D11RenderTargetView* rtv() const { return m_rtv.Get(); }
    ID3D11DepthStencilView* dsv() const { return m_dsv.Get(); }
    ID3D11ShaderResourceView* srv() const { return m_srv.Get(); }

    void createRTV(ID3D11Device* device, DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);
    void createDSV(ID3D11Device* device, DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);
    void createSRV(ID3D11Device* device, DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN);

private:
    TextureDesc m_desc;
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11DepthStencilView> m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
};

}  // namespace mulan::engine
