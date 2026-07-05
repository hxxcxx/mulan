/**
 * @file dx12_pipeline_state.h
 * @brief D3D12 管线状态 + Root Signature 实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../pipeline_state.h"
#include "dx12_common.h"
#include "dx12_convert.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <vector>

namespace mulan::engine {

class DX12PipelineState final : public PipelineState {
public:
    /// 创建 DX12PipelineState。失败返回 PipelineCreateFailed。
    static core::Result<std::unique_ptr<DX12PipelineState>>
        create(const GraphicsPipelineDesc& desc, ID3D12Device* device);
    ~DX12PipelineState();

    const GraphicsPipelineDesc& desc() const override { return desc_; }

    ID3D12PipelineState* pipeline() const { return pipeline_.Get(); }
    ID3D12RootSignature* rootSignature() const { return root_signature_.Get(); }

private:
    DX12PipelineState(const GraphicsPipelineDesc& desc, ID3D12Device* device);

    void build(DXGI_FORMAT rtFormat, DXGI_FORMAT dsFormat);
    void createRootSignature();
    D3D12_INPUT_LAYOUT_DESC buildInputLayout();

    GraphicsPipelineDesc         desc_;
    ID3D12Device*                device_;
    ComPtr<ID3D12RootSignature>  root_signature_;
    ComPtr<ID3D12PipelineState>  pipeline_;

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements_;
};

} // namespace mulan::engine
