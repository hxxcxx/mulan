/**
 * @file dx11_pipeline_state.h
 * @brief D3D11 管线状态实现
 * @author zmb
 * @date 2026-04-19
 *
 * D3D11 没有 monolithic PSO。我们在 finalize() 时创建各独立状态对象
 * （InputLayout / RasterizerState / BlendState / DepthStencilState），
 * 并在 bind 时一次性设置。
 */
#pragma once

#include "../../rhi/pipeline_state.h"
#include "dx11_common.h"
#include "dx11_convert.h"

#include <vector>

namespace mulan::engine {

class DX11PipelineState final : public PipelineState {
public:
    DX11PipelineState(const GraphicsPipelineDesc& desc, ID3D11Device* device);
    ~DX11PipelineState() = default;

    const GraphicsPipelineDesc& desc() const override { return m_desc; }

    ID3D11InputLayout* inputLayout() const { return m_inputLayout.Get(); }
    ID3D11RasterizerState* rasterizerState() const { return m_rasterizer.Get(); }
    ID3D11BlendState* blendState() const { return m_blend.Get(); }
    ID3D11DepthStencilState* depthStencilState() const { return m_depthStencil.Get(); }
    ID3D11VertexShader* vertexShader() const { return m_vertexShader.Get(); }
    ID3D11PixelShader* pixelShader() const { return m_pixelShader.Get(); }
    ID3D11GeometryShader* geometryShader() const { return m_geometryShader.Get(); }
    bool isValid() const { return m_initialized; }

    uint32_t stride() const { return m_desc.vertexLayout.stride(); }

private:
    void createInputLayout();
    void createRasterizerState();
    void createBlendState();
    void createDepthStencilState();

    GraphicsPipelineDesc m_desc;
    ID3D11Device* m_device;

    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11BlendState> m_blend;
    ComPtr<ID3D11DepthStencilState> m_depthStencil;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11GeometryShader> m_geometryShader;
    bool m_initialized = false;
};

}  // namespace mulan::engine
