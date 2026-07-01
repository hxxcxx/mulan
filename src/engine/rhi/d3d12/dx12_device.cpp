#include "dx12_device.h"

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
    // DX12 设备创建时自动选适配器
}

void DX12Device::createDevice() {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
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

ResourcePtr<Buffer> DX12Device::createBuffer(const BufferDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        std::fprintf(stderr, "[DX12 ERROR] createBuffer: device already removed! Reason=0x%08lX\n",
                     static_cast<unsigned long>(reason));
        dumpInfoQueueMessages();
    }

    auto* buf = new DX12Buffer(desc, device_.Get());

    // 上传初始数据
    if (buf->needsUpload()) {
        upload_context_->uploadBuffer(buf, buf->pendingData(), desc.size);
        buf->markUploaded();
    }

    return ResourcePtr<Buffer>(buf, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Texture> DX12Device::createTexture(const TextureDesc& desc) {
    return ResourcePtr<Texture>(new DX12Texture(desc, device_.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Shader> DX12Device::createShader(const ShaderDesc& desc) {
    return ResourcePtr<Shader>(new DX12Shader(desc), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<PipelineState> DX12Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    HRESULT reason = device_->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        std::fprintf(stderr, "[DX12 ERROR] createPipelineState('%.*s'): device already removed! Reason=0x%08lX\n",
                     static_cast<int>(desc.name.size()), desc.name.data(),
                     static_cast<unsigned long>(reason));
        dumpInfoQueueMessages();
    }
    return ResourcePtr<PipelineState>(new DX12PipelineState(desc, device_.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<CommandList> DX12Device::createCommandList() {
    // 独立命令列表（非帧循环用）
    auto allocator = ComPtr<ID3D12CommandAllocator>();
    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     IID_PPV_ARGS(&allocator));
    return ResourcePtr<CommandList>(new DX12CommandList(device_.Get(), allocator.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<SwapChain> DX12Device::createSwapChain(const SwapChainDesc& desc) {
    return ResourcePtr<SwapChain>(new DX12SwapChain(desc, device_.Get(), factory_.Get(),
                             command_queue_.Get(), window_), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<RenderTarget> DX12Device::createRenderTarget(const RenderTargetDesc& desc) {
    return ResourcePtr<RenderTarget>(new DX12RenderTarget(desc, device_.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Sampler> DX12Device::createSampler(const SamplerDesc& desc) {
    // DX12 sampler 需要描述符堆，这里用 nullptr 占位，后续接入主流程时传入 sampler heap
    return ResourcePtr<Sampler>(new DX12Sampler(desc, device_.Get(), nullptr), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Fence> DX12Device::createFence(uint64_t initialValue) {
    return ResourcePtr<Fence>(new DX12Fence(device_.Get(), initialValue), DeviceResourceDeleter{shared_from_this()});
}

// ============================================================
// 资源销毁
// ============================================================

void DX12Device::destroy(Buffer* r) { delete r; }
void DX12Device::destroy(Texture* r) { delete r; }
void DX12Device::destroy(Shader* r) { delete r; }
void DX12Device::destroy(PipelineState* r) { delete r; }
void DX12Device::destroy(CommandList* r) { delete r; }
void DX12Device::destroy(SwapChain* r) { delete r; }
void DX12Device::destroy(RenderTarget* r) { delete r; }
void DX12Device::destroy(Sampler* r) { delete r; }
void DX12Device::destroy(Fence* r) { delete r; }

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

void DX12Device::beginFrame() {
    frame_index_ = (frame_index_ + 1) % frame_count_;
    auto& frame = frames_[frame_index_];
    frame->waitForFence();
    frame->resetCommandAllocator();

    // 重置 shader-visible 描述符堆
    shader_visible_heap_->reset();
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
