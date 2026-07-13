#include "detail/dx11_device.h"
#include "../rhi/engine_error_code.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <expected>
#include <string>
#include <thread>
#include <utility>

namespace {

using namespace mulan::engine;

RenderConfig::MSAALevel toMSAALevel(uint32_t sampleCount) {
    switch (sampleCount) {
    case 8: return RenderConfig::MSAALevel::x8;
    case 4: return RenderConfig::MSAALevel::x4;
    case 2: return RenderConfig::MSAALevel::x2;
    default: return RenderConfig::MSAALevel::None;
    }
}

template <typename Base, typename Impl, typename... Args>
mulan::core::Result<std::unique_ptr<Base>> createDX11Resource(RHIDevice& device, EngineErrorCode errorCode,
                                                              RHIResourceKind resourceKind, std::string_view name,
                                                              Args&&... args) {
    auto resource = std::make_unique<Impl>(std::forward<Args>(args)...);
    if (!resource->isValid())
        return std::unexpected(makeError(errorCode, "DX11 resource initialization failed"));
    resource->trackResource(device, resourceKind, name);
    return std::unique_ptr<Base>(std::move(resource));
}

const BindGroupLayoutEntry* findLayoutEntry(const BindGroupLayout& layout, uint32_t binding) {
    const auto& entries = layout.entries();
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [binding](const BindGroupLayoutEntry& entry) { return entry.binding == binding; });
    return it == entries.end() ? nullptr : &*it;
}

std::string validateDX11BindGroup(const BindGroupLayout& layout, const BindGroupDesc& desc) {
    if (desc.count > BindGroupDesc::kMaxEntries)
        return "DX11 BindGroup entry count exceeds the RHI limit";

    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& entry = desc.entries[i];
        const uint32_t resourceCount = static_cast<uint32_t>(entry.buffer != nullptr) +
                                       static_cast<uint32_t>(entry.texture != nullptr) +
                                       static_cast<uint32_t>(entry.sampler != nullptr);
        if (resourceCount != 1)
            return "every DX11 BindGroup entry must contain exactly one resource";
        if (!findLayoutEntry(layout, entry.binding))
            return "DX11 BindGroup contains a binding absent from its layout";
        for (uint8_t j = 0; j < i; ++j) {
            if (desc.entries[j].binding == entry.binding)
                return "DX11 BindGroup contains duplicate bindings";
        }
    }

    for (const auto& layoutEntry : layout.entries()) {
        if (layoutEntry.count != 1)
            return "DX11 BindGroup descriptor arrays are not represented by the current RHI entry type";

        const BindGroupEntry* entry = nullptr;
        for (uint8_t i = 0; i < desc.count; ++i) {
            if (desc.entries[i].binding == layoutEntry.binding) {
                entry = &desc.entries[i];
                break;
            }
        }
        if (!entry)
            return "DX11 BindGroup is missing a layout binding";

        switch (layoutEntry.type) {
        case DescriptorType::UniformBuffer:
            if (!entry->buffer || !dynamic_cast<DX11Buffer*>(entry->buffer))
                return "DX11 uniform binding requires a DX11Buffer";
            if (layoutEntry.binding >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
                return "DX11 uniform binding slot exceeds the D3D11 limit";
            break;
        case DescriptorType::TextureSRV:
            if (!entry->texture || !dynamic_cast<DX11Texture*>(entry->texture))
                return "DX11 texture binding requires a DX11Texture";
            if (layoutEntry.binding >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
                return "DX11 texture binding slot exceeds the D3D11 limit";
            break;
        case DescriptorType::Sampler:
            if (!entry->sampler || !dynamic_cast<DX11Sampler*>(entry->sampler))
                return "DX11 sampler binding requires a DX11Sampler";
            if (layoutEntry.binding >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT)
                return "DX11 sampler binding slot exceeds the D3D11 limit";
            break;
        }
    }

    return {};
}

}  // namespace

