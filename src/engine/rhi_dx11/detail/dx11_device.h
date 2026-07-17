/**
 * @file dx11_device.h
 * @brief D3D11 设备实现，资源工厂与后端入口
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../../rhi/device.h"
#include "../../rhi/window.h"
#include "dx11_common.h"
#include "dx11_convert.h"
#include "dx11_fence.h"
#include "dx11_bind_group.h"
#include "dx11_buffer.h"
#include "dx11_texture.h"
#include "dx11_shader.h"
#include "dx11_pipeline_state.h"
#include "dx11_command_list.h"
#include "dx11_swap_chain.h"
#include "dx11_render_target.h"
#include "dx11_sampler.h"

#include <memory>

namespace mulan::engine {

class DX11Device final : public RHIDevice {
public:
    explicit DX11Device(const DeviceCreateInfo& ci);
    ~DX11Device();

    // --- Device 信息 ---
    GraphicsBackend backend() const override { return GraphicsBackend::D3D11; }
    const GPUDeviceCapabilities& capabilities() const override { return m_caps; }
    const RenderConfig& renderConfig() const override { return m_renderConfig; }
    math::Mat4 clipSpaceCorrectionMatrix() const override;

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

    ResultVoid uploadTextureData(Texture* dst, const TextureUploadDesc& upload) override;
    ResultVoid beginUploadBatch() override { return {}; }
    ResultVoid flushUploadBatch() override { return {}; }

    // --- 提交命令 ---
    Result<SubmissionToken> executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence = nullptr,
                                                uint64_t fenceValue = 0) override;
    ResultVoid waitIdle() override;

    // --- 帧循环 ---
    Result<CommandList*> beginFrame(SwapChain* swapchain = nullptr) override;
    Result<SubmissionToken> endFrame(SwapChain* swapchain = nullptr) override;

private:
    bool isInitialized() const noexcept override {
        return m_factory && m_device && m_immediateCtx && m_frameCmdList && submissionFence();
    }

    Result<SubmissionToken> submitFrame();
    void init(const DeviceCreateInfo& ci);
    uint32_t resolveSampleCount(TextureFormat colorFormat, TextureFormat depthFormat, bool hasDepth,
                                uint32_t requestedSampleCount) const;

    ComPtr<IDXGIFactory2> m_factory;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediateCtx;
    ComPtr<ID3D11DeviceContext1> m_immediateCtx1;
    ComPtr<ID3D11Debug> m_debugDevice;

    GPUDeviceCapabilities m_caps;
    RenderConfig m_renderConfig;
    NativeWindowHandle m_window;

    std::unique_ptr<DX11CommandList> m_frameCmdList;
};

}  // namespace mulan::engine
