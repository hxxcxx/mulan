/**
 * @file GLDevice.h
 * @brief OpenGL 设备实现，资源工厂与后端入口
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#include "GLCommon.h"
#include "GLCommandList.h"
#include "GLSampler.h"
#include "../Device.h"
#include "../../Window.h"

#include <vector>
#include <memory>
#include <cstdio>

namespace mulan::engine {

// ── 前向声明 OpenGL 子资源类型（后续实现）──
class GLBuffer;
class GLTexture;
class GLShader;
class GLPipelineState;
class GLSwapChain;
class GLFence;

class GLDevice : public RHIDevice {
public:
    struct CreateInfo {
        bool               enableValidation = true;
        NativeWindowHandle window;
        RenderConfig       renderConfig;
        const char*        appName = "MulanGeo";
    };

    explicit GLDevice(const CreateInfo& ci) {
        init(ci);
    }

    /// 从通用 DeviceCreateInfo 构造（供 RHIDevice::create 工厂调用）
    explicit GLDevice(const DeviceCreateInfo& ci) {
        CreateInfo glCI;
        glCI.enableValidation = ci.enableValidation;
        glCI.window           = ci.window;
        glCI.renderConfig     = ci.renderConfig;
        glCI.appName          = ci.appName;
        init(glCI);
    }

    ~GLDevice();

    // --- Device 信息 ---

    GraphicsBackend backend() const override {
        return GraphicsBackend::OpenGL;
    }

    const GPUDeviceCapabilities& capabilities() const override {
        return m_caps;
    }

    const RenderConfig& renderConfig() const override {
        return m_renderConfig;
    }

    Mat4 clipSpaceCorrectionMatrix() const override {
        return Mat4(1.0);  // OpenGL: 标准右手坐标，无需修正
    }

    // --- 资源创建（桩实现，后续补全）---

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

    void executeCommandLists(CommandList** cmdLists,
                             uint32_t count,
                             Fence* fence = nullptr,
                             uint64_t fenceValue = 0) override;

    void waitIdle() override;

    // --- 帧循环 ---

    void beginFrame() override;
    CommandList* frameCommandList() override;
    void submitAndPresent(SwapChain* swapchain) override;
    void submitOffscreen() override;

    // --- OpenGL 特有访问器 ---

#ifdef _WIN32
    HDC   hdc()   const { return m_hdc; }
    HGLRC hglrc() const { return m_hglrc; }
#endif

    bool isInitialized() const { return m_initialized; }

private:
    void init(const CreateInfo& ci);
    void shutdown();
    void queryCapabilities();

#ifdef _WIN32
    bool createWGLContext(HWND hwnd, bool enableValidation);
    HDC   m_hdc   = nullptr;
    HGLRC m_hglrc = nullptr;
    HWND  m_hwnd  = nullptr;
#endif

    bool               m_initialized = false;
    NativeWindowHandle m_nativeWindow;
    RenderConfig       m_renderConfig;
    GPUDeviceCapabilities m_caps;

    // 帧命令列表缓存（直接成员，避免堆指针被污染）
    //GLCommandList m_frameCommandList;
    std::unique_ptr<GLCommandList> m_frameCommandList;
};

} // namespace mulan::Engine
