#include "detail/dx12_device.h"
#include "detail/dx12_debug_name.h"
#include "detail/dx12_texture.h"
#include "detail/dx12_bind_group.h"

#include "../rhi/engine_error_code.h"

#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace mulan::engine {

namespace {

uint32_t dx12SupportedSamples(ID3D12Device* device, DXGI_FORMAT format, uint32_t requested) {
    const uint32_t candidates[] = { 8, 4, 2, 1 };
    for (uint32_t sample : candidates) {
        if (sample > requested)
            continue;
        if (sample == 1)
            return 1;

        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
        levels.Format = format;
        levels.SampleCount = sample;
        levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) &&
            levels.NumQualityLevels > 0) {
            return sample;
        }
    }
    return 1;
}

RenderConfig::MSAALevel toMsaaLevel(uint32_t samples) {
    switch (samples) {
    case 8: return RenderConfig::MSAALevel::x8;
    case 4: return RenderConfig::MSAALevel::x4;
    case 2: return RenderConfig::MSAALevel::x2;
    default: return RenderConfig::MSAALevel::None;
    }
}

}  // namespace

// ============================================================
// 构造 / 析构
// ============================================================

DX12Device::DX12Device(const DeviceCreateInfo& ci) {
    init(ci);
}

DX12Device::~DX12Device() {
    waitIdle();
    upload_context_.reset();
    frames_.clear();
    shader_visible_heap_.reset();
    sampler_heap_.reset();
    command_queue_.Reset();
    device_.Reset();
    factory_.Reset();
}

// ============================================================
// 初始化
// ============================================================

void DX12Device::init(const DeviceCreateInfo& ci) {
    window_ = ci.window;
    render_config_ = ci.renderConfig;
    frame_count_ = ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2;
    if (ci.enableValidation)
        enableDebugLayer();
    createFactory();
    if (!factory_)
        return;
    findAdapter();
    createDevice();
    if (!device_)
        return;
    if (ci.enableValidation)
        attachInfoQueue();
    createCommandQueue();
    if (!command_queue_)
        return;
    createFrameContexts();
    if (frames_.size() != frame_count_ ||
        std::any_of(frames_.begin(), frames_.end(), [](const auto& frame) { return !frame || !frame->isValid(); }))
        return;

    upload_context_ = std::make_unique<DX12UploadContext>(device_.Get(), command_queue_.Get(), frame_count_);
    if (!upload_context_->isValid()) {
        upload_context_.reset();
        return;
    }

    frame_cmd_wrapper_ = std::make_unique<DX12CommandList>(nullptr);

    shader_visible_heap_ = std::make_unique<DX12DescriptorAllocator>(
            device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 8192);

    // Sampler descriptor heap：sampler 会在创建时写入持久 descriptor，
    // 绘制时通过 root descriptor table 直接引用 GPU handle。
    sampler_heap_ = std::make_unique<DX12DescriptorAllocator>(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 64);
    if (!shader_visible_heap_->isValid() || !sampler_heap_->isValid())
        return;

    caps_.backend = GraphicsBackend::D3D12;

    // D3D12 FL 12.0 所有基础特性均保证支持
    caps_.depthClamp = true;
    caps_.geometryShader = true;
    caps_.tessellationShader = true;
    caps_.computeShader = true;
    caps_.maxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;  // 16384
    caps_.maxTextureAniso = D3D12_DEFAULT_MAX_ANISOTROPY;         // 16
    const uint32_t colorSamples =
            dx12SupportedSamples(device_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, render_config_.sampleCount());
    const uint32_t depthSamples =
            dx12SupportedSamples(device_.Get(), DXGI_FORMAT_D24_UNORM_S8_UINT, render_config_.sampleCount());
    caps_.maxSampleCount = dx12SupportedSamples(device_.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, 8);
    render_config_.msaa = toMsaaLevel((std::min) (colorSamples, depthSamples));
    // minUniformBufferOffsetAlignment 保持默认 256（D3D12 常量缓冲对齐）
    LOG_INFO("[DX12] Device initialized: validation={}, maxMSAA={}, activeMSAA={}, frames={}", ci.enableValidation,
             caps_.maxSampleCount, (std::min) (colorSamples, depthSamples), frame_count_);
}

