#include "dx12_device.h"
#include "dx12_debug_name.h"
#include "dx12_texture.h"

#include <cstdio>
#include <string>
#include <vector>
#include <cstring>

namespace mulan::engine {

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
    command_queue_.Reset();
    device_.Reset();
    factory_.Reset();
}

// ============================================================
// 初始化
// ============================================================

void DX12Device::init(const DeviceCreateInfo& ci) {
    window_       = ci.window;
    render_config_ = ci.renderConfig;
    frame_count_   = ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2;
    if (ci.enableValidation) enableDebugLayer();
    createFactory();
    findAdapter();
    createDevice();
    if (ci.enableValidation) attachInfoQueue();
    createCommandQueue();
    createFrameContexts();

    upload_context_ = std::make_unique<DX12UploadContext>(
        device_.Get(), command_queue_.Get(), frame_count_);

    frame_cmd_wrapper_ = std::make_unique<DX12CommandList>(nullptr);

    shader_visible_heap_ = std::make_unique<DX12DescriptorAllocator>(
        device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1024);

    caps_.backend = GraphicsBackend::D3D12;

    // D3D12 FL 12.0 所有基础特性均保证支持
    caps_.depthClamp         = true;
    caps_.geometryShader     = true;
    caps_.tessellationShader = true;
    caps_.computeShader      = true;
    caps_.maxTextureSize     = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;   // 16384
    caps_.maxTextureAniso    = D3D12_DEFAULT_MAX_ANISOTROPY;           // 16
    // minUniformBufferOffsetAlignment 保持默认 256（D3D12 常量缓冲对齐）
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
    }
}

void DX12Device::attachInfoQueue() {
    // 挂载 InfoQueue：让 D3D12 运行时把所有消息（error/warning/info）留存，
    // 供 dumpInfoQueueMessages() 在 device removed 时导出真正的原因。
    // 这是定位 device removed 根因的唯一可靠手段——
    // GetDeviceRemovedReason() 只返回「已移除」，从不说明原因。
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device_.As(&infoQueue))) return;
    info_queue_ = infoQueue;

    // 关闭存储过滤：让所有严重级别的消息都被留存
    infoQueue->PushEmptyStorageFilter();
    infoQueue->SetMessageCountLimit(D3D12_INFO_QUEUE_DEFAULT_MESSAGE_COUNT_LIMIT);
}

void DX12Device::dumpInfoQueueMessages() const {
    if (!info_queue_) return;
    UINT64 count = info_queue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        info_queue_->GetMessage(i, nullptr, &size);
        if (size == 0) continue;
        std::vector<uint8_t> buffer(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (SUCCEEDED(info_queue_->GetMessage(i, msg, &size))) {
            // pDescription 末尾含 '\0'，DescriptionByteLength 含该字节
            const char* desc = msg->pDescription ? msg->pDescription : "";
            std::fprintf(stderr, "[D3D12] %s%s",
                         desc,
                         (desc[0] && desc[std::strlen(desc) - 1] != '\n') ? "\n" : "");
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
    DX12_CHECK(hr);
}

void DX12Device::findAdapter() {
    ComPtr<IDXGIAdapter1> bestAdapter;
    SIZE_T maxDedicatedVideoMemory = 0;

    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory_->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // 跳过软件适配器
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (desc.DedicatedVideoMemory > maxDedicatedVideoMemory) {
            maxDedicatedVideoMemory = desc.DedicatedVideoMemory;
            bestAdapter = adapter;
        }
    }

    if (bestAdapter) {
        adapter_ = bestAdapter;
    }
}

void DX12Device::createDevice() {
    HRESULT hr = D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&device_));
    DX12_CHECK(hr);
}

void DX12Device::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    HRESULT hr = device_->CreateCommandQueue(&queueDesc,
                                              IID_PPV_ARGS(&command_queue_));
    DX12_CHECK(hr);
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

Mat4 DX12Device::clipSpaceCorrectionMatrix() const {
    // D3D12 NDC: Y↑ (same as OpenGL), z∈[0,1]
    // Camera generates OpenGL-style (Y↑, z∈[-1,1])
    // Only Z needs conversion: z' = 0.5*z + 0.5*w
    // (Vulkan flips Y; D3D12 does NOT)
    Mat4 mat(1.0);
    mat[2][2] =  0.5;   // z scale: [-1,1] → [0,1]
    mat[3][2] =  0.5;   // z offset
    return mat;
}

// ============================================================
// 资源创建
// ============================================================