namespace mulan::engine {

DX11Device::DX11Device(const DeviceCreateInfo& ci) {
    init(ci);
}

DX11Device::~DX11Device() {
    waitIdle();
    drainDeferredReleases();
    shutdownSubmissionTracking();
    m_frameCmdList.reset();
    m_immediateCtx1.Reset();
    m_immediateCtx.Reset();
    m_debugDevice.Reset();
    m_device.Reset();
    m_factory.Reset();
}

void DX11Device::init(const DeviceCreateInfo& ci) {
    m_window = ci.window;
    m_renderConfig = ci.renderConfig;
    if (m_renderConfig.bufferCount == 0)
        m_renderConfig.bufferCount = 2;

    if (!checkDX11(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory1"))
        return;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const bool requestedDebugLayer = ci.enableValidation;
    if (requestedDebugLayer)
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL achievedLevel = {};
    const auto createHardwareDevice = [&](UINT flags) {
        m_device.Reset();
        m_immediateCtx.Reset();
        achievedLevel = {};

        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels,
                                           _countof(featureLevels), D3D11_SDK_VERSION, &m_device, &achievedLevel,
                                           &m_immediateCtx);

        // 旧版 D3D11 runtime 不识别 feature-level 11.1；去掉该项后重试，
        // 后续由 Context1 兼容路径处理范围常量缓冲。
        if (result == E_INVALIDARG) {
            m_device.Reset();
            m_immediateCtx.Reset();
            achievedLevel = {};
            const D3D_FEATURE_LEVEL fallbackFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fallbackFeatureLevels,
                                       _countof(fallbackFeatureLevels), D3D11_SDK_VERSION, &m_device, &achievedLevel,
                                       &m_immediateCtx);
        }
        return result;
    };

    HRESULT hr = createHardwareDevice(createFlags);

    // Debug layer 并非所有开发环境都已安装；仅在该情况下降级重试。两次尝试均复用
    // 同一 feature-level 兼容逻辑，避免旧 runtime 与缺失 debug layer 叠加时回退失败。
    if (FAILED(hr) && requestedDebugLayer) {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = createHardwareDevice(createFlags);
    }
    if (!checkDX11(hr, "D3D11CreateDevice"))
        return;
    if (!m_device || !m_immediateCtx) {
        LOG_ERROR("[DX11] D3D11CreateDevice returned an incomplete device");
        return;
    }

    // D3D11.1 支持常量缓冲区范围绑定。缺失时 CommandList 使用 GPU copy 兼容路径，
    // 仍保证大 Object UBO 和按材质偏移的结果正确。
    const HRESULT context1Result = m_immediateCtx.As(&m_immediateCtx1);
    if (FAILED(context1Result))
        m_immediateCtx1.Reset();

    if (ci.enableValidation && (createFlags & D3D11_CREATE_DEVICE_DEBUG))
        m_device.As(&m_debugDevice);
    if (requestedDebugLayer && !(createFlags & D3D11_CREATE_DEVICE_DEBUG))
        LOG_WARN("[DX11] D3D11 debug layer is unavailable; validation was disabled for this device");

    m_frameCmdList = std::make_unique<DX11CommandList>(m_device.Get(), m_immediateCtx.Get(), m_immediateCtx1.Get());
    m_frameCmdList->trackResource(*this, RHIResourceKind::CommandList, "DX11FrameCommandList");

    auto submissionFenceResult = createFence(0);
    if (!submissionFenceResult) {
        LOG_ERROR("[DX11] Submission timeline creation failed: {}", submissionFenceResult.error().message);
        return;
    }
    initializeSubmissionTracking(std::move(*submissionFenceResult));

    m_caps.backend = GraphicsBackend::D3D11;
    m_caps.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    m_caps.maxTextureAniso = D3D11_REQ_MAXANISOTROPY;
    m_caps.maxSampleCount = resolveSampleCount(TextureFormat::RGBA8_UNorm, TextureFormat::D24_UNorm_S8_UInt, true, 8);
    m_caps.minUniformBufferOffsetAlignment = 256;
    m_caps.depthClamp = true;
    m_caps.geometryShader = achievedLevel >= D3D_FEATURE_LEVEL_11_0;
    // 当前 DX11 CommandList 仅实现 graphics immediate-context 路径；不要把尚未
    // 落地的 Hull/Domain/Compute 支持暴露给上层 capability 协商。
    m_caps.tessellationShader = false;
    m_caps.computeShader = false;

    const uint32_t selectedSamples = resolveSampleCount(TextureFormat::RGBA8_UNorm, TextureFormat::D24_UNorm_S8_UInt,
                                                        true, m_renderConfig.sampleCount());
    m_renderConfig.msaa = toMSAALevel(selectedSamples);
    LOG_INFO("[DX11] Device initialized: featureLevel=0x{:X}, debugLayer={}, context1={}, maxMSAA={}",
             static_cast<unsigned>(achievedLevel), m_debugDevice != nullptr, m_immediateCtx1 != nullptr,
             m_caps.maxSampleCount);
}

uint32_t DX11Device::resolveSampleCount(TextureFormat colorFormat, TextureFormat depthFormat, bool hasDepth,
                                        uint32_t requestedSampleCount) const {
    if (!m_device)
        return 1;

    const DXGI_FORMAT color = toDXGIFormat11(colorFormat);
    const DXGI_FORMAT depth = toDSVFormat11(depthFormat);
    if (color == DXGI_FORMAT_UNKNOWN || (hasDepth && depth == DXGI_FORMAT_UNKNOWN))
        return 1;

    const uint32_t requested = requestedSampleCount > 1 ? requestedSampleCount : 1;
    constexpr std::array<uint32_t, 4> sampleCounts = { 8, 4, 2, 1 };
    for (const uint32_t samples : sampleCounts) {
        if (samples > requested)
            continue;
        if (samples == 1)
            return 1;

        UINT colorQualityLevels = 0;
        if (FAILED(m_device->CheckMultisampleQualityLevels(color, samples, &colorQualityLevels)) ||
            colorQualityLevels == 0) {
            continue;
        }
        if (hasDepth) {
            UINT depthQualityLevels = 0;
            if (FAILED(m_device->CheckMultisampleQualityLevels(depth, samples, &depthQualityLevels)) ||
                depthQualityLevels == 0) {
                continue;
            }
        }
        return samples;
    }
    return 1;
}

math::Mat4 DX11Device::clipSpaceCorrectionMatrix() const {
    // D3D11 与 D3D12 都使用 Y↑、Z∈[0,1]；仅把统一投影的 Z 从 [-1,1] 映射到 [0,1]。
    math::Mat4 mat(1.0);
    mat[2][2] = 0.5;
    mat[3][2] = 0.5;
    return mat;
}

core::Result<std::unique_ptr<Buffer>> DX11Device::createBuffer(const BufferDesc& desc) {
    if (!m_device || !m_immediateCtx)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<Buffer, DX11Buffer>(*this, EngineErrorCode::BufferCreateFailed, RHIResourceKind::Buffer,
                                                  desc.name, desc, m_device.Get(), m_immediateCtx.Get());
}

core::Result<std::unique_ptr<Texture>> DX11Device::createTexture(const TextureDesc& desc) {
    if (!m_device)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<Texture, DX11Texture>(*this, EngineErrorCode::TextureCreateFailed,
                                                    RHIResourceKind::Texture, desc.name, desc, m_device.Get());
}

core::Result<std::unique_ptr<Shader>> DX11Device::createShader(const ShaderDesc& desc) {
    if (!m_device)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<Shader, DX11Shader>(*this, EngineErrorCode::ShaderCompileFailed, RHIResourceKind::Shader,
                                                  desc.name, desc, m_device.Get());
}

core::Result<std::unique_ptr<PipelineState>> DX11Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    if (!m_device)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<PipelineState, DX11PipelineState>(*this, EngineErrorCode::PipelineCreateFailed,
                                                                RHIResourceKind::PipelineState, desc.name, desc,
                                                                m_device.Get());
}