void DX12Device::enableDebugLayer() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        debug_controller_ = debug;

        // 同步命令队列校验：把异步的 device removed 变成同步上报，
        // 便于在故障点立刻拿到 InfoQueue 消息。
        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1))) {
            debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        }
        LOG_INFO("[DX12] D3D12 debug layer enabled");
    } else {
        LOG_WARN("[DX12] D3D12 debug layer requested but unavailable");
    }
}

void DX12Device::attachInfoQueue() {
    // 挂载 InfoQueue：让 D3D12 运行时把所有消息（error/warning/info）留存，
    // 供 dumpInfoQueueMessages() 在 device removed 时导出真正的原因。
    // 这是定位 device removed 根因的唯一可靠手段——
    // GetDeviceRemovedReason() 只返回「已移除」，从不说明原因。
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device_.As(&infoQueue)))
        return;
    info_queue_ = infoQueue;

    // 关闭存储过滤：让所有严重级别的消息都被留存
    infoQueue->PushEmptyStorageFilter();
    infoQueue->SetMessageCountLimit(D3D12_INFO_QUEUE_DEFAULT_MESSAGE_COUNT_LIMIT);
}

void DX12Device::dumpInfoQueueMessages() const {
    if (!info_queue_)
        return;
    UINT64 count = info_queue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        info_queue_->GetMessage(i, nullptr, &size);
        if (size == 0)
            continue;
        std::vector<uint8_t> buffer(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (SUCCEEDED(info_queue_->GetMessage(i, msg, &size))) {
            // pDescription 末尾含 '\0'，DescriptionByteLength 含该字节
            const char* desc = msg->pDescription ? msg->pDescription : "";
            switch (msg->Severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            case D3D12_MESSAGE_SEVERITY_ERROR: LOG_ERROR("[DX12 DebugLayer] {}", desc); break;
            case D3D12_MESSAGE_SEVERITY_WARNING: LOG_WARN("[DX12 DebugLayer] {}", desc); break;
            default: LOG_INFO("[DX12 DebugLayer] {}", desc); break;
            }
        }
    }
    // 清空已读消息，避免重复打印
    info_queue_->ClearStoredMessages();
}

void DX12Device::createFactory() {
    UINT flags = 0;
#ifdef _DEBUG
    // DXGIGetDebugInterface1 for DXGI debug — optional
#endif
    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_));
    if (!checkDX12(hr, "CreateDXGIFactory2"))
        return;
}

void DX12Device::findAdapter() {
    ComPtr<IDXGIAdapter1> bestAdapter;
    SIZE_T maxDedicatedVideoMemory = 0;

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory_->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            continue;

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // 跳过软件适配器
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (desc.DedicatedVideoMemory > maxDedicatedVideoMemory) {
            maxDedicatedVideoMemory = desc.DedicatedVideoMemory;
            bestAdapter = adapter;
        }
    }

    if (bestAdapter) {
        adapter_ = bestAdapter;
        LOG_INFO("[DX12] Hardware adapter selected: dedicatedVideoMemory={} MiB",
                 static_cast<uint64_t>(maxDedicatedVideoMemory / (1024ull * 1024ull)));
    } else {
        LOG_WARN("[DX12] No hardware adapter was enumerated; D3D12 will select its default adapter");
    }
}

void DX12Device::createDevice() {
    HRESULT hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
    if (!checkDX12(hr, "D3D12CreateDevice"))
        return;
}

void DX12Device::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&command_queue_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandQueue"))
        return;
}

void DX12Device::createFrameContexts() {
    frames_.reserve(frame_count_);
    for (uint32_t i = 0; i < frame_count_; ++i) {
        frames_.push_back(std::make_unique<DX12FrameContext>(device_.Get()));
    }
}