std::expected<std::unique_ptr<Buffer>, core::Error> DX12Device::createBuffer(const BufferDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        std::fprintf(stderr, "[DX12 ERROR] createBuffer: device already removed! Reason=0x%08lX\n",
                     static_cast<unsigned long>(reason));
        dumpInfoQueueMessages();
    }

    auto result = DX12Buffer::create(desc, device_.Get());
    if (!result) return std::unexpected(result.error());
    auto& buf = *result;
    setDebugName(buf->resource(), desc.name.empty() ? "Buffer" : desc.name);

    if (buf->needsUpload()) {
        upload_context_->uploadBuffer(buf.get(), buf->pendingData(), desc.size);
        buf->markUploaded();
    }

    return result;
}

std::expected<std::unique_ptr<Texture>, core::Error> DX12Device::createTexture(const TextureDesc& desc) {
    auto result = DX12Texture::create(desc, device_.Get());
    if (!result) return std::unexpected(result.error());
    auto& tex = *result;
    setDebugName(tex->resource(), desc.name.empty() ? "Texture" : desc.name);
    return result;
}

std::expected<std::unique_ptr<Shader>, core::Error> DX12Device::createShader(const ShaderDesc& desc) {
    auto result = DX12Shader::create(desc);
    if (!result) return std::unexpected(result.error());
    // DX12Shader 仅持有 DXIL 字节码（无 COM 对象），无需命名
    return result;
}

std::expected<std::unique_ptr<PipelineState>, core::Error>
DX12Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        std::fprintf(stderr, "[DX12 ERROR] createPipelineState('%.*s'): device already removed! Reason=0x%08lX\n",
                     static_cast<int>(desc.name.size()), desc.name.data(),
                     static_cast<unsigned long>(reason));
        dumpInfoQueueMessages();
    }
    auto result = DX12PipelineState::create(desc, device_.Get());
    if (!result) return std::unexpected(result.error());
    auto& pso = *result;
    setDebugName(pso->pipeline(),
                 desc.name.empty() ? "Pipeline" : desc.name);
    setDebugName(pso->rootSignature(),
                 desc.name.empty() ? "RootSignature" : (std::string(desc.name) + "/RootSig").c_str());
    return result;
}

std::expected<std::unique_ptr<CommandList>, core::Error> DX12Device::createCommandList() {
    auto allocator = ComPtr<ID3D12CommandAllocator>();
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     IID_PPV_ARGS(&allocator));
    auto result = DX12CommandList::create(device_.Get(), allocator.Get());
    if (!result) return std::unexpected(result.error());
    auto& cmd = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "CommandList@%p", cmd.get());
    setDebugName(cmd->commandList(), nm);
    return result;
}

std::expected<std::unique_ptr<SwapChain>, core::Error>
DX12Device::createSwapChain(const SwapChainDesc& desc) {
    return DX12SwapChain::create(desc, device_.Get(), factory_.Get(),
                                 command_queue_.Get(), window_);
}

std::expected<std::unique_ptr<RenderTarget>, core::Error>
DX12Device::createRenderTarget(const RenderTargetDesc& desc) {
    return DX12RenderTarget::create(desc, device_.Get());
}

std::expected<std::unique_ptr<Sampler>, core::Error>
DX12Device::createSampler(const SamplerDesc& desc) {
    // Sampler 仅持有 descriptor handle（非 COM 对象），无需命名。
    // 传 nullptr samplerHeap 会被 create() 拒绝并返回错误。
    return DX12Sampler::create(desc, device_.Get(), nullptr);
}

std::expected<std::unique_ptr<Fence>, core::Error>
DX12Device::createFence(uint64_t initialValue) {
    auto result = DX12Fence::create(device_.Get(), initialValue);
    if (!result) return std::unexpected(result.error());
    auto& f = *result;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "Fence@%p", f.get());
    setDebugName(f->fence(), nm);
    return result;
}

void DX12Device::uploadTextureData(Texture* dst, const void* data,
                                   uint32_t width, uint32_t height,
                                   TextureFormat format) {
    upload_context_->uploadTexture(static_cast<DX12Texture*>(dst), data,
                                   width, height, format);
}

// ============================================================
// 命令提交
// ============================================================

void DX12Device::executeCommandLists(CommandList** cmdLists, uint32_t count,
                                      Fence* fence, uint64_t fenceValue) {
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
    if (!command_queue_) return;
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
}

void DX12Device::clearCaches() {
    // D3D12 后端暂无内部缓存需要清理
}

CommandList* DX12Device::frameCommandList() {
    auto& frame = frames_[frame_index_];
    frame_cmd_wrapper_->setCommandList(frame->commandList());

    // 设置当前帧的描述符堆（bindResources 时分配 SRV 句柄用）
    auto* heap = shader_visible_heap_->heap();
    if (heap) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = heap->GetGPUDescriptorHandleForHeapStart();
        uint32_t descSize = shader_visible_heap_->descriptorSize();
        frame_cmd_wrapper_->setDescriptorHeap(heap, cpuBase, gpuBase, descSize);
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
    DX12_CHECK(hr);
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

} // namespace mulan::engine
