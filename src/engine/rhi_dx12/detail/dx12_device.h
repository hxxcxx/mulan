/**
 * @file dx12_device.h
 * @brief D3D12 设备实现，资源工厂与后端入口
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/device.h"
#include "../rhi/window.h"
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
    math::Mat4 clipSpaceCorrectionMatrix() const override;
    bool isInitialized() const {
        return factory_ && device_ && command_queue_ && !frames_.empty() && upload_context_ && sampler_heap_ &&
               submissionFence() && sampler_heap_->isValid();
    }

    // --- 资源创建 ---
    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) override;
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override;
    Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc) override;
    Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc) override;
    Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(const ComputePipelineDesc& desc) override;
    Result<std::unique_ptr<CommandList>> createCommandList() override;
    Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc& desc) override;
    Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc& desc) override;
    Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc& desc) override;
    Result<std::unique_ptr<Fence>> createFence(uint64_t initialValue = 0) override;
    Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout& layout,
                                                       const BindGroupDesc& desc) override;

    // --- 资源上传 ---
    Result<void> uploadTextureData(Texture* dst, const TextureUploadDesc& upload) override;
    Result<void> beginUploadBatch() override;
    Result<void> flushUploadBatch() override;

    // --- 提交命令 ---
    Result<SubmissionToken> executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence = nullptr,
                                                uint64_t fenceValue = 0) override;
    Result<void> waitIdle() override;

    // --- 帧循环 ---
    Result<CommandList*> beginFrame(SwapChain* swapchain = nullptr) override;
    Result<SubmissionToken> endFrame(SwapChain* swapchain = nullptr) override;

    /// 惰性创建间接绘制 CommandSignature
    ID3D12CommandSignature* drawIndirectSignature();

private:
    Result<SubmissionToken> submitFrame();
    void init(const DeviceCreateInfo& ci);
    void createFactory();
    void findAdapter();
    void createDevice();
    void createCommandQueue();
    void createFrameContexts();
    void enableDebugLayer();
    void attachInfoQueue();
    void dumpInfoQueueMessages() const;

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<ID3D12Debug> debug_controller_;
    ComPtr<ID3D12InfoQueue> info_queue_;
    ComPtr<ID3D12CommandSignature> draw_indirect_sig_;

    GPUDeviceCapabilities caps_;
    RenderConfig render_config_;
    NativeWindowHandle window_;
    uint32_t frame_count_ = 2;
    uint32_t frame_index_ = 0;
    uint64_t frame_token_ = 0;  // 单调递增，BindGroup 句柄版本化用

    std::vector<std::unique_ptr<DX12FrameContext>> frames_;
    std::unique_ptr<DX12UploadContext> upload_context_;

    // 帧命令列表包装（复用，避免每帧 new）
    std::unique_ptr<DX12CommandList> frame_cmd_wrapper_;

    // 着色器可见的采样器描述符堆。
    // 采样器通过root signature中的描述符表进行绑定。
    std::unique_ptr<DX12DescriptorAllocator> sampler_heap_;
};

}  // namespace mulan::engine
