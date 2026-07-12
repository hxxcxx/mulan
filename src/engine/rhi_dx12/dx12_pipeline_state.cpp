#include "detail/dx12_pipeline_state.h"
#include "detail/dx12_shader.h"

#include <mulan/core/result/error.h>
#include <mulan/graphics/vertex/vertex_semantic.h>
#include "../rhi/engine_error_code.h"

#include <deque>
#include <algorithm>
#include <string>
#include <vector>

namespace {
D3D12_PRIMITIVE_TOPOLOGY_TYPE toDX12TopologyType(mulan::engine::PrimitiveTopology topology) {
    using mulan::engine::PrimitiveTopology;
    switch (topology) {
    case PrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
    case PrimitiveTopology::LineListAdj:
    case PrimitiveTopology::LineStripAdj: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
    case PrimitiveTopology::TriangleListAdj:
    case PrimitiveTopology::TriangleStripAdj: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    }
}
}  // namespace

namespace mulan::engine {

using graphics::VertexSemantic;

core::Result<std::unique_ptr<DX12PipelineState>> DX12PipelineState::create(const GraphicsPipelineDesc& desc,
                                                                           ID3D12Device* device) {
    try {
        return std::unique_ptr<DX12PipelineState>(new DX12PipelineState(desc, device));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::PipelineCreateFailed,
                                         std::string("DX12PipelineState create failed: ") + e.what()));
    }
}

DX12PipelineState::DX12PipelineState(const GraphicsPipelineDesc& desc, ID3D12Device* device)
    : desc_(desc), device_(device) {
    createRootSignature();

    // 从 desc 读取 RT 格式，一步完成 PSO 创建
    DXGI_FORMAT rtFormat = (desc_.colorTargetCount > 0) ? toDXGIFormat(desc_.colorFormats[0]) : DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT dsFormat = desc_.depthEnable ? toDSVFormat(desc_.depthStencilFormat) : DXGI_FORMAT_UNKNOWN;
    build(rtFormat, dsFormat);
}

DX12PipelineState::~DX12PipelineState() = default;

void DX12PipelineState::createRootSignature() {
    // 根据 canonical binding 顺序构建 root parameters。BindGroupLayout 会按
    // binding 排序，DX12 root signature 也必须使用同一顺序，否则 root index
    // 映射会在 descriptorBindings 未按序填写时错位。
    std::vector<PipelineBinding> descriptorBindings;
    descriptorBindings.reserve(desc_.descriptorBindingCount);
    for (uint8_t i = 0; i < desc_.descriptorBindingCount; ++i) {
        if (desc_.descriptorBindings[i].count > 0)
            descriptorBindings.push_back(desc_.descriptorBindings[i]);
    }
    std::sort(descriptorBindings.begin(), descriptorBindings.end(),
              [](const PipelineBinding& a, const PipelineBinding& b) { return a.binding < b.binding; });

    // 用 deque 持有 descriptor range：deque 的 push_back 不会搬迁既有元素，
    // 因此 &texRanges.back() 指针在后续 push_back 后仍然有效。
    // （vector 会因扩容整体搬迁内存，使之前记录的 pDescriptorRanges 失效。）
    std::deque<D3D12_DESCRIPTOR_RANGE> texRanges;
    std::deque<D3D12_DESCRIPTOR_RANGE> samplerRanges;

    std::vector<D3D12_ROOT_PARAMETER> rootParams;
    rootParams.reserve(descriptorBindings.size());

    for (const auto& db : descriptorBindings) {
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
            param.Descriptor.RegisterSpace = 0;
            param.ShaderVisibility = visibility;
            break;

        case DescriptorType::TextureSRV: {
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = db.count;
            range.BaseShaderRegister = db.binding;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            texRanges.push_back(range);

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            // texRanges 是 deque，back() 指针在后续 push_back 后仍有效（见上方说明）
            param.DescriptorTable.pDescriptorRanges = &texRanges.back();
            param.ShaderVisibility = visibility;
            break;
        }

        case DescriptorType::Sampler: {
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.NumDescriptors = db.count;
            range.BaseShaderRegister = db.binding;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            samplerRanges.push_back(range);

            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &samplerRanges.back();
            param.ShaderVisibility = visibility;
            break;
        }
        }

        rootParams.push_back(param);
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = static_cast<UINT>(rootParams.size());
    rsDesc.pParameters = rootParams.data();
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    DX12_CHECK(hr);

    if (error) {
        fprintf(stderr, "[DX12] Root signature error: %s\n", static_cast<const char*>(error->GetBufferPointer()));
    }

    hr = device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&root_signature_));
    DX12_CHECK(hr);
}

