#include "detail/dx11_pipeline_state.h"
#include "detail/dx11_shader.h"

namespace mulan::engine {

using graphics::VertexSemantic;

DX11PipelineState::DX11PipelineState(const GraphicsPipelineDesc& desc, ID3D11Device* device)
    : m_desc(desc), m_device(device) {
    auto* vsShader = desc.vs ? dynamic_cast<DX11Shader*>(desc.vs) : nullptr;
    if (!device || !vsShader || !vsShader->vsShader() || vsShader->byteCodeSize() == 0) {
        LOG_ERROR("[DX11] Pipeline initialization rejected: invalid device or vertex shader");
        return;
    }
    m_vertexShader = vsShader->vsShader();
    if (desc.ps)
        m_pixelShader = static_cast<DX11Shader*>(desc.ps)->psShader();
    if (desc.gs)
        m_geometryShader = static_cast<DX11Shader*>(desc.gs)->gsShader();
    createInputLayout();
    createRasterizerState();
    createBlendState();
    createDepthStencilState();
    m_initialized = (m_desc.vertexLayout.empty() || m_inputLayout) && m_rasterizer && m_blend && m_depthStencil;
    m_desc.discardShaderReferences();
}

DX11PipelineState::~DX11PipelineState() {
    waitForLastUseBeforeDestruction();
}

void DX11PipelineState::createInputLayout() {
    if (!m_device || !m_desc.vs) {
        LOG_ERROR("[DX11] Pipeline initialization rejected: missing device or vertex shader");
        return;
    }

    auto* vsShader = dynamic_cast<DX11Shader*>(m_desc.vs);
    if (!vsShader || !vsShader->vsShader() || vsShader->byteCodeSize() == 0) {
        LOG_ERROR("[DX11] Pipeline initialization rejected: invalid vertex shader");
        return;
    }

    const auto& layout = m_desc.vertexLayout;
    if (layout.empty())
        return;

    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    elements.reserve(layout.attrCount());

    for (uint32_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];

        const char* semanticName = "POSITION";
        UINT semIdx = 0;
        switch (attr.semantic) {
        case VertexSemantic::Position:
            semanticName = "POSITION";
            semIdx = 0;
            break;
        case VertexSemantic::Normal:
            semanticName = "NORMAL";
            semIdx = 0;
            break;
        case VertexSemantic::Color0:
            semanticName = "COLOR";
            semIdx = 0;
            break;
        case VertexSemantic::Color1:
            semanticName = "COLOR";
            semIdx = 1;
            break;
        case VertexSemantic::TexCoord0:
            semanticName = "TEXCOORD";
            semIdx = 0;
            break;
        case VertexSemantic::TexCoord1:
            semanticName = "TEXCOORD";
            semIdx = 1;
            break;
        case VertexSemantic::Tangent:
            semanticName = "TANGENT";
            semIdx = 0;
            break;
        case VertexSemantic::Bitangent:
            semanticName = "BINORMAL";
            semIdx = 0;
            break;
        default:
            semanticName = "TEXCOORD";
            semIdx = 2;
            break;
        }

        D3D11_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName = semanticName;
        elem.SemanticIndex = semIdx;
        elem.Format = toDXGIFormat11(attr.format);
        if (elem.Format == DXGI_FORMAT_UNKNOWN || attr.bufferSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
            LOG_ERROR("[DX11] Pipeline initialization rejected: invalid vertex layout");
            return;
        }
        elem.InputSlot = attr.bufferSlot;
        elem.AlignedByteOffset = attr.offset;
        elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = 0;

        elements.push_back(elem);
    }

    if (!checkDX11(m_device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()),
                                               vsShader->byteCodeData(), vsShader->byteCodeSize(), &m_inputLayout),
                   "ID3D11Device::CreateInputLayout"))
        return;
}

void DX11PipelineState::createRasterizerState() {
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = toDX11FillMode(m_desc.fillMode);
    rd.CullMode = toDX11CullMode(m_desc.cullMode);
    rd.FrontCounterClockwise = (m_desc.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
    rd.DepthBias = static_cast<INT>(m_desc.depthStencil.depthBias);
    rd.DepthBiasClamp = m_desc.depthStencil.depthBiasClamp;
    rd.SlopeScaledDepthBias = m_desc.depthStencil.slopeScaledDepthBias;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    rd.MultisampleEnable = m_desc.sampleCount > 1 ? TRUE : FALSE;
    rd.AntialiasedLineEnable = FALSE;

    HRESULT hr = m_device->CreateRasterizerState(&rd, &m_rasterizer);
    if (!checkDX11(hr, "ID3D11Device::CreateRasterizerState"))
        return;
}

void DX11PipelineState::createBlendState() {
    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable = m_desc.blend.alphaToCoverage ? TRUE : FALSE;
    bd.IndependentBlendEnable = m_desc.blend.independentBlend ? TRUE : FALSE;

    for (int i = 0; i < 8; ++i) {
        const auto& src = m_desc.blend.renderTargets[i];
        auto& dst = bd.RenderTarget[i];
        dst.BlendEnable = src.blendEnable ? TRUE : FALSE;
        dst.SrcBlend = toDX11Blend(src.srcBlend);
        dst.DestBlend = toDX11Blend(src.dstBlend);
        dst.BlendOp = toDX11BlendOp(src.blendOp);
        dst.SrcBlendAlpha = toDX11Blend(src.srcBlendAlpha);
        dst.DestBlendAlpha = toDX11Blend(src.dstBlendAlpha);
        dst.BlendOpAlpha = toDX11BlendOp(src.blendOpAlpha);
        dst.RenderTargetWriteMask = src.writeMask;
    }

    HRESULT hr = m_device->CreateBlendState(&bd, &m_blend);
    if (!checkDX11(hr, "ID3D11Device::CreateBlendState"))
        return;
}

void DX11PipelineState::createDepthStencilState() {
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = m_desc.depthStencil.depthEnable ? TRUE : FALSE;
    dd.DepthWriteMask = m_desc.depthStencil.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = toDX11CompareFunc(m_desc.depthStencil.depthFunc);
    dd.StencilEnable = m_desc.depthStencil.stencilEnable ? TRUE : FALSE;
    dd.StencilReadMask = m_desc.depthStencil.stencilReadMask;
    dd.StencilWriteMask = m_desc.depthStencil.stencilWriteMask;

    // Front face stencil ops
    dd.FrontFace.StencilFailOp = toDX11StencilOp(m_desc.depthStencil.frontFace.failOp);
    dd.FrontFace.StencilDepthFailOp = toDX11StencilOp(m_desc.depthStencil.frontFace.depthFailOp);
    dd.FrontFace.StencilPassOp = toDX11StencilOp(m_desc.depthStencil.frontFace.passOp);
    dd.FrontFace.StencilFunc = toDX11CompareFunc(m_desc.depthStencil.frontFace.func);

    // Back face stencil ops
    dd.BackFace.StencilFailOp = toDX11StencilOp(m_desc.depthStencil.backFace.failOp);
    dd.BackFace.StencilDepthFailOp = toDX11StencilOp(m_desc.depthStencil.backFace.depthFailOp);
    dd.BackFace.StencilPassOp = toDX11StencilOp(m_desc.depthStencil.backFace.passOp);
    dd.BackFace.StencilFunc = toDX11CompareFunc(m_desc.depthStencil.backFace.func);

    HRESULT hr = m_device->CreateDepthStencilState(&dd, &m_depthStencil);
    if (!checkDX11(hr, "ID3D11Device::CreateDepthStencilState"))
        return;
}

}  // namespace mulan::engine
