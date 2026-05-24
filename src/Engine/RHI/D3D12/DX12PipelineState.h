/**
 * @file DX12PipelineState.h
 * @brief D3D12 管线状态 + Root Signature 实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../PipelineState.h"
#include "DX12Common.h"
#include "DX12Convert.h"

#include <vector>

namespace MulanGeo::engine {

class DX12PipelineState final : public PipelineState {
public:
    DX12PipelineState(const GraphicsPipelineDesc& desc, ID3D12Device* device);
    ~DX12PipelineState();

    const GraphicsPipelineDesc& desc() const override { return m_desc; }

    ID3D12PipelineState* pipeline() const { return m_pipeline.Get(); }
    ID3D12RootSignature* rootSignature() const { return m_rootSignature.Get(); }

private:
    void build(DXGI_FORMAT rtFormat, DXGI_FORMAT dsFormat);
    void createRootSignature();
    D3D12_INPUT_LAYOUT_DESC buildInputLayout();

    GraphicsPipelineDesc         m_desc;
    ID3D12Device*                m_device;
    ComPtr<ID3D12RootSignature>  m_rootSignature;
    ComPtr<ID3D12PipelineState>  m_pipeline;

    std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputElements;
};

} // namespace MulanGeo::Engine