D3D12_INPUT_LAYOUT_DESC DX12PipelineState::buildInputLayout() {
    input_elements_.clear();

    const auto& layout = desc_.vertexLayout;

    for (uint32_t i = 0; i < layout.attrCount(); ++i) {
        const auto& attr = layout[i];

        // Semantic name 映射
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

        D3D12_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName = semanticName;
        elem.SemanticIndex = semIdx;
        elem.Format = toDXGIFormat(attr.format);
        elem.InputSlot = 0;
        elem.AlignedByteOffset = attr.offset;
        elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = 0;

        input_elements_.push_back(elem);
    }

    D3D12_INPUT_LAYOUT_DESC layoutDesc = {};
    layoutDesc.pInputElementDescs = input_elements_.data();
    layoutDesc.NumElements = static_cast<UINT>(input_elements_.size());
    return layoutDesc;
}

void DX12PipelineState::build(DXGI_FORMAT rtFormat, DXGI_FORMAT dsFormat) {
    auto inputLayout = buildInputLayout();

    // Shader bytecode
    D3D12_SHADER_BYTECODE vsBytecode = {};
    D3D12_SHADER_BYTECODE psBytecode = {};
    if (desc_.vs)
        vsBytecode = static_cast<DX12Shader*>(desc_.vs)->byteCode();
    if (desc_.ps)
        psBytecode = static_cast<DX12Shader*>(desc_.ps)->byteCode();

    // Rasterizer
    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = toDX12FillMode(desc_.fillMode);
    rasterizer.CullMode = toDX12CullMode(desc_.cullMode);
    rasterizer.FrontCounterClockwise = (desc_.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
    rasterizer.DepthBias = static_cast<INT>(desc_.depthStencil.depthBias);
    rasterizer.DepthBiasClamp = desc_.depthStencil.depthBiasClamp;
    rasterizer.SlopeScaledDepthBias = desc_.depthStencil.slopeScaledDepthBias;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = desc_.sampleCount > 1 ? TRUE : FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;
    rasterizer.ForcedSampleCount = 0;
    rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend
    D3D12_BLEND_DESC blend = {};
    blend.AlphaToCoverageEnable = desc_.blend.alphaToCoverage ? TRUE : FALSE;
    blend.IndependentBlendEnable = desc_.blend.independentBlend ? TRUE : FALSE;
    for (int i = 0; i < 8; ++i) {
        const auto& src = desc_.blend.renderTargets[i];
        auto& dst = blend.RenderTarget[i];
        dst.BlendEnable = src.blendEnable ? TRUE : FALSE;
        dst.SrcBlend = toDX12Blend(src.srcBlend);
        dst.DestBlend = toDX12Blend(src.dstBlend);
        dst.BlendOp = toDX12BlendOp(src.blendOp);
        dst.SrcBlendAlpha = toDX12Blend(src.srcBlendAlpha);
        dst.DestBlendAlpha = toDX12Blend(src.dstBlendAlpha);
        dst.BlendOpAlpha = toDX12BlendOp(src.blendOpAlpha);
        dst.RenderTargetWriteMask = src.writeMask;
    }

    // Depth stencil
    D3D12_DEPTH_STENCIL_DESC depthStencil = {};
    depthStencil.DepthEnable = desc_.depthStencil.depthEnable ? TRUE : FALSE;
    depthStencil.DepthWriteMask =
            desc_.depthStencil.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = toDX12CompareFunc(desc_.depthStencil.depthFunc);
    depthStencil.StencilEnable = desc_.depthStencil.stencilEnable ? TRUE : FALSE;
    // Stencil ops 省略（当前不需要）

    // PSO 描述
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = root_signature_.Get();
    psoDesc.VS = vsBytecode;
    psoDesc.PS = psBytecode;
    psoDesc.InputLayout = inputLayout;
    psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    psoDesc.PrimitiveTopologyType = toDX12TopologyType(desc_.topology);
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtFormat;
    psoDesc.DSVFormat = dsFormat;
    psoDesc.SampleDesc = { desc_.sampleCount, 0 };
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = rasterizer;
    psoDesc.BlendState = blend;
    psoDesc.DepthStencilState = depthStencil;

    HRESULT hr = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline_));
    if (FAILED(hr)) {
        DX12_LOG(
                "[DX12] CreateGraphicsPipelineState failed name=%s topology=%d topoType=%u rt=%u ds=%u vsBytes=%zu "
                "psBytes=%zu\n",
                std::string(desc_.name).c_str(), static_cast<int>(desc_.topology),
                static_cast<unsigned>(psoDesc.PrimitiveTopologyType), static_cast<unsigned>(rtFormat),
                static_cast<unsigned>(dsFormat), vsBytecode.BytecodeLength, psBytecode.BytecodeLength);
    }
    DX12_CHECK(hr);
}

}  // namespace mulan::engine