// ============================================================
// Clip Space — D3D12: Y↓ z∈[0,1]
// ============================================================

math::Mat4 DX12Device::clipSpaceCorrectionMatrix() const {
    // D3D12 NDC: Y↑ (same as OpenGL), z∈[0,1]
    // Camera generates OpenGL-style (Y↑, z∈[-1,1])
    // Only Z needs conversion: z' = 0.5*z + 0.5*w
    // (Vulkan flips Y; D3D12 does NOT)
    math::Mat4 mat(1.0);
    mat[2][2] = 0.5;  // z scale: [-1,1] → [0,1]
    mat[3][2] = 0.5;  // z offset
    return mat;
}

// ============================================================
// 资源创建
// ============================================================

core::Result<std::unique_ptr<Buffer>> DX12Device::createBuffer(const BufferDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        LOG_ERROR("[DX12] createBuffer called after device removal: reason=0x{:08X}", static_cast<unsigned>(reason));
        dumpInfoQueueMessages();
    }

    auto result = DX12Buffer::create(desc, device_.Get());
    if (!result)
        return std::unexpected(result.error());
    auto& buf = *result;
    setDebugName(buf->resource(), desc.name.empty() ? "Buffer" : desc.name);

    if (buf->needsUpload()) {
        upload_context_->uploadBuffer(buf.get(), buf->pendingData(), desc.size);
        buf->markUploaded();
    }

    buf->trackResource(*this, RHIResourceKind::Buffer, desc.name);
    return result;
}

core::Result<std::unique_ptr<Texture>> DX12Device::createTexture(const TextureDesc& desc) {
    auto result = DX12Texture::create(desc, device_.Get());
    if (!result)
        return std::unexpected(result.error());
    auto& tex = *result;
    setDebugName(tex->resource(), desc.name.empty() ? "Texture" : desc.name);
    tex->trackResource(*this, RHIResourceKind::Texture, desc.name);
    return result;
}

core::Result<std::unique_ptr<Shader>> DX12Device::createShader(const ShaderDesc& desc) {
    auto result = DX12Shader::create(desc);
    if (!result)
        return std::unexpected(result.error());
    // DX12Shader 仅持有 DXIL 字节码（无 COM 对象），无需命名
    (*result)->trackResource(*this, RHIResourceKind::Shader, desc.name);
    return result;
}

core::Result<std::unique_ptr<PipelineState>> DX12Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        LOG_ERROR("[DX12] createPipelineState({}) called after device removal: reason=0x{:08X}", desc.name,
                  static_cast<unsigned>(reason));
        dumpInfoQueueMessages();
    }
    auto result = DX12PipelineState::create(desc, device_.Get());
    if (!result)
        return std::unexpected(result.error());
    auto& pso = *result;
    setDebugName(pso->pipeline(), desc.name.empty() ? "Pipeline" : desc.name);
    setDebugName(pso->rootSignature(),
                 desc.name.empty() ? "RootSignature" : (std::string(desc.name) + "/RootSig").c_str());
    pso->trackResource(*this, RHIResourceKind::PipelineState, desc.name);
    return result;
}

core::Result<std::unique_ptr<ComputePipelineState>> DX12Device::createComputePipelineState(
        const ComputePipelineDesc& /*desc*/) {
    return std::unexpected(
            core::Error::make(core::ErrorCode::NotSupported, "DX12 compute pipeline not yet implemented"));
}

core::Result<std::unique_ptr<CommandList>> DX12Device::createCommandList() {
    auto allocator = ComPtr<ID3D12CommandAllocator>();
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    auto result = DX12CommandList::create(device_.Get(), allocator.Get());
    if (!result)
        return std::unexpected(result.error());
    auto& cmd = *result;
    cmd->setIndirectSignatures(drawIndirectSignature(), dispatchIndirectSignature());
    if (auto* heap = shader_visible_heap_->heap()) {
        const auto cpuBase = heap->GetCPUDescriptorHandleForHeapStart();
        const auto gpuBase = heap->GetGPUDescriptorHandleForHeapStart();
        cmd->setDescriptorHeap(heap, cpuBase, gpuBase, shader_visible_heap_->descriptorSize(),
                               sampler_heap_ ? sampler_heap_->heap() : nullptr, shader_visible_heap_->capacity());
    }
    char nm[64];
    std::snprintf(nm, sizeof(nm), "CommandList@%p", cmd.get());
    setDebugName(cmd->commandList(), nm);
    cmd->trackResource(*this, RHIResourceKind::CommandList, nm);
    return result;
}

