/**
 * @file DX12PipelineState.cpp
 * @brief D3D12 管线状态实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12PipelineState.h"
#include "DX12Shader.h"
#include <string>

namespace {
D3D12_PRIMITIVE_TOPOLOGY_TYPE toDX12TopologyType(mulan::engine::PrimitiveTopology topology) {
    using mulan::engine::PrimitiveTopology;
    switch (topology) {
    case PrimitiveTopology::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
    case PrimitiveTopology::LineListAdj:
    case PrimitiveTopology::LineStripAdj:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
    case PrimitiveTopology::TriangleListAdj:
    case PrimitiveTopology::TriangleStripAdj:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    }
}
} // namespace

namespace mulan::engine {

DX12PipelineState::DX12PipelineState(const GraphicsPipelineDesc& desc,
                                     ID3D12Device* device)
    : m_desc(desc)
    , m_device(device)
{
    createRootSignature();

    // 从 desc 读取 RT 格式，一步完成 PSO 创建
    DXGI_FORMAT rtFormat = (m_desc.colorTargetCount > 0)
        ? toDXGIFormat(m_desc.colorFormats[0])
        : DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dsFormat = m_desc.depthEnable
        ? toDSVFormat(m_desc.depthStencilFormat)
        : DXGI_FORMAT_UNKNOWN;
    build(rtFormat, dsFormat);
}

DX12PipelineState::~DX12PipelineState() = default;

void DX12PipelineState::createRootSignature() {
    // 根据 descriptorBindings 构建 root parameters
    // 策略：
    //   UniformBuffer → Root CBV (直接 GPU 地址)
    //   TextureSRV    → Descriptor Table (1 个 SRV range)
    //   Sampler       → (未来) static sampler
    //
    // 纹理绑定数由 m_desc.descriptorBindingCount 中的 TextureSRV 项决定

    // 第一遍：统计 TextureSRV bindings 数量
    uint32_t texBindingCount = 0;
    for (uint8_t i = 0; i < m_desc.descriptorBindingCount; ++i) {
        if (m_desc.descriptorBindings[i].type == DescriptorType::TextureSRV)
            ++texBindingCount;
    }

    // 为每个描述符表预分配 range（堆上分配以避免悬垂指针）
    std::vector<D3D12_DESCRIPTOR_RANGE> texRanges;
    texRanges.reserve(texBindingCount);

    std::vector<D3D12_ROOT_PARAMETER> rootParams;
    rootParams.reserve(m_desc.descriptorBindingCount);

    for (uint8_t i = 0; i < m_desc.descriptorBindingCount; ++i) {
        const auto& db = m_desc.descriptorBindings[i];
        if (db.count == 0) continue;

        D3D12_ROOT_PARAMETER param = {};
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
        if (db.stages == PipelineBinding::kStageVertex) {
            visibility = D3D12_SHADER_VISIBILITY_VERTEX;
        } else if (db.stages == PipelineBinding::kStageFragment) {
            visibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }

        switch (db.type) {
        case DescriptorType::UniformBuffer:
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.ShaderRegister = db.binding;
            param.Descriptor.RegisterSpace  = 0;
            param.ShaderVisibility = visibility;
            break;

        case DescriptorType::TextureSRV: {
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors     = db.count;
            range.BaseShaderRegister = db.binding;
            range.RegisterSpace      = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            texRanges.push_back(range);

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            // 指针在 push_back 后仍然有效，因为 texRanges 在此函数作用域内
            param.DescriptorTable.pDescriptorRanges = &texRanges.back();
            param.ShaderVisibility = visibility;
            break;
        }

        case DescriptorType::Sampler:
            // 未来：static sampler 或 descriptor table
            continue;
        }

        rootParams.push_back(param);
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = static_cast<UINT>(rootParams.size());
    rsDesc.pParameters   = rootParams.data();
    rsDesc.NumStaticSamplers = 0;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &error);
    DX12_CHECK(hr);

    if (error) {
        fprintf(stderr, "[DX12] Root signature error: %s\n",
                static_cast<const char*>(error->GetBufferPointer()));
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                       signature->GetBufferSize(),
                                       IID_PPV_ARGS(&m_rootSignature));
    DX12_CHECK(hr);
}

D3D12_INPUT_LAYOUT_DESC DX12PipelineState::buildInputLayout() {
    m_inputElements.clear();

    const auto& layout = m_desc.vertexLayout;

    for (uint32_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];

        // Semantic name 映射
        const char* semanticName = "POSITION";
        UINT semIdx = 0;
        switch (attr.semantic) {
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

        D3D12_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName         = semanticName;
        elem.SemanticIndex        = semIdx;
        elem.Format               = toDXGIFormat(attr.format);
        elem.InputSlot            = 0;
        elem.AlignedByteOffset    = attr.offset;
        elem.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = 0;

        m_inputElements.push_back(elem);
    }

    D3D12_INPUT_LAYOUT_DESC layoutDesc = {};
    layoutDesc.pInputElementDescs = m_inputElements.data();
    layoutDesc.NumElements        = static_cast<UINT>(m_inputElements.size());
    return layoutDesc;
}

void DX12PipelineState::build(DXGI_FORMAT rtFormat, DXGI_FORMAT dsFormat) {
    auto inputLayout = buildInputLayout();

    // Shader bytecode
    D3D12_SHADER_BYTECODE vsBytecode = {};
    D3D12_SHADER_BYTECODE psBytecode = {};
    if (m_desc.vs) vsBytecode = static_cast<DX12Shader*>(m_desc.vs)->byteCode();
    if (m_desc.ps) psBytecode = static_cast<DX12Shader*>(m_desc.ps)->byteCode();

    // Rasterizer
    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode              = toDX12FillMode(m_desc.fillMode);
    rasterizer.CullMode              = toDX12CullMode(m_desc.cullMode);
    rasterizer.FrontCounterClockwise = (m_desc.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
    rasterizer.DepthBias             = static_cast<INT>(m_desc.depthStencil.depthBias);
    rasterizer.DepthBiasClamp        = m_desc.depthStencil.depthBiasClamp;
    rasterizer.SlopeScaledDepthBias  = m_desc.depthStencil.slopeScaledDepthBias;
    rasterizer.DepthClipEnable       = TRUE;
    rasterizer.MultisampleEnable     = FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;
    rasterizer.ForcedSampleCount     = 0;
    rasterizer.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend
    D3D12_BLEND_DESC blend = {};
    blend.AlphaToCoverageEnable  = m_desc.blend.alphaToCoverage ? TRUE : FALSE;
    blend.IndependentBlendEnable = m_desc.blend.independentBlend ? TRUE : FALSE;
    for (int i = 0; i < 8; ++i) {
        const auto& src = m_desc.blend.renderTargets[i];
        auto& dst = blend.RenderTarget[i];
        dst.BlendEnable           = src.blendEnable ? TRUE : FALSE;
        dst.SrcBlend              = toDX12Blend(src.srcBlend);
        dst.DestBlend             = toDX12Blend(src.dstBlend);
        dst.BlendOp               = toDX12BlendOp(src.blendOp);
        dst.SrcBlendAlpha         = toDX12Blend(src.srcBlendAlpha);
        dst.DestBlendAlpha        = toDX12Blend(src.dstBlendAlpha);
        dst.BlendOpAlpha          = toDX12BlendOp(src.blendOpAlpha);
        dst.RenderTargetWriteMask = src.writeMask;
    }

    // Depth stencil
    D3D12_DEPTH_STENCIL_DESC depthStencil = {};
    depthStencil.DepthEnable      = m_desc.depthStencil.depthEnable ? TRUE : FALSE;
    depthStencil.DepthWriteMask   = m_desc.depthStencil.depthWrite
                                      ? D3D12_DEPTH_WRITE_MASK_ALL
                                      : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc        = toDX12CompareFunc(m_desc.depthStencil.depthFunc);
    depthStencil.StencilEnable    = m_desc.depthStencil.stencilEnable ? TRUE : FALSE;
    // Stencil ops 省略（当前不需要）

    // PSO 描述
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = vsBytecode;
    psoDesc.PS                    = psBytecode;
    psoDesc.InputLayout           = inputLayout;
    psoDesc.IBStripCutValue       = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    psoDesc.PrimitiveTopologyType = toDX12TopologyType(m_desc.topology);
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = rtFormat;
    psoDesc.DSVFormat             = dsFormat;
    psoDesc.SampleDesc            = { 1, 0 };
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.RasterizerState       = rasterizer;
    psoDesc.BlendState            = blend;
    psoDesc.DepthStencilState     = depthStencil;

    HRESULT hr = m_device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pipeline));
    if (FAILED(hr)) {
        DX12_LOG("[DX12] CreateGraphicsPipelineState failed name=%s topology=%d topoType=%u rt=%u ds=%u vsBytes=%zu psBytes=%zu\n",
                 std::string(m_desc.name).c_str(), static_cast<int>(m_desc.topology),
                 static_cast<unsigned>(psoDesc.PrimitiveTopologyType),
                 static_cast<unsigned>(rtFormat), static_cast<unsigned>(dsFormat),
                 vsBytecode.BytecodeLength, psBytecode.BytecodeLength);
    }
    DX12_CHECK(hr);
}

} // namespace mulan::engine
