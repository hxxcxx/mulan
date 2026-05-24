/**
 * @file DX12Texture.h
 * @brief D3D12 纹理实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../Texture.h"
#include "DX12Common.h"

namespace MulanGeo::engine {

class DX12Texture final : public Texture {
public:
    DX12Texture(const TextureDesc& desc, ID3D12Device* device,
                D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
    DX12Texture(const TextureDesc& desc, ID3D12Resource* existingResource,
                D3D12_RESOURCE_STATES initialState);
    ~DX12Texture();

    const TextureDesc& desc() const override { return m_desc; }

    ID3D12Resource* resource() const { return m_resource.Get(); }
    D3D12_RESOURCE_STATES state() const { return m_state; }
    void setState(D3D12_RESOURCE_STATES s) { m_state = s; }

    // RTV/DSV 句柄（由 SwapChain/RenderTarget 在创建时设置）
    void setRTV(D3D12_CPU_DESCRIPTOR_HANDLE handle) { m_rtv = handle; m_hasRTV = true; }
    void setDSV(D3D12_CPU_DESCRIPTOR_HANDLE handle) { m_dsv = handle; m_hasDSV = true; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return m_rtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return m_dsv; }
    bool hasRTV() const { return m_hasRTV; }
    bool hasDSV() const { return m_hasDSV; }

private:
    TextureDesc               m_desc;
    ComPtr<ID3D12Resource>    m_resource;
    D3D12_RESOURCE_STATES     m_state;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsv = {};
    bool m_hasRTV = false;
    bool m_hasDSV = false;
};

} // namespace MulanGeo::Engine