core::Result<std::unique_ptr<SwapChain>> DX12Device::createSwapChain(const SwapChainDesc& desc) {
    SwapChainDesc resolvedDesc = desc;
    resolvedDesc.sampleCount = render_config_.sampleCount();
    if (!resolvedDesc.window.valid()) {
        return std::unexpected(
                makeError(EngineErrorCode::SwapChainCreateFailed, "DX12 swap chain requires a native window handle"));
    }
    auto result = DX12SwapChain::create(resolvedDesc, device_.Get(), factory_.Get(), command_queue_.Get(),
                                        resolvedDesc.window);
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(*this, RHIResourceKind::SwapChain, "SwapChain");
    return result;
}

core::Result<std::unique_ptr<RenderTarget>> DX12Device::createRenderTarget(const RenderTargetDesc& desc) {
    RenderTargetDesc resolvedDesc = desc;
    if (resolvedDesc.sampleCount > caps_.maxSampleCount)
        resolvedDesc.sampleCount = caps_.maxSampleCount;
    if (resolvedDesc.sampleCount != 1 && resolvedDesc.sampleCount != 2 && resolvedDesc.sampleCount != 4 &&
        resolvedDesc.sampleCount != 8) {
        resolvedDesc.sampleCount = 1;
    }
    auto result = DX12RenderTarget::create(resolvedDesc, device_.Get());
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(*this, RHIResourceKind::RenderTarget, "RenderTarget");
    return result;
}

core::Result<std::unique_ptr<Sampler>> DX12Device::createSampler(const SamplerDesc& desc) {
    // Sampler 仅持有 descriptor handle（非 COM 对象），无需命名。
    // Descriptors are allocated from the persistent shader-visible sampler heap
    // and consumed by the sampler descriptor-table root parameter.
    auto result = DX12Sampler::create(desc, device_.Get(), sampler_heap_.get());
    if (!result)
        return std::unexpected(result.error());
    (*result)->trackResource(*this, RHIResourceKind::Sampler, "Sampler");
    return result;
}

core::Result<std::unique_ptr<Fence>> DX12Device::createFence(uint64_t initialValue) {
    auto result = DX12Fence::create(device_.Get(), initialValue);
    if (!result)
        return std::unexpected(result.error());
    auto& f = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Fence@%p", f.get());
    setDebugName(f->fence(), nm);
    f->trackResource(*this, RHIResourceKind::Fence, nm);
    return result;
}

core::Result<std::unique_ptr<BindGroup>> DX12Device::createBindGroup(const BindGroupLayout& layout,
                                                                     const BindGroupDesc& desc) {
    auto bindGroup = std::unique_ptr<BindGroup>(std::make_unique<DX12BindGroup>(layout, desc.entries, desc.count));
    bindGroup->trackResource(*this, RHIResourceKind::BindGroup, "BindGroup");
    return bindGroup;
}

void DX12Device::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    upload_context_->uploadTexture(static_cast<DX12Texture*>(dst), upload);
}

void DX12Device::beginUploadBatch() {
    upload_context_->beginUploadBatch();
}

void DX12Device::flushUploadBatch() {
    upload_context_->flushUploadBatch();
}

ID3D12CommandSignature* DX12Device::drawIndirectSignature() {
    if (!draw_indirect_sig_) {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        sigDesc.NodeMask = 0;

        device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&draw_indirect_sig_));
    }
    return draw_indirect_sig_.Get();
}

