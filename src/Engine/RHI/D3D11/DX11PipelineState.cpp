/**
 * @file DX11PipelineState.cpp
 * @brief D3D11 管线状态实现
 * @author zmb
 * @date 2026-04-19
 */
#include "DX11PipelineState.h"
#include "DX11Shader.h"

namespace MulanGeo::engine
{

DX11PipelineState::DX11PipelineState(const GraphicsPipelineDesc& desc,
                                     ID3D11Device* device)
    : m_desc(desc)
    , m_device(device)
{
    createInputLayout();
    createRasterizerState();
    createBlendState();
    createDepthStencilState();
}

void DX11PipelineState::createInputLayout()
{
    auto* vsShader = static_cast<DX11Shader*>(m_desc.vs);
    if (!vsShader || vsShader->byteCodeSize() == 0) return;

    const auto& layout = m_desc.vertexLayout;
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    elements.reserve(layout.attrCount());

    for (uint32_t i = 0; i < layout.attrCount(); ++i)
    {
        const auto& attr = layout[i];

        const char* semanticName = "POSITION";
        UINT semIdx = 0;
        switch (attr.semantic)
        {
            case VertexSemantic::Position:  semanticName = "POSITION"; semIdx = 0; break;
            case VertexSemantic::Normal:    semanticName = "NORMAL";   semIdx = 0; break;
            case VertexSemantic::Color0:    semanticName = "COLOR";    semIdx = 0; break;
            case VertexSemantic::Color1:    semanticName = "COLOR";    semIdx = 1; break;
            case VertexSemantic::TexCoord0: semanticName = "TEXCOORD"; semIdx = 0; break;
            case VertexSemantic::TexCoord1: semanticName = "TEXCOORD"; semIdx = 1; break;
            case VertexSemantic::Tangent:   semanticName = "TANGENT";  semIdx = 0; break;
            case VertexSemantic::Bitangent: semanticName = "BINORMAL"; semIdx = 0; break;
            default:                        semanticName = "TEXCOORD"; semIdx = 2; break;
        }

        D3D11_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName         = semanticName;
        elem.SemanticIndex        = semIdx;
        elem.Format               = toDXGIFormat11(attr.format);
        elem.InputSlot            = 0;
        elem.AlignedByteOffset    = attr.offset;
        elem.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = 0;

        elements.push_back(elem);
    }

    HRESULT hr = m_device->CreateInputLayout(
        elements.data(),
        static_cast<UINT>(elements.size()),
        vsShader->byteCodeData(),
        vsShader->byteCodeSize(),
        &m_inputLayout);
    DX11_CHECK(hr);
}

void DX11PipelineState::createRasterizerState()
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode              = toDX11FillMode(m_desc.fillMode);
    rd.CullMode              = toDX11CullMode(m_desc.cullMode);
    rd.FrontCounterClockwise = (m_desc.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
    rd.DepthBias             = 0;
    rd.DepthBiasClamp        = 0.0f;
    rd.SlopeScaledDepthBias  = 0.0f;
    rd.DepthClipEnable       = TRUE;
    rd.ScissorEnable         = TRUE;
    rd.MultisampleEnable     = FALSE;
    rd.AntialiasedLineEnable = FALSE;

    HRESULT hr = m_device->CreateRasterizerState(&rd, &m_rasterizer);
    DX11_CHECK(hr);
}

void DX11PipelineState::createBlendState()
{
    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable  = m_desc.blend.alphaToCoverage ? TRUE : FALSE;
    bd.IndependentBlendEnable = m_desc.blend.independentBlend ? TRUE : FALSE;

    for (int i = 0; i < 8; ++i)
    {
        const auto& src = m_desc.blend.renderTargets[i];
        auto& dst = bd.RenderTarget[i];
        dst.BlendEnable           = src.blendEnable ? TRUE : FALSE;
        dst.SrcBlend              = toDX11Blend(src.srcBlend);
        dst.DestBlend             = toDX11Blend(src.dstBlend);
        dst.BlendOp               = toDX11BlendOp(src.blendOp);
        dst.SrcBlendAlpha         = toDX11Blend(src.srcBlendAlpha);
        dst.DestBlendAlpha        = toDX11Blend(src.dstBlendAlpha);
        dst.BlendOpAlpha          = toDX11BlendOp(src.blendOpAlpha);
        dst.RenderTargetWriteMask = src.writeMask;
    }

    HRESULT hr = m_device->CreateBlendState(&bd, &m_blend);
    DX11_CHECK(hr);
}

void DX11PipelineState::createDepthStencilState()
{
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable    = m_desc.depthStencil.depthEnable ? TRUE : FALSE;
    dd.DepthWriteMask = m_desc.depthStencil.depthWrite
                            ? D3D11_DEPTH_WRITE_MASK_ALL
                            : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc      = toDX11CompareFunc(m_desc.depthStencil.depthFunc);
    dd.StencilEnable  = m_desc.depthStencil.stencilEnable ? TRUE : FALSE;
    dd.StencilReadMask  = m_desc.depthStencil.stencilReadMask;
    dd.StencilWriteMask = m_desc.depthStencil.stencilWriteMask;

    // Front face stencil ops
    dd.FrontFace.StencilFailOp      = toDX11StencilOp(m_desc.depthStencil.frontFace.failOp);
    dd.FrontFace.StencilDepthFailOp = toDX11StencilOp(m_desc.depthStencil.frontFace.depthFailOp);
    dd.FrontFace.StencilPassOp      = toDX11StencilOp(m_desc.depthStencil.frontFace.passOp);
    dd.FrontFace.StencilFunc        = toDX11CompareFunc(m_desc.depthStencil.frontFace.func);

    // Back face stencil ops
    dd.BackFace.StencilFailOp       = toDX11StencilOp(m_desc.depthStencil.backFace.failOp);
    dd.BackFace.StencilDepthFailOp  = toDX11StencilOp(m_desc.depthStencil.backFace.depthFailOp);
    dd.BackFace.StencilPassOp       = toDX11StencilOp(m_desc.depthStencil.backFace.passOp);
    dd.BackFace.StencilFunc         = toDX11CompareFunc(m_desc.depthStencil.backFace.func);

    HRESULT hr = m_device->CreateDepthStencilState(&dd, &m_depthStencil);
    DX11_CHECK(hr);
}

} // namespace MulanGeo::Engine
