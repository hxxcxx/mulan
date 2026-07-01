/**
 * @file dx12_device.h
 * @brief D3D12 设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../device.h"
#include "../../window.h"
#include "dx12_common.h"
#include "dx12_convert.h"
#include "dx12_fence.h"
#include "dx12_buffer.h"
#include "dx12_texture.h"
#include "dx12_shader.h"
#include "dx12_pipeline_state.h"
#include "dx12_command_list.h"
#include "dx12_swap_chain.h"
#include "dx12_render_target.h"
#include "dx12_sampler.h"
#include "dx12_frame_context.h"
#include "dx12_descriptor_allocator.h"
#include "dx12_upload_context.h"

#include <vector>
#include <memory>

namespace mulan::engine {

class DX12Device final : public RHIDevice {
public:
    explicit DX12Device(const DeviceCreateInfo& ci);
    ~DX12Device();

    // --- Device 信息 ---
    GraphicsBackend backend() const override { return GraphicsBackend::D3D12; }
    const GPUDeviceCapabilities& capabilities() const override { return caps_; }
    const RenderConfig& renderConfig() const override { return render_config_; }
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
    void attachInfoQueue();
    void dumpInfoQueueMessages() const;

    ComPtr<IDXGIFactory4>              factory_;
    ComPtr<ID3D12Device>               device_;
    ComPtr<ID3D12CommandQueue>         command_queue_;
    ComPtr<ID3D12Debug>                debug_controller_;
    ComPtr<ID3D12InfoQueue>            info_queue_;

    GPUDeviceCapabilities                 caps_;
    RenderConfig                       render_config_;
    NativeWindowHandle                 window_;
    uint32_t                           frame_count_ = 2;
    uint32_t                           frame_index_ = 0;

    std::vector<std::unique_ptr<DX12FrameContext>> frames_;
    std::unique_ptr<DX12UploadContext>             upload_context_;

    // 帧命令列表包装（复用，避免每帧 new）
    std::unique_ptr<DX12CommandList>               frame_cmd_wrapper_;

    // Shader-visible descriptor heap for CBV/SRV/UAV
    std::unique_ptr<DX12DescriptorAllocator>       shader_visible_heap_;
};

} // namespace mulan::engine