ID3D12CommandSignature* DX12Device::dispatchIndirectSignature() {
    if (!dispatch_indirect_sig_) {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        sigDesc.NodeMask = 0;

        device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&dispatch_indirect_sig_));
    }
    return dispatch_indirect_sig_.Get();
}

// ============================================================
// 命令提交
// ============================================================

void DX12Device::executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence, uint64_t fenceValue) {
    std::vector<ID3D12CommandList*> lists(count);
    for (uint32_t i = 0; i < count; ++i) {
        lists[i] = static_cast<DX12CommandList*>(cmdLists[i])->commandList();
    }
    command_queue_->ExecuteCommandLists(count, lists.data());

    if (fence) {
        auto* dx12Fence = static_cast<DX12Fence*>(fence);
        command_queue_->Signal(dx12Fence->fence(), fenceValue);
    }
}

void DX12Device::waitIdle() {
    if (!command_queue_)
        return;
    ComPtr<ID3D12Fence> fence;
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    command_queue_->Signal(fence.Get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
}

// ============================================================
// 帧循环
// ============================================================

void DX12Device::beginFrame(SwapChain* /*swapchain*/) {
    frame_index_ = (frame_index_ + 1) % frame_count_;
    auto& frame = frames_[frame_index_];
    frame->waitForFence();
    frame->resetCommandAllocator();

    // 重置 shader-visible 描述符堆
    shader_visible_heap_->reset();

    // 单调递增 frame token：heap reset 已回收上一帧的 descriptor 区段，
    // 自增后 BindGroup 缓存句柄的旧 token 必然失配，触发跨帧失效。
    ++frame_token_;
}

void DX12Device::clearCaches() {
    // D3D12 后端暂无内部缓存需要清理
}

CommandList* DX12Device::frameCommandList() {
    auto& frame = frames_[frame_index_];
    frame_cmd_wrapper_->setCommandList(frame->commandList());

    // 注入当前帧 token，让 BindGroup 缓存的 descriptor 句柄跨帧自动失效
    frame_cmd_wrapper_->setFrameToken(frame_token_);

    // 设置当前帧的描述符堆（bindResources 时分配 SRV 句柄用）
    auto* heap = shader_visible_heap_->heap();
    if (heap) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = heap->GetGPUDescriptorHandleForHeapStart();
        uint32_t descSize = shader_visible_heap_->descriptorSize();
        frame_cmd_wrapper_->setDescriptorHeap(heap, cpuBase, gpuBase, descSize,
                                              sampler_heap_ ? sampler_heap_->heap() : nullptr,
                                              shader_visible_heap_->capacity());
    }

    return frame_cmd_wrapper_.get();
}

void DX12Device::submitAndPresent(SwapChain* swapchain) {
    submit();
    present(swapchain);
}

void DX12Device::submit() {
    auto& frame = frames_[frame_index_];

    // cmd list 已由 EngineView::cmd->end() 关闭，直接提交
    ID3D12CommandList* lists[] = { frame->commandList() };
    command_queue_->ExecuteCommandLists(1, lists);

    // Signal fence
    auto fenceVal = frame->fenceValue() + 1;
    frame->setFenceValue(fenceVal);
    HRESULT hr = command_queue_->Signal(frame->fence()->fence(), fenceVal);
    if (!checkDX12(hr, "ID3D12CommandQueue::Signal"))
        return;
}

void DX12Device::present(SwapChain* swapchain) {
    auto* dx12Swap = static_cast<DX12SwapChain*>(swapchain);
    dx12Swap->present();
    frame_index_ = (frame_index_ + 1) % frame_count_;
}

void DX12Device::submitOffscreen() {
    auto& frame = frames_[frame_index_];

    ID3D12CommandList* lists[] = { frame->commandList() };
    command_queue_->ExecuteCommandLists(1, lists);

    auto fenceVal = frame->fenceValue() + 1;
    frame->setFenceValue(fenceVal);
    command_queue_->Signal(frame->fence()->fence(), fenceVal);
}

}  // namespace mulan::engine
