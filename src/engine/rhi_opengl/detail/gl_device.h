/**
 * @file gl_device.h
 * @brief OpenGL 设备实现，资源工厂与后端入口
 * @author terry
 * @date 2026-04-16
 */

#pragma once

#include "gl_common.h"
#include "gl_context.h"
#include "gl_command_list.h"
#include "gl_sampler.h"
#include "../../rhi/device.h"
#include "../../rhi/window.h"

#include <vector>
#include <memory>

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
        bool enableValidation = true;
        NativeWindowHandle window;
        RenderConfig renderConfig;
        const char* appName = "MulanGeo";
    };

    explicit GLDevice(const CreateInfo& ci) { init(ci); }

    /// 从通用 DeviceCreateInfo 构造（供 RHIDevice::create 工厂调用）
    explicit GLDevice(const DeviceCreateInfo& ci) {
        CreateInfo glCI;
        glCI.enableValidation = ci.enableValidation;
        glCI.window = ci.window;
        glCI.renderConfig = ci.renderConfig;
        glCI.appName = ci.appName;
        init(glCI);
    }

    ~GLDevice();

    // --- Device 信息 ---

    GraphicsBackend backend() const override { return GraphicsBackend::OpenGL; }

    const GPUDeviceCapabilities& capabilities() const override { return caps_; }

    const RenderConfig& renderConfig() const override { return render_config_; }

    math::Mat4 clipSpaceCorrectionMatrix() const override {
        return math::Mat4(1.0);  // OpenGL: 标准右手坐标，无需修正
    }

    // --- 资源创建（桩实现，后续补全）---

    core::Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) override;
    core::Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override;
    core::Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc) override;
    core::Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc) override;
    core::Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(
            const ComputePipelineDesc& desc) override;
    core::Result<std::unique_ptr<CommandList>> createCommandList() override;
    core::Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc& desc) override;
    core::Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc& desc) override;
    core::Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc& desc) override;
    core::Result<std::unique_ptr<Fence>> createFence(uint64_t initialValue = 0) override;
    core::Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout& layout,
                                                             const BindGroupDesc& desc) override;

    void uploadTextureData(Texture* dst, const TextureUploadDesc& upload) override;
    void beginUploadBatch() override {}
    void flushUploadBatch() override {}

    // --- 提交命令 ---

    void executeCommandLists(CommandList** cmdLists, uint32_t count, Fence* fence = nullptr,
                             uint64_t fenceValue = 0) override;

    void waitIdle() override;

    // --- 帧循环 ---

    void beginFrame(SwapChain* swapchain = nullptr) override;
    void clearCaches() override;
    CommandList* frameCommandList() override;
    core::Result<SubmissionToken> submitAndPresent(SwapChain* swapchain) override;
    core::Result<SubmissionToken> submit() override;
    void present(SwapChain* swapchain) override;
    core::Result<SubmissionToken> submitOffscreen() override;

    // --- OpenGL 特有访问器 ---

    GLContext* context() const { return context_.get(); }

    bool isInitialized() const { return initialized_; }

private:
    void init(const CreateInfo& ci);
    void shutdown();
    void queryCapabilities();

    bool initialized_ = false;
    NativeWindowHandle native_window_;
    RenderConfig render_config_;
    GPUDeviceCapabilities caps_;
    std::unique_ptr<GLContext> context_;

    // 帧命令列表缓存（直接成员，避免堆指针被污染）
    //GLCommandList frame_command_list_;
    std::unique_ptr<GLCommandList> frame_command_list_;
};

}  // namespace mulan::engine
