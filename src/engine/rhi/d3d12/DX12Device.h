/**
 * @file DX12Device.h
 * @brief D3D12 设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../Device.h"
#include "../../Window.h"
#include "DX12Common.h"
#include "DX12Convert.h"
#include "DX12Fence.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Shader.h"
#include "DX12PipelineState.h"
#include "DX12CommandList.h"
#include "DX12SwapChain.h"
#include "DX12RenderTarget.h"
#include "DX12Sampler.h"
#include "DX12FrameContext.h"
#include "DX12DescriptorAllocator.h"
#include "DX12UploadContext.h"

#include <vector>
#include <memory>

namespace mulan::engine {

class DX12Device final : public RHIDevice {
public:
    explicit DX12Device(const DeviceCreateInfo& ci);
    ~DX12Device();

    // --- Device 信息 ---
    GraphicsBackend backend() const override { return GraphicsBackend::D3D12; }
    const GPUDeviceCapabilities& capabilities() const override { return m_caps; }
    const RenderConfig& renderConfig() const override { return m_renderConfig; }
    Mat4 clipSpaceCorrectionMatrix() const override;

    // --- 资源创建 ---
    ResourcePtr<Buffer>        createBuffer(const BufferDesc& desc) override;
    ResourcePtr<Texture>       createTexture(const TextureDesc& desc) override;
    ResourcePtr<Shader>        createShader(const ShaderDesc& desc) override;
    ResourcePtr<PipelineState> createPipelineState(const GraphicsPipelineDesc& desc) override;
    ResourcePtr<CommandList>   createCommandList() override;
    ResourcePtr<SwapChain>     createSwapChain(const SwapChainDesc& desc) override;
    ResourcePtr<RenderTarget>  createRenderTarget(const RenderTargetDesc& desc) override;
    ResourcePtr<Sampler>       createSampler(const SamplerDesc& desc) override;
    ResourcePtr<Fence>         createFence(uint64_t initialValue = 0) override;

    // --- 资源销毁 ---
    void destroy(Buffer* resource) override;
    void destroy(Texture* resource) override;
    void destroy(Shader* resource) override;
    void destroy(PipelineState* resource) override;
    void destroy(CommandList* resource) override;
    void destroy(SwapChain* resource) override;
    void destroy(RenderTarget* resource) override;
    void destroy(Sampler* resource) override;
    void destroy(Fence* resource) override;

    // --- 提交命令 ---
    void executeCommandLists(CommandList** cmdLists, uint32_t count,
                             Fence* fence = nullptr,
                             uint64_t fenceValue = 0) override;
    void waitIdle() override;

    // --- 帧循环 ---
    void beginFrame() override;
    CommandList* frameCommandList() override;
    void submitAndPresent(SwapChain* swapchain) override;
    void submit() override;
    void present(SwapChain* swapchain) override;
    void submitOffscreen() override;

private:
    void init(const DeviceCreateInfo& ci);
    void createFactory();
    void findAdapter();
    void createDevice();
    void createCommandQueue();
    void createFrameContexts();
    void enableDebugLayer();

    ComPtr<IDXGIFactory4>              m_factory;
    ComPtr<ID3D12Device>               m_device;
    ComPtr<ID3D12CommandQueue>         m_commandQueue;
    ComPtr<ID3D12Debug>                m_debugController;

    GPUDeviceCapabilities                 m_caps;
    RenderConfig                       m_renderConfig;
    NativeWindowHandle                 m_window;
    uint32_t                           m_frameCount = 2;
    uint32_t                           m_frameIndex = 0;

    std::vector<std::unique_ptr<DX12FrameContext>> m_frames;
    std::unique_ptr<DX12UploadContext>             m_uploadContext;

    // 帧命令列表包装（复用，避免每帧 new）
    std::unique_ptr<DX12CommandList>               m_frameCmdWrapper;

    // Shader-visible descriptor heap for CBV/SRV/UAV
    std::unique_ptr<DX12DescriptorAllocator>       m_shaderVisibleHeap;
};

} // namespace mulan::engine
