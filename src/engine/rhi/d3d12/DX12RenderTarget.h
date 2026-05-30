/**
 * @file DX12RenderTarget.h
 * @brief D3D12 离屏渲染目标实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../RenderTarget.h"
#include "DX12Common.h"
#include "DX12Texture.h"
#include "DX12DescriptorAllocator.h"

#include <memory>

namespace mulan::engine {

class DX12RenderTarget final : public RenderTarget {
public:
    DX12RenderTarget(const RenderTargetDesc& desc, ID3D12Device* device);
    ~DX12RenderTarget();

    const RenderTargetDesc& desc() const override { return m_desc; }
    Texture* colorTexture() override { return m_colorTexture.get(); }
    Texture* depthTexture() override { return m_depthTexture.get(); }

    void resize(uint32_t width, uint32_t height) override;

private:
    void createResources();

    RenderTargetDesc                       m_desc;
    ID3D12Device*                          m_device;
    std::unique_ptr<DX12Texture>           m_colorTexture;
    std::unique_ptr<DX12Texture>           m_depthTexture;

    std::unique_ptr<DX12DescriptorAllocator> m_rtvHeap;
    std::unique_ptr<DX12DescriptorAllocator> m_dsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE             m_rtvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE             m_dsvHandle = {};
};

} // namespace mulan::engine
