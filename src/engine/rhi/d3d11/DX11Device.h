/**
 * @file DX11Device.h
 * @brief D3D11 设备实现，资源工厂与后端入口
 * @author zmb
 * @date 2026-04-19
 */
#pragma once

#include "../Device.h"
#include "../../Window.h"
#include "DX11Common.h"
#include "DX11Convert.h"
#include "DX11Fence.h"
#include "DX11Buffer.h"
#include "DX11Texture.h"
#include "DX11Shader.h"
#include "DX11PipelineState.h"
#include "DX11CommandList.h"
#include "DX11SwapChain.h"
#include "DX11RenderTarget.h"
#include "DX11Sampler.h"

#include <memory>

namespace mulan::engine
{

class DX11Device final : public RHIDevice
{
public:
    explicit DX11Device(const DeviceCreateInfo& ci);
    ~DX11Device();

    // --- Device 信息 ---
    GraphicsBackend backend() const override { return GraphicsBackend::D3D11; }
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
    void submitOffscreen() override;

private:
    void init(const DeviceCreateInfo& ci);

    ComPtr<IDXGIFactory2>              m_factory;
    ComPtr<ID3D11Device>               m_device;
    ComPtr<ID3D11DeviceContext>         m_immediateCtx;
    ComPtr<ID3D11Debug>                m_debugDevice;

    GPUDeviceCapabilities                 m_caps;
    RenderConfig                       m_renderConfig;
    NativeWindowHandle                 m_window;

    std::unique_ptr<DX11CommandList>   m_frameCmdList;
};

} // namespace mulan::engine