core::Result<std::unique_ptr<ComputePipelineState>> DX11Device::createComputePipelineState(const ComputePipelineDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "D3D11 compute pipeline is not implemented"));
}

core::Result<std::unique_ptr<CommandList>> DX11Device::createCommandList() {
    if (!m_device || !m_immediateCtx)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<CommandList, DX11CommandList>(
            *this, EngineErrorCode::CommandListCreateFailed, RHIResourceKind::CommandList, "DX11CommandList",
            m_device.Get(), m_immediateCtx.Get(), m_immediateCtx1.Get());
}

core::Result<std::unique_ptr<SwapChain>> DX11Device::createSwapChain(const SwapChainDesc& desc) {
    if (!m_device || !m_factory || !m_immediateCtx)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    if (!desc.window.valid() || desc.window.type != NativeWindowHandle::Type::Win32) {
        return std::unexpected(
                makeError(EngineErrorCode::SwapChainCreateFailed, "DX11 swap chain requires a native Win32 window"));
    }

    SwapChainDesc resolvedDesc = desc;
    resolvedDesc.sampleCount = resolveSampleCount(resolvedDesc.format, resolvedDesc.depthFormat, resolvedDesc.hasDepth,
                                                  m_renderConfig.sampleCount());
    return createDX11Resource<SwapChain, DX11SwapChain>(
            *this, EngineErrorCode::SwapChainCreateFailed, RHIResourceKind::SwapChain, "DX11SwapChain", resolvedDesc,
            m_device.Get(), m_factory.Get(), m_immediateCtx.Get(), resolvedDesc.window);
}

