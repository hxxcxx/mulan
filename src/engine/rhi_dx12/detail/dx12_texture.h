/**
 * @file dx12_texture.h
 * @brief D3D12 纹理实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/texture.h"
#include "dx12_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>

namespace mulan::engine {

class DX12Texture final : public Texture {
public:
    /// 创建常规纹理。失败返回 TextureCreateFailed。
    static Result<std::unique_ptr<DX12Texture>> create(
            const TextureDesc& desc, ID3D12Device* device,
            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    /// Swapchain backbuffer / 现有资源包装构造（不可失败，保持 public）。
    DX12Texture(const TextureDesc& desc, ID3D12Resource* existingResource, D3D12_RESOURCE_STATES initialState);
    ~DX12Texture();

    const TextureDesc& desc() const override { return desc_; }

    ID3D12Resource* resource() const { return resource_.Get(); }
    D3D12_RESOURCE_STATES state() const { return state_; }
    void setState(D3D12_RESOURCE_STATES s) { state_ = s; }

    // RTV/DSV/SRV 句柄
    void setRTV(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        rtv_ = handle;
        has_rtv_ = true;
    }
    void setDSV(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        dsv_ = handle;
        has_dsv_ = true;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return rtv_; }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv() const { return dsv_; }
    D3D12_CPU_DESCRIPTOR_HANDLE srv() const { return srv_; }
    bool hasRTV() const { return has_rtv_; }
    bool hasDSV() const { return has_dsv_; }
    bool hasSRV() const { return has_srv_; }

private:
    DX12Texture(const TextureDesc& desc, D3D12_RESOURCE_STATES initialState) : desc_(desc), state_(initialState) {}

    [[nodiscard]] Result<void> initialize(ID3D12Device* device);
    [[nodiscard]] Result<void> createSRVIfNeeded(ID3D12Device* device);

    TextureDesc desc_;
    ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES state_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_ = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_ = {};
    D3D12_CPU_DESCRIPTOR_HANDLE srv_ = {};
    ComPtr<ID3D12DescriptorHeap> srv_heap_;  // 仅用于持有 SRV
    bool has_rtv_ = false;
    bool has_dsv_ = false;
    bool has_srv_ = false;
};

}  // namespace mulan::engine
