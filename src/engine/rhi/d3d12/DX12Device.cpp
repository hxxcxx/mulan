/**
 * @file DX12Device.cpp
 * @brief D3D12 设备实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Device.h"

namespace mulan::engine {

// ============================================================
// 构造 / 析构
// ============================================================

DX12Device::DX12Device(const DeviceCreateInfo& ci) {
    init(ci);
}

DX12Device::~DX12Device() {
    waitIdle();
    m_uploadContext.reset();
    m_frames.clear();
    m_shaderVisibleHeap.reset();
    m_commandQueue.Reset();
    m_device.Reset();
    m_factory.Reset();
}

// ============================================================
// 初始化
// ============================================================

void DX12Device::init(const DeviceCreateInfo& ci) {
    m_window       = ci.window;
    m_renderConfig = ci.renderConfig;
    m_frameCount   = ci.renderConfig.bufferCount > 0 ? ci.renderConfig.bufferCount : 2;
    DX12_LOG("[DX12] Init windowType=%d hwnd=%p frameCount=%u validation=%d\n",
             static_cast<int>(ci.window.type),
             reinterpret_cast<void*>(ci.window.win32.hWnd),
             m_frameCount, ci.enableValidation ? 1 : 0);
    if (ci.enableValidation) enableDebugLayer();
    createFactory();
    findAdapter();
    createDevice();
    createCommandQueue();
    createFrameContexts();

    m_uploadContext = std::make_unique<DX12UploadContext>(
        m_device.Get(), m_commandQueue.Get(), m_frameCount);

    m_frameCmdWrapper = std::make_unique<DX12CommandList>(nullptr);

    m_shaderVisibleHeap = std::make_unique<DX12DescriptorAllocator>(
        m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1024);

    m_caps.backend = GraphicsBackend::D3D12;
}

void DX12Device::enableDebugLayer() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        m_debugController = debug;
    }
}

void DX12Device::createFactory() {
    UINT flags = 0;
#ifdef _DEBUG
    // DXGIGetDebugInterface1 for DXGI debug — optional
#endif
    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    DX12_CHECK(hr);
}

void DX12Device::findAdapter() {
    // DX12 设备创建时自动选适配器
}

void DX12Device::createDevice() {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&m_device));
    DX12_CHECK(hr);
}

void DX12Device::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    HRESULT hr = m_device->CreateCommandQueue(&queueDesc,
                                              IID_PPV_ARGS(&m_commandQueue));
    DX12_CHECK(hr);
}

void DX12Device::createFrameContexts() {
    m_frames.reserve(m_frameCount);
    for (uint32_t i = 0; i < m_frameCount; ++i) {
        m_frames.push_back(std::make_unique<DX12FrameContext>(m_device.Get()));
    }
}

// ============================================================
// Clip Space — D3D12: Y↓ z∈[0,1]
// ============================================================

Mat4 DX12Device::clipSpaceCorrectionMatrix() const {
    // D3D12 NDC: Y↓, z∈[0,1]
    // 投影矩阵产出 z∈[-1,1]，需映射到 [0,1]: z' = 0.5*z + 0.5*w
    // w 必须保持不变，否则透视除法会污染 x/y
    Mat4 mat(1.0);
    mat[1][1] = -1.0;   // Y 翻转
    mat[2][2] =  0.5;   // z scale
    mat[3][2] =  0.5;   // z offset
    return mat;
}

// ============================================================
// 资源创建
// ============================================================

ResourcePtr<Buffer> DX12Device::createBuffer(const BufferDesc& desc) {
    auto* buf = new DX12Buffer(desc, m_device.Get());

    // 上传初始数据
    if (buf->needsUpload()) {
        m_uploadContext->uploadBuffer(buf, buf->pendingData(), desc.size);
        buf->markUploaded();
    }

    return ResourcePtr<Buffer>(buf, DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Texture> DX12Device::createTexture(const TextureDesc& desc) {
    return ResourcePtr<Texture>(new DX12Texture(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Shader> DX12Device::createShader(const ShaderDesc& desc) {
    return ResourcePtr<Shader>(new DX12Shader(desc), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<PipelineState> DX12Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    return ResourcePtr<PipelineState>(new DX12PipelineState(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<CommandList> DX12Device::createCommandList() {
    // 独立命令列表（非帧循环用）
    auto allocator = ComPtr<ID3D12CommandAllocator>();
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     IID_PPV_ARGS(&allocator));
    return ResourcePtr<CommandList>(new DX12CommandList(m_device.Get(), allocator.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<SwapChain> DX12Device::createSwapChain(const SwapChainDesc& desc) {
    return ResourcePtr<SwapChain>(new DX12SwapChain(desc, m_device.Get(), m_factory.Get(),
                             m_commandQueue.Get(), m_window), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<RenderTarget> DX12Device::createRenderTarget(const RenderTargetDesc& desc) {
    return ResourcePtr<RenderTarget>(new DX12RenderTarget(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Sampler> DX12Device::createSampler(const SamplerDesc& desc) {
    // DX12 sampler 需要描述符堆，这里用 nullptr 占位，后续接入主流程时传入 sampler heap
    return ResourcePtr<Sampler>(new DX12Sampler(desc, m_device.Get(), nullptr), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Fence> DX12Device::createFence(uint64_t initialValue) {
    return ResourcePtr<Fence>(new DX12Fence(m_device.Get(), initialValue), DeviceResourceDeleter{shared_from_this()});
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
    m_commandQueue->ExecuteCommandLists(count, lists.data());

    if (fence) {
        auto* dx12Fence = static_cast<DX12Fence*>(fence);
        m_commandQueue->Signal(dx12Fence->fence(), fenceValue);
    }
}

void DX12Device::waitIdle() {
    if (!m_commandQueue) return;
    ComPtr<ID3D12Fence> fence;
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    m_commandQueue->Signal(fence.Get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
}

// ============================================================
// 帧循环
// ============================================================

void DX12Device::beginFrame() {
    m_frameIndex = (m_frameIndex + 1) % m_frameCount;
    auto& frame = m_frames[m_frameIndex];
    frame->waitForFence();
    frame->resetCommandAllocator();

    // 重置 shader-visible 描述符堆
    m_shaderVisibleHeap->reset();
}

CommandList* DX12Device::frameCommandList() {
    auto& frame = m_frames[m_frameIndex];
    m_frameCmdWrapper->setCommandList(frame->commandList());

    // 设置当前帧的描述符堆（bindResources 时分配 SRV 句柄用）
    auto* heap = m_shaderVisibleHeap->heap();
    if (heap) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = heap->GetGPUDescriptorHandleForHeapStart();
        uint32_t descSize = m_shaderVisibleHeap->descriptorSize();
        m_frameCmdWrapper->setDescriptorHeap(heap, cpuBase, gpuBase, descSize);
    }

    return m_frameCmdWrapper.get();
}

void DX12Device::submitAndPresent(SwapChain* swapchain) {
    submit();
    present(swapchain);
}

void DX12Device::submit() {
    auto& frame = m_frames[m_frameIndex];

    // cmd list 已由 EngineView::cmd->end() 关闭，直接提交
    ID3D12CommandList* lists[] = { frame->commandList() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // Signal fence
    auto fenceVal = frame->fenceValue() + 1;
    frame->setFenceValue(fenceVal);
    HRESULT hr = m_commandQueue->Signal(frame->fence()->fence(), fenceVal);
    DX12_CHECK(hr);
}

void DX12Device::present(SwapChain* swapchain) {
    auto* dx12Swap = static_cast<DX12SwapChain*>(swapchain);
    dx12Swap->present();
    m_frameIndex = (m_frameIndex + 1) % m_frameCount;
}

void DX12Device::submitOffscreen() {
    auto& frame = m_frames[m_frameIndex];

    ID3D12CommandList* lists[] = { frame->commandList() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    auto fenceVal = frame->fenceValue() + 1;
    frame->setFenceValue(fenceVal);
    m_commandQueue->Signal(frame->fence()->fence(), fenceVal);
}

} // namespace mulan::engine