core::Result<std::unique_ptr<RenderTarget>> DX11Device::createRenderTarget(const RenderTargetDesc& desc) {
    if (!m_device)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));

    RenderTargetDesc resolvedDesc = desc;
    resolvedDesc.sampleCount = resolveSampleCount(resolvedDesc.colorFormat, resolvedDesc.depthFormat,
                                                  resolvedDesc.hasDepth, resolvedDesc.sampleCount);
    return createDX11Resource<RenderTarget, DX11RenderTarget>(*this, EngineErrorCode::RenderTargetCreateFailed,
                                                              RHIResourceKind::RenderTarget, "DX11RenderTarget",
                                                              resolvedDesc, m_device.Get());
}

core::Result<std::unique_ptr<Sampler>> DX11Device::createSampler(const SamplerDesc& desc) {
    if (!m_device)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    const std::string_view name = desc.debugName.empty() ? std::string_view("DX11Sampler") : desc.debugName;
    return createDX11Resource<Sampler, DX11Sampler>(*this, EngineErrorCode::SamplerCreateFailed,
                                                    RHIResourceKind::Sampler, name, desc, m_device.Get());
}

core::Result<std::unique_ptr<Fence>> DX11Device::createFence(uint64_t value) {
    if (!m_device || !m_immediateCtx)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 device is not initialized"));
    return createDX11Resource<Fence, DX11Fence>(*this, EngineErrorCode::FenceCreateFailed, RHIResourceKind::Fence,
                                                "DX11Fence", m_device.Get(), m_immediateCtx.Get(), value);
}

core::Result<std::unique_ptr<BindGroup>> DX11Device::createBindGroup(const BindGroupLayout& layout,
                                                                     const BindGroupDesc& desc) {
    std::string validationError = validateBindGroupDesc(
            layout, desc, { m_caps.minUniformBufferOffsetAlignment, m_caps.maxUniformBufferBindingSize });
    if (validationError.empty())
        validationError = validateDX11BindGroup(layout, desc);
    if (!validationError.empty())
        return std::unexpected(makeError(EngineErrorCode::ResourceCreateFailed, validationError));
    return createDX11Resource<BindGroup, DX11BindGroup>(*this, EngineErrorCode::ResourceCreateFailed,
                                                        RHIResourceKind::BindGroup, "DX11BindGroup", layout, desc);
}

void DX11Device::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    auto* texture = dynamic_cast<DX11Texture*>(dst);
    if (!texture || !texture->resource() || !m_immediateCtx || upload.data.empty()) {
        LOG_ERROR("[DX11] uploadTextureData rejected: invalid texture, missing context, or empty source data");
        return;
    }

    const auto& desc = texture->desc();
    const uint32_t bytesPerPixel = textureFormatBytesPerPixel(upload.format);
    if (desc.dimension != TextureDimension::Texture2D || desc.sampleCount != 1 || bytesPerPixel == 0 ||
        upload.format != desc.format || upload.mipLevel >= desc.mipLevels || upload.arrayLayer >= desc.arraySize) {
        LOG_ERROR("[DX11] uploadTextureData rejected: unsupported format or subresource");
        return;
    }

    const uint32_t expectedWidth = (std::max) (1u, desc.width >> upload.mipLevel);
    const uint32_t expectedHeight = (std::max) (1u, desc.height >> upload.mipLevel);
    if (upload.width != expectedWidth || upload.height != expectedHeight || upload.depth != 1) {
        LOG_ERROR("[DX11] uploadTextureData rejected: dimensions do not match the destination subresource");
        return;
    }

    const uint32_t rowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bytesPerPixel;
    const uint32_t minimumRowPitch = upload.width * bytesPerPixel;
    const uint32_t slicePitch = upload.sourceSlicePitch ? upload.sourceSlicePitch : rowPitch * upload.height;
    if (rowPitch < minimumRowPitch || slicePitch < rowPitch * upload.height ||
        upload.data.size_bytes() < static_cast<size_t>(slicePitch)) {
        LOG_ERROR("[DX11] uploadTextureData rejected: source pitch or data size is invalid");
        return;
    }

    const UINT subresource = D3D11CalcSubresource(upload.mipLevel, upload.arrayLayer, desc.mipLevels);
    m_immediateCtx->UpdateSubresource(texture->resource(), subresource, nullptr, upload.data.data(), rowPitch,
                                      slicePitch);
}

core::Result<SubmissionToken> DX11Device::executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence,
                                                              uint64_t value) {
    if (!cmdLists || count == 0)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 command list batch is empty"));
    auto submissionLock = lockSubmissionQueue();
    // CommandList 直接包装 immediate context，命令在录制时已经进入同一条队列。
    if (fence) {
        auto* dx11Fence = dynamic_cast<DX11Fence*>(fence);
        if (!dx11Fence) {
            LOG_ERROR("[DX11] executeCommandLists rejected: fence is not a DX11 fence");
            return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 external fence type is invalid"));
        }
        dx11Fence->signal(value);
    }

    const SubmissionToken token = reserveSubmissionToken();
    auto* completionFence = static_cast<DX11Fence*>(submissionFence());
    if (!token || !completionFence)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 submission timeline is unavailable"));
    completionFence->signal(token.value);
    if (completionFence->signaledValue() < token.value) {
        LOG_ERROR("[DX11] Standalone submission timeline signal failed");
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 submission timeline signal failed"));
    }
    for (uint32_t i = 0; i < count; ++i)
        cmdLists[i]->markSubmitted(token);
    commitSubmission(token);
    return token;
}

void DX11Device::waitIdle() {
    if (!m_device || !m_immediateCtx)
        return;

    D3D11_QUERY_DESC desc = {};
    desc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    HRESULT hr = m_device->CreateQuery(&desc, &query);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11Device::CreateQuery(waitIdle)");
        return;
    }

    m_immediateCtx->End(query.Get());
    m_immediateCtx->Flush();
    BOOL completed = FALSE;
    while ((hr = m_immediateCtx->GetData(query.Get(), &completed, sizeof(completed), 0)) == S_FALSE)
        std::this_thread::yield();
    if (FAILED(hr))
        logDX11Failure(hr, "ID3D11DeviceContext::GetData(waitIdle)");
}

void DX11Device::beginFrame(SwapChain*) {
    collectGarbage();
    // immediate context 不需要 acquire/reset；SwapChain 在 Present 后自动轮换。
}

void DX11Device::clearCaches() {
    if (m_immediateCtx)
        m_immediateCtx->ClearState();
}

CommandList* DX11Device::frameCommandList() {
    return m_frameCmdList.get();
}

core::Result<SubmissionToken> DX11Device::submit() {
    if (!m_immediateCtx)
        return std::unexpected(makeError(EngineErrorCode::DeviceLost, "DX11 immediate context is unavailable"));

    auto submissionLock = lockSubmissionQueue();
    const SubmissionToken token = reserveSubmissionToken();
    if (!token)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 submission timeline is unavailable"));
    auto* completionFence = static_cast<DX11Fence*>(submissionFence());
    completionFence->signal(token.value);
    if (completionFence->signaledValue() < token.value)
        return std::unexpected(makeError(EngineErrorCode::SubmissionFailed, "DX11 event query creation failed"));
    m_frameCmdList->markSubmitted(token);
    commitSubmission(token);
    return token;
}

void DX11Device::present(SwapChain* swapchain) {
    if (swapchain)
        swapchain->present();
}

core::Result<SubmissionToken> DX11Device::submitAndPresent(SwapChain* swapchain) {
    auto result = submit();
    if (!result)
        return std::unexpected(result.error());
    present(swapchain);
    return result;
}

core::Result<SubmissionToken> DX11Device::submitOffscreen() {
    return submit();
}

}  // namespace mulan::engine
